/* HTTP/1.1 requests: one connection each, "Connection: close", the response
 * read whole (Content-Length, chunked, or until the server closes). */
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct http_request {
    http_request* next;
    conn* c;
    int state; /* HTTP_* */
    buf out, in;
    int status;
    size_t head_len;      /* through the blank line; 0 until it's in */
    long content_length;  /* -1 when not given */
    int chunked;
    buf body;
    char error[200];
};

static http_request* all;

/* strncasecmp, which not every C runtime has */
static int same_name(const char* a, const char* b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        const char x = (char) (a[i] >= 'A' && a[i] <= 'Z' ? a[i] + 32 : a[i]);
        const char y = (char) (b[i] >= 'A' && b[i] <= 'Z' ? b[i] + 32 : b[i]);
        if (x != y) {
            return 0;
        }
    }
    return 1;
}

static void finish(http_request* r, int state, const char* error)
{
    r->state = state;
    if (error != NULL) {
        snprintf(r->error, sizeof r->error, "%s", error);
    }
    conn_close(r->c);
    r->c = NULL;
    buf_free(&r->out);
}

/* A header's value from the head, or NULL; `out` gets it */
static int header(const char* head, size_t len, const char* name, char* out, size_t cap)
{
    const size_t n = strlen(name);
    for (const char* line = head; line < head + len;) {
        const char* eol = strstr(line, "\r\n");
        if (eol == NULL || eol == line) {
            break;
        }
        if ((size_t) (eol - line) > n && same_name(line, name, n) && line[n] == ':') {
            const char* v = line + n + 1;
            while (*v == ' ' || *v == '\t') {
                v++;
            }
            const size_t vlen = (size_t) (eol - v);
            snprintf(out, cap, "%.*s", (int) (vlen < cap ? vlen : cap - 1), v);
            return 1;
        }
        line = eol + 2;
    }
    return 0;
}

/* Decodes a chunked body: 1 when it's whole (into r->body), 0 not yet, -1 bad */
static int dechunk(http_request* r, const char* p, size_t len)
{
    buf body = {0};
    const char* end = p + len;
    for (;;) {
        const char* eol = NULL;
        for (const char* q = p; q + 1 < end; q++) {
            if (q[0] == '\r' && q[1] == '\n') {
                eol = q;
                break;
            }
        }
        if (eol == NULL) {
            buf_free(&body);
            return 0;
        }
        char* stop = NULL;
        const unsigned long size = strtoul(p, &stop, 16);
        if (stop == p) {
            buf_free(&body);
            return -1;
        }
        p = eol + 2;
        if (size == 0) {
            buf_free(&r->body);
            r->body = body;
            if (r->body.data == NULL) {
                buf_add(&r->body, "", 0);
            }
            return 1;
        }
        if ((size_t) (end - p) < size + 2) {
            buf_free(&body);
            return 0;
        }
        if (body.len + size > HTTP_MAX_RESPONSE || buf_add(&body, p, size) != 0) {
            buf_free(&body);
            return -1;
        }
        p += size + 2;
    }
}

/* Reads what came; finishes when the response is whole */
static void take(http_request* r, int closed)
{
    if (r->head_len == 0) {
        const char* blank = strstr(r->in.data != NULL ? r->in.data : "", "\r\n\r\n");
        if (blank == NULL) {
            if (closed) {
                finish(r, HTTP_FAILED, "The server closed the connection without answering");
            }
            return;
        }
        r->head_len = (size_t) (blank - r->in.data) + 4;
        if (sscanf(r->in.data, "HTTP/1.%*d %d", &r->status) != 1) {
            finish(r, HTTP_FAILED, "The server's answer wasn't HTTP");
            return;
        }
        char value[64];
        r->content_length = -1;
        if (header(r->in.data, r->head_len, "Content-Length", value, sizeof value)) {
            r->content_length = atol(value);
        }
        r->chunked = header(r->in.data, r->head_len, "Transfer-Encoding", value, sizeof value) &&
                     strstr(value, "chunked") != NULL;
    }
    const char* body = r->in.data + r->head_len;
    const size_t have = r->in.len - r->head_len;
    if (r->chunked) {
        const int whole = dechunk(r, body, have);
        if (whole < 0) {
            finish(r, HTTP_FAILED, "The server's answer was cut or too big");
        } else if (whole > 0) {
            finish(r, HTTP_DONE, NULL);
        } else if (closed) {
            finish(r, HTTP_FAILED, "The server's answer was cut");
        }
        return;
    }
    if ((r->content_length >= 0 && have >= (size_t) r->content_length) || closed) {
        if (r->content_length >= 0 && have < (size_t) r->content_length) {
            finish(r, HTTP_FAILED, "The server's answer was cut");
            return;
        }
        const size_t n = r->content_length >= 0 ? (size_t) r->content_length : have;
        buf_free(&r->body);
        if (buf_add(&r->body, body, n) != 0) {
            finish(r, HTTP_FAILED, "Out of memory");
            return;
        }
        finish(r, HTTP_DONE, NULL);
    }
}

static void pump(http_request* r)
{
    if (r->state != HTTP_WORKING) {
        return;
    }
    const int step = conn_step(r->c);
    if (step == CONN_FAILED) {
        finish(r, HTTP_FAILED, conn_error(r->c));
        return;
    }
    if (step != CONN_READY) {
        return;
    }
    if (conn_flush(r->c, &r->out) != 0) {
        finish(r, HTTP_FAILED, "The connection broke");
        return;
    }
    char chunk[16384];
    for (;;) {
        const int n = conn_read(r->c, chunk, sizeof chunk);
        if (n == 0) {
            take(r, 0);
            return;
        }
        if (n < 0) {
            take(r, 1);
            return;
        }
        if (r->in.len + (size_t) n > HTTP_MAX_RESPONSE + 65536 ||
            buf_add(&r->in, chunk, (size_t) n) != 0)
        {
            finish(r, HTTP_FAILED, "The server's answer is too big");
            return;
        }
        take(r, 0);
        if (r->state != HTTP_WORKING) {
            return;
        }
    }
}

void request_pump_all(void)
{
    for (http_request* r = all; r != NULL; r = r->next) {
        pump(r);
    }
}

http_request* http_request_start(const char* method, const char* url_text, const char* headers,
                                 const void* body, size_t len)
{
    url u;
    if (method == NULL || url_text == NULL || !url_parse(url_text, &u) || u.ws) {
        tgg_log("can't request \"%s\"", url_text != NULL ? url_text : "");
        return NULL;
    }
    conn* c = conn_open(&u);
    if (c == NULL) {
        http_request* r = calloc(1, sizeof *r);
        if (r != NULL) {
            r->state = HTTP_FAILED;
            snprintf(r->error, sizeof r->error, "%s", conn_open_error());
            r->next = all;
            all = r;
        }
        return r;
    }
    http_request* r = calloc(1, sizeof *r);
    if (r == NULL) {
        conn_close(c);
        return NULL;
    }
    r->c = c;
    char head[4096];
    const int n = snprintf(head, sizeof head,
                           "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: tgg.http/0.1\r\n"
                           "Accept-Encoding: identity\r\nConnection: close\r\n"
                           "Content-Length: %zu\r\n%s%s\r\n",
                           method, u.path, u.host, body != NULL ? len : (size_t) 0,
                           headers != NULL ? headers : "",
                           headers != NULL && headers[0] != '\0' ? "\r\n" : "");
    if (n < 0 || (size_t) n >= sizeof head || buf_add(&r->out, head, (size_t) n) != 0 ||
        (body != NULL && buf_add(&r->out, body, len) != 0))
    {
        conn_close(c);
        buf_free(&r->out);
        free(r);
        return NULL;
    }
    r->next = all;
    all = r;
    pump(r);
    return r;
}

int http_request_poll(http_request* r)
{
    if (r == NULL) {
        return HTTP_FAILED;
    }
    pump(r);
    return r->state;
}

int http_response_status(const http_request* r)
{
    return r != NULL && r->state == HTTP_DONE ? r->status : 0;
}

const char* http_response_body(const http_request* r, size_t* len)
{
    if (r == NULL || r->state != HTTP_DONE) {
        if (len != NULL) {
            *len = 0;
        }
        return NULL;
    }
    if (len != NULL) {
        *len = r->body.len;
    }
    return r->body.data != NULL ? r->body.data : "";
}

const char* http_request_error(const http_request* r)
{
    return r != NULL ? r->error : "";
}

void http_request_free(http_request* r)
{
    if (r == NULL) {
        return;
    }
    for (http_request** p = &all; *p != NULL; p = &(*p)->next) {
        if (*p == r) {
            *p = r->next;
            break;
        }
    }
    conn_close(r->c);
    buf_free(&r->out);
    buf_free(&r->in);
    buf_free(&r->body);
    free(r);
}
