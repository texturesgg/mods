/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A stand-in for tgg-melee under the fuzz drivers: the parts of the port's
 * API a mod's network and file code calls, with the network a queue the
 * driver fills and a clock it moves. */
#ifndef FUZZ_PORT_H
#define FUZZ_PORT_H

#include "tgg/net.h"

#include <stddef.h>
#include <stdint.h>

/* How many addresses a driver can send from: 0 is the joiner's host
 * (1.2.3.4:51000), 1-3 are other games, and the rest are what
 * tgg_net_resolve answered, in the order it was asked (a STUN or TURN
 * server, an HTTP host) */
#define FZ_ADDRS 12

/* Forgets every datagram, address and the clock */
void fz_reset(void);
/* The address `k` (mod FZ_ADDRS) */
tgg_net_addr fz_addr(int k);
/* Queues a datagram for tgg_net_recv, from the address `k` */
void fz_push(int k, const void* data, size_t len);
/* Moves the clock */
void fz_advance_ms(double ms);
/* Points `pads` at four controllers, all plugged in and at rest, as the
 * game hands pad_read */
struct tgg_pads;
void fz_pads(struct tgg_pads* pads);
/* The handler a mod registered for `event` (TGG_ON), or NULL */
const void* fz_event(const char* event);

/* The match: how many sides play (tgg_match_player), the frame running
 * (tgg_frame), and how far back tgg_rollback goes */
extern int fz_players;
extern int fz_frame;

/* A byte stream the drivers read their choices from */
typedef struct Fz {
    const uint8_t* p;
    size_t n;
} Fz;

static inline unsigned fz_byte(Fz* f)
{
    if (f->n == 0) {
        return 0;
    }
    f->n--;
    return *f->p++;
}

static inline unsigned fz_u16(Fz* f)
{
    const unsigned lo = fz_byte(f);
    return lo | fz_byte(f) << 8;
}

/* Up to `max` bytes: a length, then that many (fewer at the end) */
static inline size_t fz_bytes(Fz* f, const uint8_t** out, size_t max)
{
    size_t len = fz_u16(f) % (max + 1);
    if (len > f->n) {
        len = f->n;
    }
    *out = f->p;
    f->p += len;
    f->n -= len;
    return len;
}

#endif
