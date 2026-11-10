/* SPDX-License-Identifier: GPL-3.0-or-later */
/* tgg.http's parts: a connection, plain or TLS, to a URL's host (conn.c),
 * requests on one (request.c), and WebSockets on one (ws.c). */
#ifndef HTTP_INTERNAL_H
#define HTTP_INTERNAL_H

#include "http.h"

#include <stddef.h>
#include <stdint.h>

/* ---- A URL, as far as requests need it */

typedef struct url {
    int tls;         /* https, wss */
    int ws;          /* ws, wss */
    char host[256];  /* without brackets */
    int port;
    char path[2048]; /* from the first '/', with the query; "/" when there's none */
} url;

/* Reads `text`; 0 when it isn't an http(s) or ws(s) URL this mod takes. */
int url_parse(const char* text, url* out);

/* ---- conn.c: a connection to a URL's host, plain or TLS, that never waits */

typedef struct conn conn;

enum {
    CONN_WORKING = 0, /* looking up, connecting, shaking hands */
    CONN_READY = 1,   /* conn_write and conn_read work */
    CONN_FAILED = -1, /* conn_error says why */
};

/* Starts connecting; NULL when out of sockets or memory (conn_open_error says
 * why). Looks the host up first, which can take a moment. */
conn* conn_open(const url* u);
const char* conn_open_error(void);
/* Moves it forward: CONN_* */
int conn_step(conn* c);
/* Bytes taken (maybe 0), or -1 when the connection is gone */
int conn_write(conn* c, const void* data, size_t len);
/* Bytes read, 0 when none are waiting, -1 once the other end closed it (and
 * everything was read) or it broke */
int conn_read(conn* c, void* buf, size_t cap);
const char* conn_error(const conn* c);
void conn_close(conn* c);

/* ---- A growing buffer */

typedef struct buf {
    char* data;
    size_t len, cap;
} buf;

/* 0, or -1 out of memory */
int buf_add(buf* b, const void* data, size_t len);
/* Drops the first `n` bytes */
void buf_drop(buf* b, size_t n);
void buf_free(buf* b);

/* Sends what it can of `out` on `c`, dropping what was sent. -1 when the
 * connection is gone. */
int conn_flush(conn* c, buf* out);

/* ---- Each frame: requests and WebSockets move forward even unpolled */

void request_pump_all(void);
void ws_pump_all(void);

#endif
