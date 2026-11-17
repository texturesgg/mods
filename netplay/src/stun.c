/* SPDX-License-Identifier: GPL-3.0-or-later */
/* STUN (RFC 8489): binding requests from netplay's own socket to Cloudflare's
 * STUN server, whose answer is the address and port the router gives that
 * socket on the internet. The other game punches through to it.
 *
 * The same request also goes to the server's other port (53). When both
 * answers have the same public port, the router keeps one mapping for the
 * socket whoever it sends to ("easy"), and punching works. When they differ,
 * it makes a new mapping for each destination ("hard"), and punching only
 * works if the other game's router is easy. No second answer: "unknown".
 * Each is asked again every 250 ms until it answers, for up to two seconds. */
#include "internal.h"

#include "tgg/net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STUN_HOST "stun.cloudflare.com"
#define STUN_COOKIE 0x2112A442u
#define RESEND_MS 250.0
#define GIVE_UP_MS 2000.0

/* The main port, whose answer is the candidate, and the other one */
static const uint16_t PORTS[2] = {3478, 53};

typedef struct Ask {
    tgg_net_addr server;
    uint8_t txn[12];
    double sent_ms;
    int state; /* 0 asking, 1 answered, -1 gave up or off */
    tgg_net_addr mapped;
} Ask;

static struct {
    tgg_socket* sock;
    double started_ms;
    Ask ask[2];
} st = {.ask = {{.state = -1}, {.state = -1}}};

static void send_request(Ask* a)
{
    uint8_t req[20] = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42};
    memcpy(req + 8, a->txn, sizeof a->txn);
    (void) tgg_net_send(st.sock, &a->server, req, sizeof req);
    a->sent_ms = net_now_ms();
}

void stun_start(tgg_socket* sock)
{
    memset(&st, 0, sizeof st);
    st.ask[0].state = st.ask[1].state = -1;
    if (getenv("TGG_NETPLAY_NO_STUN") != NULL) {
        return;
    }
    st.sock = sock;
    st.started_ms = net_now_ms();
    for (int i = 0; i < 2; i++) {
        Ask* a = &st.ask[i];
        if (tgg_net_resolve(STUN_HOST, PORTS[i], &a->server) != TGG_NET_OK ||
            tgg_net_random(a->txn, sizeof a->txn) != TGG_NET_OK)
        {
            tgg_log("can't ask %s:%d for this game's address", STUN_HOST, PORTS[i]);
            continue;
        }
        a->server.port = PORTS[i];
        a->state = 0;
        send_request(a);
    }
}

void stun_stop(void)
{
    st.sock = NULL;
    for (int i = 0; i < 2; i++) {
        if (st.ask[i].state == 0) {
            st.ask[i].state = -1;
        }
    }
}

void stun_pump(void)
{
    if (st.sock == NULL) {
        return;
    }
    const double now = net_now_ms();
    for (int i = 0; i < 2; i++) {
        Ask* a = &st.ask[i];
        if (a->state != 0) {
            continue;
        }
        if (now - st.started_ms > GIVE_UP_MS) {
            a->state = -1;
            if (i == 0) {
                tgg_log("%s:%d didn't answer, so this game's internet address is unknown",
                        STUN_HOST, PORTS[i]);
            }
        } else if (now - a->sent_ms > RESEND_MS) {
            send_request(a);
        }
    }
}

int stun_got(const void* data, size_t len)
{
    const uint8_t* p = data;
    if (len < 20 || (p[0] & 0xC0) != 0 || p[4] != 0x21 || p[5] != 0x12 || p[6] != 0xA4 ||
        p[7] != 0x42)
    {
        return 0; /* not STUN */
    }
    Ask* a = NULL;
    for (int i = 0; i < 2; i++) {
        if (st.ask[i].state == 0 && memcmp(p + 8, st.ask[i].txn, 12) == 0) {
            a = &st.ask[i];
        }
    }
    if (a == NULL || p[0] != 0x01 || p[1] != 0x01) {
        return 1; /* STUN, but not an answer we wait for */
    }
    const size_t body = (size_t) p[2] << 8 | p[3];
    const size_t end = 20 + body < len ? 20 + body : len;
    for (size_t at = 20; at + 4 <= end;) {
        const unsigned type = (unsigned) p[at] << 8 | p[at + 1];
        const size_t alen = (size_t) p[at + 2] << 8 | p[at + 3];
        const uint8_t* v = p + at + 4;
        if (at + 4 + alen > end) {
            break;
        }
        /* XOR-MAPPED-ADDRESS */
        if (type == 0x0020 && alen >= 8) {
            tgg_net_addr m;
            memset(&m, 0, sizeof m);
            m.port = (uint16_t) (((unsigned) v[2] << 8 | v[3]) ^ (STUN_COOKIE >> 16));
            if (v[1] == 0x01) {
                m.family = TGG_NET_IPV4;
                for (int i = 0; i < 4; i++) {
                    m.ip[i] = v[4 + i] ^ p[4 + i];
                }
            } else if (v[1] == 0x02 && alen >= 20) {
                m.family = TGG_NET_IPV6;
                for (int i = 0; i < 16; i++) {
                    m.ip[i] = v[4 + i] ^ p[4 + i]; /* the cookie, then the transaction */
                }
            } else {
                break;
            }
            a->mapped = m;
            a->state = 1;
            char text[TGG_NET_ADDR_TEXT];
            tgg_net_addr_format(&m, text, sizeof text);
            tgg_log("%s:%d sees this game at %s", STUN_HOST, a->server.port, text);
            if (st.ask[0].state == 1 && st.ask[1].state == 1) {
                tgg_log("this game's router is %s", stun_nat());
            }
            return 1;
        }
        at += 4 + ((alen + 3) & ~(size_t) 3);
    }
    return 1;
}

int stun_mapped(char* text, size_t cap)
{
    if (st.ask[0].state == 1) {
        tgg_net_addr_format(&st.ask[0].mapped, text, cap);
    }
    return st.ask[0].state;
}

int stun_settled(void)
{
    return st.ask[0].state != 0 && st.ask[1].state != 0;
}

const char* stun_nat(void)
{
    if (st.ask[0].state != 1 || st.ask[1].state != 1) {
        return "unknown";
    }
    return st.ask[0].mapped.port == st.ask[1].mapped.port ? "easy" : "hard";
}
