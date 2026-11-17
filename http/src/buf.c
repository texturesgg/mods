/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A growing buffer (internal.h) */
#include "internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int buf_add(buf* b, const void* data, size_t len)
{
    if (len > SIZE_MAX / 2 - b->len) {
        return -1;
    }
    if (b->len + len + 1 > b->cap) {
        size_t cap = b->cap > 0 ? b->cap : 1024;
        while (cap < b->len + len + 1) {
            cap *= 2;
        }
        char* grown = realloc(b->data, cap);
        if (grown == NULL) {
            return -1;
        }
        b->data = grown;
        b->cap = cap;
    }
    if (len > 0) {
        memcpy(b->data + b->len, data, len);
    }
    b->len += len;
    b->data[b->len] = '\0';
    return 0;
}

void buf_drop(buf* b, size_t n)
{
    if (n >= b->len) {
        b->len = 0;
    } else {
        memmove(b->data, b->data + n, b->len - n);
        b->len -= n;
    }
    if (b->data != NULL) {
        b->data[b->len] = '\0';
    }
}

void buf_free(buf* b)
{
    free(b->data);
    memset(b, 0, sizeof *b);
}
