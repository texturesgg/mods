/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A relay (TURN, RFC 8656) for when two games can't punch through to each
 * other: the host asks a TURN server (Cloudflare's, credentials from the
 * meeting point) for an address of its own on the server, and the other
 * games send to that address like any other candidate. The server passes
 * their packets to the host inside Data indications, and the host's go back
 * out in Send indications. All of it on netplay's one socket.
 *
 * Only the host allocates. Its allocation lives 10 minutes and is refreshed
 * every 4; permissions for the other games' addresses every 4 minutes too. */
#include "internal.h"

#include "tgg/net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COOKIE 0x2112A442u
#define RESEND_MS 500.0
#define GIVE_UP_MS 6000.0
#define REFRESH_MS 240000.0
#define LIFETIME_S 600
#define MAX_PERMITS 16

enum {
    T_ALLOCATE = 0x0003,
    T_REFRESH = 0x0004,
    T_SEND = 0x0016,
    T_DATA = 0x0017,
    T_PERMISSION = 0x0008,
    A_USERNAME = 0x0006,
    A_INTEGRITY = 0x0008,
    A_ERROR = 0x0009,
    A_LIFETIME = 0x000D,
    A_PEER = 0x0012,
    A_DATA = 0x0013,
    A_REALM = 0x0014,
    A_NONCE = 0x0015,
    A_RELAYED = 0x0016,
    A_TRANSPORT = 0x0019,
};

enum { R_OFF, R_ALLOCATING, R_READY, R_FAILED };

static struct {
    int state;
    tgg_socket* sock;
    tgg_net_addr server;
    char user[256], pass[256], realm[128], nonce[256];
    uint8_t key[16]; /* MD5(user:realm:pass) */
    int have_key;
    uint8_t txn[12]; /* the request waiting for an answer */
    int waiting;     /* its type, 0 for none */
    double sent_ms, started_ms, refreshed_ms;
    tgg_net_addr relayed;
    tgg_net_addr permits[MAX_PERMITS];
    int permit_state[MAX_PERMITS]; /* 0 to ask, 1 granted, -1 refused */
    double permit_ms[MAX_PERMITS]; /* when it was granted */
    int n_permits;
    int asking;                    /* the permit asked for now */
    double bytes;
} t;

/* ---- SHA-1, HMAC-SHA1 and MD5, as STUN's message integrity needs them ------- */

static uint32_t rol(uint32_t x, int n)
{
    return x << n | x >> (32 - n);
}

static void sha1_block(uint32_t h[5], const uint8_t* p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t) p[4 * i] << 24 | (uint32_t) p[4 * i + 1] << 16 |
               (uint32_t) p[4 * i + 2] << 8 | p[4 * i + 3];
    }
    for (int i = 16; i < 80; i++) {
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | (~b & d), k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d, k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d, k = 0xCA62C1D6;
        }
        const uint32_t tmp = rol(a, 5) + f + e + k + w[i];
        e = d, d = c, c = rol(b, 30), b = a, a = tmp;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
}

/* SHA-1 of `a` then `b` */
static void sha1_2(const uint8_t* a, size_t na, const uint8_t* b, size_t nb, uint8_t out[20])
{
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint8_t block[64];
    size_t used = 0;
    const uint64_t bits = (uint64_t) (na + nb) * 8;
    for (int part = 0; part < 2; part++) {
        const uint8_t* p = part == 0 ? a : b;
        size_t n = part == 0 ? na : nb;
        while (n > 0) {
            const size_t take = n < 64 - used ? n : 64 - used;
            memcpy(block + used, p, take);
            used += take, p += take, n -= take;
            if (used == 64) {
                sha1_block(h, block);
                used = 0;
            }
        }
    }
    block[used++] = 0x80;
    if (used > 56) {
        memset(block + used, 0, 64 - used);
        sha1_block(h, block);
        used = 0;
    }
    memset(block + used, 0, 56 - used);
    for (int i = 0; i < 8; i++) {
        block[56 + i] = (uint8_t) (bits >> (56 - 8 * i));
    }
    sha1_block(h, block);
    for (int i = 0; i < 5; i++) {
        out[4 * i] = (uint8_t) (h[i] >> 24), out[4 * i + 1] = (uint8_t) (h[i] >> 16);
        out[4 * i + 2] = (uint8_t) (h[i] >> 8), out[4 * i + 3] = (uint8_t) h[i];
    }
}

static void hmac_sha1(const uint8_t* key, size_t nkey, const uint8_t* msg, size_t n,
                      uint8_t out[20])
{
    uint8_t k[64] = {0}, ipad[64], opad[64], inner[20];
    if (nkey > 64) {
        sha1_2(key, nkey, NULL, 0, k);
    } else {
        memcpy(k, key, nkey);
    }
    for (int i = 0; i < 64; i++) {
        ipad[i] = k[i] ^ 0x36, opad[i] = k[i] ^ 0x5C;
    }
    sha1_2(ipad, 64, msg, n, inner);
    sha1_2(opad, 64, inner, 20, out);
}

static void md5(const uint8_t* msg, size_t n, uint8_t out[16])
{
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613,
        0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193,
        0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d,
        0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
        0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122,
        0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
        0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244,
        0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb,
        0xeb86d391};
    static const int S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                              5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    const size_t total = ((n + 8) / 64 + 1) * 64;
    uint8_t* m = calloc(total, 1);
    if (m == NULL) {
        memset(out, 0, 16);
        return;
    }
    memcpy(m, msg, n);
    m[n] = 0x80;
    const uint64_t bits = (uint64_t) n * 8;
    for (int i = 0; i < 8; i++) {
        m[total - 8 + i] = (uint8_t) (bits >> (8 * i));
    }
    for (size_t off = 0; off < total; off += 64) {
        uint32_t w[16];
        for (int i = 0; i < 16; i++) {
            const uint8_t* p = m + off + 4 * i;
            w[i] = p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; i++) {
            uint32_t f;
            int g;
            if (i < 16) {
                f = (b & c) | (~b & d), g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c), g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d, g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | ~d), g = (7 * i) % 16;
            }
            const uint32_t tmp = d;
            d = c, c = b;
            b = b + rol(a + f + K[i] + w[g], S[i]);
            a = tmp;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d;
    }
    free(m);
    for (int i = 0; i < 4; i++) {
        out[4 * i] = (uint8_t) h[i], out[4 * i + 1] = (uint8_t) (h[i] >> 8);
        out[4 * i + 2] = (uint8_t) (h[i] >> 16), out[4 * i + 3] = (uint8_t) (h[i] >> 24);
    }
}

/* ---- Messages -------------------------------------------------------------------- */

typedef struct Msg {
    uint8_t b[1500];
    size_t n;
} Msg;

static void begin(Msg* m, int type, const uint8_t txn[12])
{
    m->b[0] = (uint8_t) (type >> 8), m->b[1] = (uint8_t) type;
    m->b[2] = m->b[3] = 0;
    m->b[4] = 0x21, m->b[5] = 0x12, m->b[6] = 0xA4, m->b[7] = 0x42;
    memcpy(m->b + 8, txn, 12);
    m->n = 20;
}

static void attr(Msg* m, int type, const void* v, size_t len)
{
    if (m->n + 4 + len + 3 > sizeof m->b) {
        return;
    }
    m->b[m->n] = (uint8_t) (type >> 8), m->b[m->n + 1] = (uint8_t) type;
    m->b[m->n + 2] = (uint8_t) (len >> 8), m->b[m->n + 3] = (uint8_t) len;
    memcpy(m->b + m->n + 4, v, len);
    m->n += 4 + len;
    while (m->n % 4 != 0) {
        m->b[m->n++] = 0;
    }
    const size_t body = m->n - 20;
    m->b[2] = (uint8_t) (body >> 8), m->b[3] = (uint8_t) body;
}

static void xor_addr(Msg* m, int type, const tgg_net_addr* a)
{
    uint8_t v[20] = {0};
    const uint16_t port = a->port ^ (uint16_t) (COOKIE >> 16);
    v[1] = a->family == TGG_NET_IPV6 ? 0x02 : 0x01;
    v[2] = (uint8_t) (port >> 8), v[3] = (uint8_t) port;
    const int n = a->family == TGG_NET_IPV6 ? 16 : 4;
    for (int i = 0; i < n; i++) {
        v[4 + i] = a->ip[i] ^ m->b[4 + i]; /* the cookie, then the transaction */
    }
    attr(m, type, v, (size_t) (4 + n));
}

static int read_xor_addr(const uint8_t* msg, const uint8_t* v, size_t len, tgg_net_addr* out)
{
    if (len < 8) {
        return 0;
    }
    memset(out, 0, sizeof *out);
    out->port = (uint16_t) (((unsigned) v[2] << 8 | v[3]) ^ (COOKIE >> 16));
    const int n = v[1] == 0x02 ? 16 : 4;
    if ((size_t) (4 + n) > len) {
        return 0;
    }
    out->family = v[1] == 0x02 ? TGG_NET_IPV6 : TGG_NET_IPV4;
    for (int i = 0; i < n; i++) {
        out->ip[i] = v[4 + i] ^ msg[4 + i];
    }
    return 1;
}

/* Signs with MESSAGE-INTEGRITY (long-term credentials) */
static void sign(Msg* m)
{
    if (!t.have_key) {
        return;
    }
    attr(m, A_USERNAME, t.user, strlen(t.user));
    attr(m, A_REALM, t.realm, strlen(t.realm));
    attr(m, A_NONCE, t.nonce, strlen(t.nonce));
    /* The length counts the integrity attribute as if it were there */
    const size_t body = m->n - 20 + 24;
    m->b[2] = (uint8_t) (body >> 8), m->b[3] = (uint8_t) body;
    uint8_t mac[20];
    hmac_sha1(t.key, 16, m->b, m->n, mac);
    attr(m, A_INTEGRITY, mac, 20);
}

static void send_msg(const Msg* m)
{
    (void) tgg_net_send(t.sock, &t.server, m->b, m->n);
}

static void request(int type)
{
    Msg m;
    if (tgg_net_random(t.txn, sizeof t.txn) != TGG_NET_OK) {
        return;
    }
    begin(&m, type, t.txn);
    if (type == T_ALLOCATE) {
        const uint8_t udp[4] = {17, 0, 0, 0};
        attr(&m, A_TRANSPORT, udp, 4);
    }
    if (type == T_ALLOCATE || type == T_REFRESH) {
        const uint8_t life[4] = {0, 0, (uint8_t) (LIFETIME_S >> 8), (uint8_t) LIFETIME_S};
        const uint8_t zero[4] = {0};
        attr(&m, A_LIFETIME, t.state == R_OFF ? zero : life, 4);
    }
    if (type == T_PERMISSION) {
        /* One address a request: a server refuses the whole request for any one
         * address it won't relay to */
        xor_addr(&m, A_PEER, &t.permits[t.asking]);
    }
    sign(&m);
    send_msg(&m);
    t.waiting = type;
    t.sent_ms = net_now_ms();
}

/* ---- The API for net.c ------------------------------------------------------------ */

int turn_start(tgg_socket* sock, const char* server, const char* user, const char* pass)
{
    memset(&t, 0, sizeof t);
    if (sock == NULL || tgg_net_resolve(server, 3478, &t.server) != TGG_NET_OK) {
        tgg_log("relay: can't find %s", server);
        t.state = R_FAILED;
        return -1;
    }
    t.sock = sock;
    snprintf(t.user, sizeof t.user, "%s", user);
    snprintf(t.pass, sizeof t.pass, "%s", pass);
    t.state = R_ALLOCATING;
    t.started_ms = net_now_ms();
    request(T_ALLOCATE);
    char text[TGG_NET_ADDR_TEXT];
    tgg_net_addr_format(&t.server, text, sizeof text);
    tgg_log("relay: asking %s for an address", text);
    return 0;
}

void turn_stop(void)
{
    if (t.state == R_READY && t.sock != NULL) {
        const int was = t.state;
        t.state = R_OFF; /* a lifetime of 0 frees the allocation */
        request(T_REFRESH);
        t.state = was;
    }
    if (t.bytes > 0) {
        tgg_log("relay: %.0f bytes went through it", t.bytes);
    }
    t.state = R_OFF;
    t.sock = NULL;
}

/* Whether a TURN server relays to `a`: not private, shared (100.64/10), loopback,
 * link-local or multicast (Cloudflare refuses those) */
static int relayable(const tgg_net_addr* a)
{
    const uint8_t* ip = a->ip;
    if (a->family == TGG_NET_IPV4) {
        return !(ip[0] == 0 || ip[0] == 10 || ip[0] == 127 || ip[0] >= 224 ||
                 (ip[0] == 100 && (ip[1] & 0xC0) == 64) || (ip[0] == 169 && ip[1] == 254) ||
                 (ip[0] == 172 && (ip[1] & 0xF0) == 16) || (ip[0] == 192 && ip[1] == 168));
    }
    return a->family == TGG_NET_IPV6 && (ip[0] & 0xE0) == 0x20;
}

void turn_permit(const tgg_net_addr* peer, int any)
{
    if (peer == NULL || peer->family == 0 || (!any && !relayable(peer))) {
        return;
    }
    for (int i = 0; i < t.n_permits; i++) {
        if (memcmp(t.permits[i].ip, peer->ip, 16) == 0 && t.permits[i].family == peer->family) {
            return;
        }
    }
    if (t.n_permits < MAX_PERMITS) {
        t.permits[t.n_permits] = *peer;
        t.permits[t.n_permits].port = 0;
        t.permit_state[t.n_permits] = 0;
        t.n_permits++;
    }
}

void turn_pump(void)
{
    if (t.sock == NULL || t.state == R_OFF || t.state == R_FAILED) {
        return;
    }
    const double now = net_now_ms();
    if (t.state == R_ALLOCATING) {
        if (now - t.started_ms > GIVE_UP_MS) {
            t.state = R_FAILED;
            tgg_log("relay: the server didn't give an address");
        } else if (now - t.sent_ms > RESEND_MS) {
            request(T_ALLOCATE);
        }
        return;
    }
    if (t.waiting != 0 && now - t.sent_ms > RESEND_MS && now - t.sent_ms < GIVE_UP_MS) {
        request(t.waiting); /* no answer yet: again */
    } else if (t.waiting == 0 && now - t.refreshed_ms > REFRESH_MS) {
        t.refreshed_ms = now;
        request(T_REFRESH);
    } else if (t.waiting == 0) {
        for (int i = 0; i < t.n_permits; i++) {
            if (t.permit_state[i] == 0 || (t.permit_state[i] == 1 && now - t.permit_ms[i] > REFRESH_MS)) {
                t.asking = i;
                request(T_PERMISSION);
                break;
            }
        }
    }
}

int turn_is_server(const tgg_net_addr* from)
{
    return t.sock != NULL && t.state != R_OFF && tgg_net_addr_equal(from, &t.server);
}

/* An answer to the request waiting */
static void answer(const uint8_t* p, size_t len, int type)
{
    const int cls = (type & 0x0110);
    const int method = type & ~0x0110;
    if (method != t.waiting || memcmp(p + 8, t.txn, 12) != 0) {
        return;
    }
    int code = 0;
    tgg_net_addr relayed = {0};
    for (size_t at = 20; at + 4 <= len;) {
        const unsigned a = (unsigned) p[at] << 8 | p[at + 1];
        const size_t alen = (size_t) p[at + 2] << 8 | p[at + 3];
        const uint8_t* v = p + at + 4;
        if (at + 4 + alen > len) {
            break;
        }
        if (a == A_ERROR && alen >= 4) {
            code = (v[2] & 7) * 100 + v[3];
        } else if (a == A_REALM && alen < sizeof t.realm) {
            memcpy(t.realm, v, alen);
            t.realm[alen] = '\0';
        } else if (a == A_NONCE && alen < sizeof t.nonce) {
            memcpy(t.nonce, v, alen);
            t.nonce[alen] = '\0';
        } else if (a == A_RELAYED) {
            read_xor_addr(p, v, alen, &relayed);
        }
        at += 4 + ((alen + 3) & ~(size_t) 3);
    }
    t.waiting = 0;
    if (cls == 0x0110) { /* an error */
        if ((code == 401 || code == 438) && t.realm[0] != '\0' && t.nonce[0] != '\0') {
            char joined[700];
            snprintf(joined, sizeof joined, "%s:%s:%s", t.user, t.realm, t.pass);
            md5((const uint8_t*) joined, strlen(joined), t.key);
            t.have_key = 1;
            request(method); /* again, signed */
            return;
        }
        if (method == T_PERMISSION) {
            char text[TGG_NET_ADDR_TEXT];
            tgg_net_addr_format(&t.permits[t.asking], text, sizeof text);
            tgg_log("relay: the server won't relay for %s (%d)", text, code);
            t.permit_state[t.asking] = -1;
            return;
        }
        tgg_log("relay: the server refused (%d)", code);
        if (t.state == R_ALLOCATING) {
            t.state = R_FAILED;
        }
        return;
    }
    if (method == T_PERMISSION) {
        if (t.permit_state[t.asking] != 1) {
            char text[TGG_NET_ADDR_TEXT];
            tgg_net_addr_format(&t.permits[t.asking], text, sizeof text);
            tgg_log("relay: %s may send through it", text);
        }
        t.permit_state[t.asking] = 1;
        t.permit_ms[t.asking] = net_now_ms();
        return;
    }
    if (method == T_ALLOCATE && relayed.family != 0) {
        t.relayed = relayed;
        t.state = R_READY;
        t.refreshed_ms = net_now_ms();
        char text[TGG_NET_ADDR_TEXT];
        tgg_net_addr_format(&relayed, text, sizeof text);
        tgg_log("relay: this game is also at %s", text);
    }
}

int turn_got(uint8_t* data, size_t len, tgg_net_addr* peer)
{
    if (len < 20 || data[4] != 0x21 || data[5] != 0x12 || data[6] != 0xA4 || data[7] != 0x42) {
        return 0;
    }
    const int type = data[0] << 8 | data[1];
    const size_t body = (size_t) data[2] << 8 | data[3];
    const size_t end = 20 + body < len ? 20 + body : len;
    if (type != T_DATA) {
        answer(data, end, type);
        return 0;
    }
    const uint8_t* payload = NULL;
    size_t plen = 0;
    int have_peer = 0;
    for (size_t at = 20; at + 4 <= end;) {
        const unsigned a = (unsigned) data[at] << 8 | data[at + 1];
        const size_t alen = (size_t) data[at + 2] << 8 | data[at + 3];
        if (at + 4 + alen > end) {
            break;
        }
        if (a == A_PEER) {
            have_peer = read_xor_addr(data, data + at + 4, alen, peer);
        } else if (a == A_DATA) {
            payload = data + at + 4;
            plen = alen;
        }
        at += 4 + ((alen + 3) & ~(size_t) 3);
    }
    if (payload == NULL || !have_peer) {
        return 0;
    }
    memmove(data, payload, plen);
    t.bytes += (double) len;
    return (int) plen;
}

void turn_send(const tgg_net_addr* peer, const void* data, size_t len)
{
    if (t.state != R_READY || len + 64 > sizeof ((Msg*) 0)->b) {
        return;
    }
    Msg m;
    uint8_t txn[12];
    if (tgg_net_random(txn, sizeof txn) != TGG_NET_OK) {
        return;
    }
    begin(&m, T_SEND, txn);
    xor_addr(&m, A_PEER, peer);
    attr(&m, A_DATA, data, len);
    send_msg(&m);
    t.bytes += (double) m.n;
}

int turn_relayed(char* text, size_t cap)
{
    if (t.state == R_READY) {
        tgg_net_addr_format(&t.relayed, text, cap);
        return 1;
    }
    return t.state == R_ALLOCATING ? 0 : -1;
}

double turn_bytes(void)
{
    return t.bytes;
}
