/* SPDX-License-Identifier: GPL-3.0-or-later */
/* tgg.http's API for other mods: HTTPS requests and WebSocket connections,
 * with TLS checked against the system's root certificates.
 *
 *     http_request* r = http_request_start("GET", "https://example.com/api", NULL, NULL, 0);
 *     ...each frame:
 *     if (http_request_poll(r) != HTTP_WORKING) {
 *         size_t len;
 *         const char* body = http_response_body(r, &len);
 *         ...http_response_status(r), or http_request_error(r) when it failed
 *         http_request_free(r);
 *     }
 *
 *     http_ws* ws = http_ws_open("wss://example.com/live", NULL);
 *     ...each frame:
 *     char msg[4096];
 *     int n;
 *     while ((n = http_ws_receive(ws, msg, sizeof msg)) > 0) { ...one whole message }
 *     if (http_ws_state(ws) == HTTP_WS_OPEN) http_ws_send(ws, "hello", 5);
 *
 * Nothing here waits for the network: everything moves forward a little each
 * frame and whenever you call in. The one exception is looking up a host's
 * name when a connection starts, which can take a moment.
 *
 * URLs are https:// and wss://. Plain http:// and ws:// only reach addresses
 * on the local network (a router, another machine at home), never the
 * internet.
 *
 * Everything is presentation code: call it from a screen's functions or a
 * PRESENT event, never from simulation code.
 *
 * A mod that uses it lists it in its manifest's "depends":
 *
 *     "depends": { "tgg.http": "^0.1" }
 *
 * and includes this header (copy it, or add tgg.http's include/ to the
 * include path). */
#ifndef HTTP_H_API
#define HTTP_H_API

#include "tgg/mod.h"

#include <stddef.h>

#define HTTP TGG_PROVIDER(tgg_http, "tgg.http", 1)

/* ---- Requests ------------------------------------------------------------------ */

typedef struct http_request http_request;

enum {
    HTTP_WORKING = 0, /* still going */
    HTTP_DONE = 1,    /* a response came: http_response_status and _body */
    HTTP_FAILED = -1, /* no response: http_request_error says why */
};

/* The most a response body may be; a longer one fails the request. */
#define HTTP_MAX_RESPONSE (8u << 20)

/* Starts a request. `method` is "GET", "POST" and so on; `headers` is more
 * header lines ("Authorization: Bearer x\r\nContent-Type: application/json"),
 * or NULL; `body` is `len` bytes to send, or NULL. NULL when the URL can't
 * be used or the mod has too many connections open. */
TGG_API(HTTP, http_request*, http_request_start,
        (const char* method, const char* url, const char* headers, const void* body,
         size_t len));

/* Moves it forward: HTTP_WORKING, HTTP_DONE or HTTP_FAILED. */
TGG_API(HTTP, int, http_request_poll, (http_request* r));

/* HTTP_DONE: the status (200, 404, ...). */
TGG_API(HTTP, int, http_response_status, (const http_request* r));

/* HTTP_DONE: the body, with a '\0' after it, and its length in `*len` (may be
 * NULL). Valid until http_request_free. */
TGG_API(HTTP, const char*, http_response_body, (const http_request* r, size_t* len));

/* HTTP_FAILED: why, for the player ("Couldn't reach example.com"). */
TGG_API(HTTP, const char*, http_request_error, (const http_request* r));

/* Stops it if it's still going, and frees it. NULL does nothing. */
TGG_API(HTTP, void, http_request_free, (http_request* r));

/* ---- WebSockets ---------------------------------------------------------------- */

typedef struct http_ws http_ws;

enum {
    HTTP_WS_CONNECTING = 0,
    HTTP_WS_OPEN = 1,
    HTTP_WS_CLOSED = -1, /* closed, or never opened: http_ws_error says why */
};

/* The most one received message may be; a longer one closes the connection. */
#define HTTP_WS_MAX_MESSAGE (1u << 20)

/* Opens a WebSocket to a wss:// URL, with more header lines as for requests.
 * NULL when the URL can't be used or the mod has too many connections open. */
TGG_API(HTTP, http_ws*, http_ws_open, (const char* url, const char* headers));

/* HTTP_WS_CONNECTING, HTTP_WS_OPEN or HTTP_WS_CLOSED. */
TGG_API(HTTP, int, http_ws_state, (http_ws* ws));

/* Sends one text message once open. 0, or -1 when it isn't open. Messages
 * sent before it opens are refused, not kept. */
TGG_API(HTTP, int, http_ws_send, (http_ws* ws, const char* text, size_t len));

/* The next whole message that arrived: copies it to `buf` with a '\0' after
 * it and returns its length; 0 when none is waiting; -1 when `cap` is too
 * small for it (it stays waiting: http_ws_peek says how big it is). */
TGG_API(HTTP, int, http_ws_receive, (http_ws* ws, char* buf, size_t cap));

/* The length of the next message waiting, or 0. */
TGG_API(HTTP, size_t, http_ws_peek, (http_ws* ws));

/* HTTP_WS_CLOSED: why, for the player. */
TGG_API(HTTP, const char*, http_ws_error, (http_ws* ws));

/* Closes it (telling the other end) and frees it. NULL does nothing. */
TGG_API(HTTP, void, http_ws_close, (http_ws* ws));

#endif
