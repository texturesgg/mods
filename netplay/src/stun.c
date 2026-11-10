/* STUN (RFC 8489): one binding request from netplay's own socket to
 * Cloudflare's STUN server, whose answer is the address and port the router
 * gives that socket on the internet. The other game punches through to it.
 * Asked again every 250 ms until it answers, for up to two seconds. */
#include "internal.h"

#include "tgg/net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STUN_SERVER "stun.cloudflare.com:3478"
#define STUN_COOKIE 0x2112A442u
#define RESEND_MS 250.0
#define GIVE_UP_MS 2000.0

static struct {
    tgg_socket* sock;
    tgg_net_addr server;
    uint8_t txn[12];
    double started_ms, sent_ms;
    int state; /* 0 asking, 1 answered, -1 gave up or off */
    tgg_net_addr mapped;
} st = {.state = -1};

static void send_request(void)
{
    uint8_t req[20] = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42};
    memcpy(req + 8, st.txn, sizeof st.txn);
    (void) tgg_net_send(st.sock, &st.server, req, sizeof req);
    st.sent_ms = net_now_ms();
}

void stun_start(tgg_socket* sock)
{
    memset(&st, 0, sizeof st);
    st.state = -1;
    if (getenv("TGG_NETPLAY_NO_STUN") != NULL) {
        return;
    }
    if (tgg_net_resolve(STUN_SERVER, 3478, &st.server) != TGG_NET_OK ||
        tgg_net_random(st.txn, sizeof st.txn) != TGG_NET_OK)
    {
        tgg_log("can't ask %s for this game's address", STUN_SERVER);
        return;
    }
    st.sock = sock;
    st.state = 0;
    st.started_ms = net_now_ms();
    send_request();
}

void stun_stop(void)
{
    st.sock = NULL;
    if (st.state == 0) {
        st.state = -1;
    }
}

void stun_pump(void)
{
    if (st.state != 0 || st.sock == NULL) {
        return;
    }
    const double now = net_now_ms();
    if (now - st.started_ms > GIVE_UP_MS) {
        st.state = -1;
        tgg_log("%s didn't answer: this game's internet address is unknown", STUN_SERVER);
    } else if (now - st.sent_ms > RESEND_MS) {
        send_request();
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
    if (st.state != 0 || p[0] != 0x01 || p[1] != 0x01 || memcmp(p + 8, st.txn, 12) != 0) {
        return 1; /* STUN, but not the answer we wait for */
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
            tgg_net_addr a;
            memset(&a, 0, sizeof a);
            a.port = (uint16_t) (((unsigned) v[2] << 8 | v[3]) ^ (STUN_COOKIE >> 16));
            if (v[1] == 0x01) {
                a.family = TGG_NET_IPV4;
                for (int i = 0; i < 4; i++) {
                    a.ip[i] = v[4 + i] ^ p[4 + i];
                }
            } else if (v[1] == 0x02 && alen >= 20) {
                a.family = TGG_NET_IPV6;
                for (int i = 0; i < 16; i++) {
                    a.ip[i] = v[4 + i] ^ p[4 + i]; /* the cookie, then the transaction */
                }
            } else {
                break;
            }
            st.mapped = a;
            st.state = 1;
            char text[TGG_NET_ADDR_TEXT];
            tgg_net_addr_format(&a, text, sizeof text);
            tgg_log("%s sees this game at %s", STUN_SERVER, text);
            return 1;
        }
        at += 4 + ((alen + 3) & ~(size_t) 3);
    }
    return 1;
}

int stun_mapped(char* text, size_t cap)
{
    if (st.state == 1) {
        tgg_net_addr_format(&st.mapped, text, cap);
    }
    return st.state;
}
