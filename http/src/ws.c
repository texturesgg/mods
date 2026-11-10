/* SPDX-License-Identifier: GPL-3.0-or-later */
/* WebSocket client (RFC 6455): the upgrade request, then frames. Messages
 * the server sends are queued whole; pings are answered; a ping goes out
 * every 30 seconds so idle connections stay open. */
#include "internal.h"

#include "tgg/net.h"

#include "mbedtls/base64.h"
#include "mbedtls/sha1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEEPALIVE_MS 30000.0

typedef struct message {
    struct message* next;
    size_t len;
    char text[];
} message;

struct http_ws {
    http_ws* next;
    conn* c;
    int state;     /* HTTP_WS_* */
    int upgraded;  /* the server said 101 */
    char accept[64]; /* the Sec-WebSocket-Accept it must say */
    buf out, in;
    buf partial;   /* a message arriving in pieces */
    int partial_on;
    message *first, *last;
    double last_sent_ms;
    char error[200];
};

static http_ws* all;

static void closed(http_ws* ws, const char* why)
{
    if (ws->state == HTTP_WS_CLOSED) {
        return;
    }
    ws->state = HTTP_WS_CLOSED;
    snprintf(ws->error, sizeof ws->error, "%s", why);
    tgg_log("WebSocket closed: %s", why);
    conn_close(ws->c);
    ws->c = NULL;
}

/* Queues a frame with `opcode`, masked as a client must */
static int frame(http_ws* ws, int opcode, const void* data, size_t len)
{
    unsigned char head[14];
    size_t n = 0;
    head[n++] = (unsigned char) (0x80 | opcode);
    if (len < 126) {
        head[n++] = (unsigned char) (0x80 | len);
    } else if (len <= 0xFFFF) {
        head[n++] = 0x80 | 126;
        head[n++] = (unsigned char) (len >> 8);
        head[n++] = (unsigned char) len;
    } else {
        head[n++] = 0x80 | 127;
        for (int i = 7; i >= 0; i--) {
            head[n++] = (unsigned char) ((uint64_t) len >> (i * 8));
        }
    }
    unsigned char mask[4];
    if (tgg_net_random(mask, sizeof mask) != TGG_NET_OK) {
        return -1;
    }
    memcpy(head + n, mask, 4);
    n += 4;
    const size_t start = ws->out.len;
    if (buf_add(&ws->out, head, n) != 0 || buf_add(&ws->out, data, len) != 0) {
        return -1;
    }
    unsigned char* p = (unsigned char*) ws->out.data + start + n;
    for (size_t i = 0; i < len; i++) {
        p[i] ^= mask[i & 3];
    }
    ws->last_sent_ms = tgg_net_time_ms();
    return 0;
}

static void queue(http_ws* ws, const char* data, size_t len)
{
    message* m = malloc(sizeof *m + len + 1);
    if (m == NULL) {
        closed(ws, "Out of memory");
        return;
    }
    m->next = NULL;
    m->len = len;
    memcpy(m->text, data, len);
    m->text[len] = '\0';
    if (ws->last != NULL) {
        ws->last->next = m;
    } else {
        ws->first = m;
    }
    ws->last = m;
}

/* The upgrade answer: 1 when it's in and good, 0 not yet, -1 refused */
static int upgrade(http_ws* ws)
{
    const char* blank = strstr(ws->in.data != NULL ? ws->in.data : "", "\r\n\r\n");
    if (blank == NULL) {
        return 0;
    }
    const size_t head_len = (size_t) (blank - ws->in.data) + 4;
    int status = 0;
    if (sscanf(ws->in.data, "HTTP/1.%*d %d", &status) != 1 || status != 101) {
        char why[200];
        /* The server's own words, when it gave a JSON { "error" } */
        const char* error = strstr(ws->in.data + head_len, "\"error\":\"");
        if (error != NULL) {
            error += 9;
            snprintf(why, sizeof why, "%.*s", (int) strcspn(error, "\""), error);
        } else {
            snprintf(why, sizeof why, "The server refused the connection (%d)", status);
        }
        closed(ws, why);
        return -1;
    }
    if (strstr(ws->in.data, ws->accept) == NULL || strstr(ws->in.data, ws->accept) > blank) {
        closed(ws, "The server didn't answer as a WebSocket");
        return -1;
    }
    buf_drop(&ws->in, head_len);
    ws->upgraded = 1;
    ws->state = HTTP_WS_OPEN;
    return 1;
}

/* Takes whole frames from ws->in */
static void frames(http_ws* ws)
{
    while (ws->state == HTTP_WS_OPEN && ws->in.len >= 2) {
        const unsigned char* p = (const unsigned char*) ws->in.data;
        const int fin = p[0] & 0x80;
        const int opcode = p[0] & 0x0F;
        const int masked = p[1] & 0x80;
        uint64_t len = p[1] & 0x7F;
        size_t at = 2;
        if (len == 126) {
            if (ws->in.len < 4) {
                return;
            }
            len = (uint64_t) p[2] << 8 | p[3];
            at = 4;
        } else if (len == 127) {
            if (ws->in.len < 10) {
                return;
            }
            len = 0;
            for (int i = 0; i < 8; i++) {
                len = len << 8 | p[2 + i];
            }
            at = 10;
        }
        if (len > HTTP_WS_MAX_MESSAGE) {
            closed(ws, "The server sent a message too big to take");
            return;
        }
        const size_t mask_at = at;
        at += masked ? 4 : 0;
        if (ws->in.len < at + len) {
            return;
        }
        char* data = ws->in.data + at;
        if (masked) {
            for (size_t i = 0; i < len; i++) {
                data[i] ^= (char) p[mask_at + (i & 3)];
            }
        }
        switch (opcode) {
        case 0x0: /* the rest of a message */
        case 0x1: /* text */
        case 0x2: /* binary, taken like text */
            if (opcode != 0 && ws->partial_on) {
                closed(ws, "The server broke the WebSocket protocol");
                return;
            }
            if (fin && !ws->partial_on) {
                queue(ws, data, (size_t) len);
            } else {
                ws->partial_on = 1;
                if (ws->partial.len + len > HTTP_WS_MAX_MESSAGE ||
                    buf_add(&ws->partial, data, (size_t) len) != 0)
                {
                    closed(ws, "The server sent a message too big to take");
                    return;
                }
                if (fin) {
                    queue(ws, ws->partial.data, ws->partial.len);
                    buf_free(&ws->partial);
                    ws->partial_on = 0;
                }
            }
            break;
        case 0x8: { /* close */
            char why[160] = "The server closed the connection";
            if (len > 2) {
                snprintf(why, sizeof why, "%.*s", (int) (len - 2), data + 2);
            }
            frame(ws, 0x8, data, len >= 2 ? 2 : 0);
            conn_flush(ws->c, &ws->out);
            closed(ws, why);
            return;
        }
        case 0x9: /* ping */
            frame(ws, 0xA, data, (size_t) len);
            break;
        default: /* pong, and anything newer */
            break;
        }
        buf_drop(&ws->in, at + (size_t) len);
    }
}

static void pump(http_ws* ws)
{
    if (ws->state == HTTP_WS_CLOSED) {
        return;
    }
    const int step = conn_step(ws->c);
    if (step == CONN_FAILED) {
        closed(ws, conn_error(ws->c));
        return;
    }
    if (step != CONN_READY) {
        return;
    }
    if (ws->state == HTTP_WS_OPEN && tgg_net_time_ms() - ws->last_sent_ms > KEEPALIVE_MS) {
        frame(ws, 0x9, "", 0);
    }
    if (conn_flush(ws->c, &ws->out) != 0) {
        closed(ws, "The connection broke");
        return;
    }
    char chunk[16384];
    for (;;) {
        const int n = conn_read(ws->c, chunk, sizeof chunk);
        if (n < 0) {
            if (!ws->upgraded && upgrade(ws) < 0) {
                return;
            }
            frames(ws);
            closed(ws, "The connection closed");
            return;
        }
        if (n == 0) {
            break;
        }
        if (ws->in.len + (size_t) n > HTTP_WS_MAX_MESSAGE + 65536 ||
            buf_add(&ws->in, chunk, (size_t) n) != 0)
        {
            closed(ws, "The server sent too much at once");
            return;
        }
    }
    if (!ws->upgraded && upgrade(ws) <= 0) {
        return;
    }
    frames(ws);
    if (ws->state == HTTP_WS_OPEN && conn_flush(ws->c, &ws->out) != 0) {
        closed(ws, "The connection broke");
    }
}

void ws_pump_all(void)
{
    for (http_ws* ws = all; ws != NULL; ws = ws->next) {
        pump(ws);
    }
}

http_ws* http_ws_open(const char* url_text, const char* headers)
{
    url u;
    if (url_text == NULL || !url_parse(url_text, &u) || !u.ws) {
        tgg_log("can't open a WebSocket to \"%s\"", url_text != NULL ? url_text : "");
        return NULL;
    }
    http_ws* ws = calloc(1, sizeof *ws);
    if (ws == NULL) {
        return NULL;
    }
    ws->next = all;
    all = ws;
    ws->c = conn_open(&u);
    if (ws->c == NULL) {
        ws->state = HTTP_WS_CLOSED;
        snprintf(ws->error, sizeof ws->error, "%s", conn_open_error());
        return ws;
    }
    /* The key, and the answer it calls for */
    unsigned char nonce[16];
    unsigned char key[32];
    size_t key_len = 0;
    unsigned char digest[20];
    unsigned char accept[40];
    size_t accept_len = 0;
    char keyed[64];
    if (tgg_net_random(nonce, sizeof nonce) != TGG_NET_OK ||
        mbedtls_base64_encode(key, sizeof key, &key_len, nonce, sizeof nonce) != 0)
    {
        closed(ws, "No secure random numbers");
        return ws;
    }
    snprintf(keyed, sizeof keyed, "%.*s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", (int) key_len,
             (const char*) key);
    if (mbedtls_sha1((const unsigned char*) keyed, strlen(keyed), digest) != 0 ||
        mbedtls_base64_encode(accept, sizeof accept - 1, &accept_len, digest, sizeof digest) != 0)
    {
        closed(ws, "Couldn't start the WebSocket");
        return ws;
    }
    snprintf(ws->accept, sizeof ws->accept, "%.*s", (int) accept_len, (const char*) accept);

    char head[4096];
    char host[300];
    const int default_port = u.tls ? 443 : 80;
    if (u.port == default_port) {
        snprintf(host, sizeof host, strchr(u.host, ':') ? "[%s]" : "%s", u.host);
    } else {
        snprintf(host, sizeof host, strchr(u.host, ':') ? "[%s]:%d" : "%s:%d", u.host, u.port);
    }
    const int n = snprintf(head, sizeof head,
                           "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: tgg.http/0.1\r\n"
                           "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Key: %.*s\r\nSec-WebSocket-Version: 13\r\n%s%s\r\n",
                           u.path, host, (int) key_len, (const char*) key,
                           headers != NULL ? headers : "",
                           headers != NULL && headers[0] != '\0' ? "\r\n" : "");
    if (n < 0 || (size_t) n >= sizeof head || buf_add(&ws->out, head, (size_t) n) != 0) {
        closed(ws, "The WebSocket's address is too long");
        return ws;
    }
    ws->state = HTTP_WS_CONNECTING;
    pump(ws);
    return ws;
}

int http_ws_state(http_ws* ws)
{
    if (ws == NULL) {
        return HTTP_WS_CLOSED;
    }
    pump(ws);
    return ws->state;
}

int http_ws_send(http_ws* ws, const char* text, size_t len)
{
    if (ws == NULL || ws->state != HTTP_WS_OPEN || (text == NULL && len != 0)) {
        return -1;
    }
    if (frame(ws, 0x1, text, len) != 0) {
        closed(ws, "Out of memory");
        return -1;
    }
    if (conn_flush(ws->c, &ws->out) != 0) {
        closed(ws, "The connection broke");
        return -1;
    }
    return 0;
}

size_t http_ws_peek(http_ws* ws)
{
    if (ws == NULL) {
        return 0;
    }
    if (ws->first == NULL) {
        pump(ws);
    }
    return ws->first != NULL ? ws->first->len : 0;
}

int http_ws_receive(http_ws* ws, char* out, size_t cap)
{
    if (ws == NULL) {
        return 0;
    }
    if (ws->first == NULL) {
        pump(ws);
    }
    message* m = ws->first;
    if (m == NULL) {
        return 0;
    }
    if (out == NULL || cap < m->len + 1) {
        return -1;
    }
    memcpy(out, m->text, m->len + 1);
    ws->first = m->next;
    if (ws->first == NULL) {
        ws->last = NULL;
    }
    const int n = (int) m->len;
    free(m);
    return n;
}

const char* http_ws_error(http_ws* ws)
{
    return ws != NULL ? ws->error : "";
}

void http_ws_close(http_ws* ws)
{
    if (ws == NULL) {
        return;
    }
    for (http_ws** p = &all; *p != NULL; p = &(*p)->next) {
        if (*p == ws) {
            *p = ws->next;
            break;
        }
    }
    if (ws->state == HTTP_WS_OPEN) {
        const unsigned char normal[2] = {0x03, 0xE8}; /* 1000 */
        frame(ws, 0x8, normal, sizeof normal);
        conn_flush(ws->c, &ws->out);
    }
    conn_close(ws->c);
    buf_free(&ws->out);
    buf_free(&ws->in);
    buf_free(&ws->partial);
    for (message* m = ws->first; m != NULL;) {
        message* next = m->next;
        free(m);
        m = next;
    }
    free(ws);
}
