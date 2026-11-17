/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Fuzzes tgg.http's reading of what a server sends: response heads, bodies
 * (sized, chunked, cut short), WebSocket upgrades and frames. The connection
 * (conn.c, TCP and TLS) is replaced by one that hands over fuzz bytes in
 * fuzz-sized reads, so the driver reaches the code after TLS. */
#include "../../http/src/internal.h"
#include "port.h"

#include "tgg/events.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- The connection: reads come from the script */

struct conn {
    int open;
};

static Fz* script;
static int conn_state = CONN_READY;
/* The bytes of the next reads, and whether the server closed after them */
static const uint8_t* pending;
static size_t pending_len;
static int server_closed;

conn* conn_open(const url* u)
{
    (void) strlen(u->host), (void) strlen(u->path);
    conn* c = calloc(1, sizeof *c);
    c->open = 1;
    return c;
}

const char* conn_open_error(void)
{
    return "no connection";
}

int conn_step(conn* c)
{
    (void) c;
    return conn_state;
}

int conn_write(conn* c, const void* data, size_t len)
{
    (void) c;
    volatile uint8_t sink = 0;
    for (size_t i = 0; i < len; i++) {
        sink ^= ((const uint8_t*) data)[i];
    }
    return (int) len;
}

int conn_read(conn* c, void* buf, size_t cap)
{
    (void) c;
    if (pending_len == 0) {
        return server_closed ? -1 : 0;
    }
    /* A read of any size up to what's waiting */
    size_t n = 1 + fz_u16(script) % pending_len;
    n = n < cap ? n : cap;
    memcpy(buf, pending, n);
    pending += n;
    pending_len -= n;
    return (int) n;
}

int conn_flush(conn* c, buf* out)
{
    if (out->len > 0 && conn_write(c, out->data, out->len) < 0) {
        return -1;
    }
    buf_drop(out, out->len);
    return 0;
}

const char* conn_error(const conn* c)
{
    (void) c;
    return "the connection broke";
}

void conn_close(conn* c)
{
    free(c);
}

/* The Sec-WebSocket-Accept for the key a nonce of zeros makes (port.c's
 * tgg_net_random) */
static const char upgrade_ok[] = "HTTP/1.1 101 Switching Protocols\r\n"
                                 "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                                 "Sec-WebSocket-Accept: ICX+Yqv66kxgM0FcWaLWlFLwTAI=\r\n\r\n";

static void pump_all(void)
{
    request_pump_all();
    ws_pump_all();
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    Fz f = {data, size};
    script = &f;
    fz_reset();
    pending_len = 0;
    server_closed = 0;
    conn_state = CONN_READY;

    const unsigned mode = fz_byte(&f);
    http_request* r = NULL;
    http_ws* ws = NULL;
    static uint8_t staged[65536];
    if (mode & 1) {
        ws = http_ws_open("wss://example.com/socket", "X-Fuzz: 1\r\n");
        if (mode & 2) {
            memcpy(staged, upgrade_ok, sizeof upgrade_ok - 1);
            pending = staged;
            pending_len = sizeof upgrade_ok - 1;
            pump_all();
        }
    } else {
        r = http_request_start((mode & 2) ? "POST" : "GET", "https://example.com/api?x=1",
                               "Accept: */*\r\n", "body", 4);
    }
    for (int step = 0; step < 64 && f.n > 0; step++) {
        const unsigned op = fz_byte(&f) % 6;
        if (op <= 2) {
            /* What the server sends next */
            const uint8_t* bytes;
            const size_t n = fz_bytes(&f, &bytes, sizeof staged);
            memcpy(staged, bytes, n);
            pending = staged;
            pending_len = n;
            pump_all();
        } else if (op == 3) {
            server_closed = 1;
            pump_all();
        } else if (op == 4) {
            conn_state = (int) (fz_byte(&f) % 3) - 1;
            pump_all();
        } else if (ws != NULL) {
            char out[4096];
            http_ws_peek(ws);
            while (http_ws_receive(ws, out, sizeof out) > 0) {
            }
            http_ws_send(ws, "hi", 2);
            http_ws_state(ws);
            http_ws_error(ws);
        } else {
            http_request_poll(r);
            size_t len;
            const char* body = http_response_body(r, &len);
            volatile char sink = 0;
            for (size_t i = 0; body != NULL && i < len; i++) {
                sink ^= body[i];
            }
            http_response_status(r);
            http_request_error(r);
        }
    }
    server_closed = 1;
    pump_all();
    if (r != NULL) {
        http_request_free(r);
    }
    if (ws != NULL) {
        char out[4096];
        while (http_ws_receive(ws, out, sizeof out) > 0) {
        }
        http_ws_close(ws);
    }
    return 0;
}
