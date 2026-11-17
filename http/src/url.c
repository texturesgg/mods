/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Reading a URL (internal.h) */
#include "internal.h"

#include <stdlib.h>
#include <string.h>

int url_parse(const char* text, url* out)
{
    memset(out, 0, sizeof *out);
    const char* rest;
    if (strncmp(text, "https://", 8) == 0) {
        out->tls = 1, rest = text + 8;
    } else if (strncmp(text, "http://", 7) == 0) {
        rest = text + 7;
    } else if (strncmp(text, "wss://", 6) == 0) {
        out->tls = 1, out->ws = 1, rest = text + 6;
    } else if (strncmp(text, "ws://", 5) == 0) {
        out->ws = 1, rest = text + 5;
    } else {
        return 0;
    }
    out->port = out->tls ? 443 : 80;
    const char* end = rest + strcspn(rest, "/?#");
    const char* host = rest;
    const char* host_end = end;
    if (*host == '[') {
        const char* close = memchr(host, ']', (size_t) (end - host));
        if (close == NULL) {
            return 0;
        }
        host++;
        host_end = close;
        if (close + 1 < end) {
            if (close[1] != ':') {
                return 0;
            }
            out->port = atoi(close + 2);
        }
    } else {
        const char* colon = memchr(host, ':', (size_t) (end - host));
        if (colon != NULL) {
            host_end = colon;
            out->port = atoi(colon + 1);
        }
    }
    const size_t n = (size_t) (host_end - host);
    if (n == 0 || n >= sizeof out->host || out->port <= 0 || out->port > 65535) {
        return 0;
    }
    memcpy(out->host, host, n);
    const char* path = end;
    const size_t path_len = strcspn(path, "#");
    if (path_len + 2 > sizeof out->path) {
        return 0;
    }
    if (path_len == 0 || path[0] != '/') {
        out->path[0] = '/';
        memcpy(out->path + 1, path, path_len);
    } else {
        memcpy(out->path, path, path_len);
    }
    return 1;
}
