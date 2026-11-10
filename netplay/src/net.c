/* The UDP socket and the peers, over the port's tgg/net.h: the same on
 * Linux, macOS and Windows. A joiner has one peer, the host; the host has one
 * per joiner.
 *
 * Through the meeting point the socket opens early (net_prepare), so STUN
 * (stun.c) can learn the address the router gives it, and the session then
 * starts on that same socket with the other games' candidate addresses
 * (net_punch): a joiner says hello to every one of the host's until one
 * answers, and the host says hello to every joiner's to open its own router
 * for them. */
#include "internal.h"

#include "tgg/net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static tgg_socket* sock;
static int prepared; /* opened by net_prepare: kept for the session after it */
static tgg_net_addr peers[NET_PEERS], last_from;
static char names[NET_PEERS][TGG_NET_ADDR_TEXT];
static int n_peers, have_last, last_peer = -1;
static int relayed[NET_PEERS]; /* the host: that peer reaches this game through the relay */
static int last_via_relay;

/* Punching: the other games' candidate addresses, and what each is */
static tgg_net_addr candidates[NET_CANDIDATES];
static char candidate_kinds[NET_CANDIDATES][12];
static int n_candidates;
static int punching; /* a joiner: the host is whichever candidate answers */
static double punch_started_ms, punched_ms = -1;
static char punched_kind[12];
static int heard_any;   /* any datagram arrived while punching, from anywhere */

static const char* open_socket(int port)
{
    static char why[160];
    const int err = tgg_net_open(port, &sock);
    if (err != TGG_NET_OK) {
        snprintf(why, sizeof why, "can't use port %d: %s", port, tgg_net_error(err));
        return why;
    }
    return NULL;
}

/* TGG_NETPLAY_NO_DIRECT=1, for testing the relay on one network: every datagram
 * from another game that didn't come through a relay is dropped */
static int no_direct(void)
{
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("TGG_NETPLAY_NO_DIRECT");
        v = e != NULL && e[0] == '1';
    }
    return v;
}

const char* net_prepare(int port)
{
    if (sock != NULL && prepared) {
        return NULL;
    }
    net_close();
    const char* failed = open_socket(port);
    if (failed == NULL) {
        prepared = 1;
        stun_start(sock);
    }
    return failed;
}

const char* net_open(int port, const char* peer_spec)
{
    static char why[160];
    const int keep = sock != NULL && prepared;
    if (!keep) {
        net_close();
    }
    n_peers = 0;
    have_last = 0;
    last_peer = -1;
    n_candidates = 0;
    punching = 0;
    punched_ms = -1;
    memset(relayed, 0, sizeof relayed);
    last_via_relay = 0;
    punched_kind[0] = '\0';
    if (peer_spec != NULL) {
        const char* colon = strrchr(peer_spec, ':');
        const int err = tgg_net_resolve(peer_spec, 0, &peers[0]);
        if (colon == NULL || err == TGG_NET_ERR_ARG ||
            (err == TGG_NET_OK && peers[0].port == 0)) {
            snprintf(why, sizeof why, "\"%s\" isn't an address and port", peer_spec);
            return why;
        }
        if (err != TGG_NET_OK) {
            snprintf(why, sizeof why, "can't find %.*s", (int) (colon - peer_spec), peer_spec);
            return why;
        }
        n_peers = 1;
        tgg_net_addr_format(&peers[0], names[0], sizeof names[0]);
    }
    if (!keep) {
        const char* failed = open_socket(port);
        if (failed != NULL) {
            net_close();
            return failed;
        }
    }
    return NULL;
}

int net_punch(int joining, const char* const* addrs, const char* const* kinds, int n)
{
    n_candidates = 0;
    for (int i = 0; i < n && n_candidates < NET_CANDIDATES; i++) {
        tgg_net_addr a;
        if (addrs[i] != NULL && tgg_net_resolve(addrs[i], 0, &a) == TGG_NET_OK && a.port != 0) {
            candidates[n_candidates] = a;
            snprintf(candidate_kinds[n_candidates], sizeof candidate_kinds[0], "%s",
                     kinds != NULL && kinds[i] != NULL ? kinds[i] : "");
            n_candidates++;
        }
    }
    heard_any = 0;
    punch_started_ms = net_now_ms();
    if (joining && n_candidates > 0) {
        punching = 1;
        n_peers = 1;
        peers[0] = candidates[0];
        snprintf(names[0], sizeof names[0], "the host");
    }
    return n_candidates;
}

/* The candidate an address is, or -1 */
static int candidate_of(const tgg_net_addr* a)
{
    for (int i = 0; i < n_candidates; i++) {
        if (tgg_net_addr_equal(a, &candidates[i])) {
            return i;
        }
    }
    return -1;
}

static void punched(int candidate)
{
    if (punched_ms < 0 && candidate >= 0) {
        punched_ms = net_now_ms() - punch_started_ms;
        snprintf(punched_kind, sizeof punched_kind, "%s", candidate_kinds[candidate]);
        char text[TGG_NET_ADDR_TEXT];
        tgg_net_addr_format(&candidates[candidate], text, sizeof text);
        tgg_log("reached %s (%s) after %.0f ms", text, punched_kind, punched_ms);
    }
}

const char* net_punched(double* ms)
{
    if (ms != NULL) {
        *ms = punched_ms;
    }
    return punched_ms >= 0 ? punched_kind : NULL;
}

double net_punch_age_ms(void)
{
    return n_candidates > 0 ? net_now_ms() - punch_started_ms : -1;
}

void net_punch_send(const void* data, size_t len)
{
    if (sock == NULL || net_punch_age_ms() > PUNCH_MS) {
        return;
    }
    for (int i = 0; i < n_candidates; i++) {
        int taken = 0;
        for (int p = 0; p < n_peers && !punching; p++) {
            taken |= tgg_net_addr_equal(&candidates[i], &peers[p]);
        }
        if (!taken) {
            (void) tgg_net_send(sock, &candidates[i], data, len);
            turn_send(&candidates[i], data, len); /* nothing until a relay is up */
        }
    }
}

void net_close(void)
{
    stun_stop();
    turn_stop();
    memset(relayed, 0, sizeof relayed);
    last_via_relay = 0;
    tgg_net_close(sock);
    sock = NULL;
    prepared = 0;
    n_peers = 0;
    have_last = 0;
    last_peer = -1;
    n_candidates = 0;
    punching = 0;
}

void net_send_now(int peer, const void* data, size_t len)
{
    if (sock == NULL || peer < 0 || peer >= n_peers) {
        return;
    }
    if (punching && peer == 0) {
        if (net_punch_age_ms() <= PUNCH_MS) {
            for (int i = 0; i < n_candidates; i++) {
                (void) tgg_net_send(sock, &candidates[i], data, len);
            }
        }
        return;
    }
    if (relayed[peer]) {
        turn_send(&peers[peer], data, len);
        return;
    }
    (void) tgg_net_send(sock, &peers[peer], data, len);
}

void net_reply(const void* data, size_t len)
{
    if (sock != NULL && have_last && last_via_relay) {
        turn_send(&last_from, data, len);
    } else if (sock != NULL && have_last) {
        (void) tgg_net_send(sock, &last_from, data, len);
    }
}

const char* net_last_name(void)
{
    static char name[TGG_NET_ADDR_TEXT];
    name[0] = '\0';
    if (have_last) {
        tgg_net_addr_format(&last_from, name, sizeof name);
    }
    return name;
}

int net_last_peer(void)
{
    return last_peer;
}

size_t net_recv(void* buf, size_t cap)
{
    while (sock != NULL) {
        tgg_net_addr from;
        const int n = tgg_net_recv(sock, buf, cap, &from);
        if (n == TGG_NET_ERR_TOO_BIG) {
            continue; /* not one of ours */
        }
        if (n <= 0) {
            stun_pump();
            turn_pump();
            return 0;
        }
        int len = n, via = 0;
        if (turn_is_server(&from)) {
            len = turn_got(buf, (size_t) n, &from);
            if (len <= 0) {
                continue; /* the relay's own answers */
            }
            via = 1;
        } else if (stun_got(buf, (size_t) n)) {
            continue;
        }
        if (!via && no_direct()) {
            const int c = candidate_of(&from);
            if (c < 0 || strcmp(candidate_kinds[c], "relay") != 0) {
                continue; /* TGG_NETPLAY_NO_DIRECT: only the relay gets through */
            }
        }
        heard_any = 1;
        last_via_relay = via;
        last_from = from;
        have_last = 1;
        last_peer = -1;
        if (punching) {
            const int c = candidate_of(&from);
            if (c >= 0) {
                /* The host answered at this one: it's the host from now on */
                punching = 0;
                peers[0] = from;
                tgg_net_addr_format(&peers[0], names[0], sizeof names[0]);
                punched(c);
            }
        }
        for (int i = 0; i < n_peers; i++) {
            if (!punching && tgg_net_addr_equal(&from, &peers[i])) {
                last_peer = i;
            }
        }
        return (size_t) len;
    }
    return 0;
}

int net_adopt(void)
{
    if (!have_last) {
        return -1;
    }
    if (last_peer >= 0) {
        return last_peer;
    }
    if (n_peers == NET_PEERS) {
        return -1;
    }
    peers[n_peers] = last_from;
    relayed[n_peers] = last_via_relay;
    tgg_net_addr_format(&peers[n_peers], names[n_peers], sizeof names[n_peers]);
    last_peer = n_peers++;
    punched(candidate_of(&last_from));
    if (last_via_relay && punched_ms >= 0) {
        snprintf(punched_kind, sizeof punched_kind, "relay");
    }
    return last_peer;
}

int net_peers(void)
{
    return n_peers;
}

const char* net_peer_name(int peer)
{
    return peer >= 0 && peer < n_peers ? names[peer] : "";
}

double net_now_ms(void)
{
    return tgg_net_time_ms();
}

void net_wait_ms(int ms)
{
    if (sock != NULL) {
        (void) tgg_net_wait(sock, ms);
    }
}

int net_local_port(void)
{
    return sock != NULL ? tgg_net_local_port(sock) : -1;
}

int net_take_host(void)
{
    if (!punching || !have_last) {
        return -1;
    }
    /* The host's hello came from an address it didn't list (its router gave
     * this socket another port for this game): it's the host all the same */
    punching = 0;
    peers[0] = last_from;
    tgg_net_addr_format(&peers[0], names[0], sizeof names[0]);
    if (punched_ms < 0) {
        punched_ms = net_now_ms() - punch_started_ms;
        snprintf(punched_kind, sizeof punched_kind, "other");
        tgg_log("reached the host at %s, an address it didn't list, after %.0f ms", names[0],
                punched_ms);
    }
    last_peer = 0;
    return 0;
}

void net_punch_report(void)
{
    char tried[NET_CANDIDATES * 80] = "";
    size_t used = 0;
    for (int i = 0; i < n_candidates && used < sizeof tried; i++) {
        char text[TGG_NET_ADDR_TEXT];
        tgg_net_addr_format(&candidates[i], text, sizeof text);
        used += (size_t) snprintf(tried + used, sizeof tried - used, "%s%s (%s)",
                                  i > 0 ? ", " : "", text, candidate_kinds[i]);
    }
    char mapped[TGG_NET_ADDR_TEXT] = "";
    const int stun = stun_mapped(mapped, sizeof mapped);
    tgg_log("gave up after %.0f s: said hello to %s; %s. This game's internet address: %s",
            net_punch_age_ms() / 1000, tried[0] != '\0' ? tried : "nothing",
            heard_any ? "packets came back, but none from the other game"
                      : "nothing came back from anywhere",
            stun == 1 ? mapped : "unknown (STUN didn't answer)");
}

int net_relay(const char* server, const char* user, const char* pass)
{
    if (sock == NULL || turn_start(sock, server, user, pass) != 0) {
        return -1;
    }
    if (no_direct()) {
        tgg_net_addr loopback = {.family = TGG_NET_IPV4, .ip = {127, 0, 0, 1}};
        turn_permit(&loopback, 1); /* the test's TURN server is on this machine */
    }
    for (int i = 0; i < n_candidates; i++) {
        turn_permit(&candidates[i], 0);
    }
    punch_started_ms = net_now_ms(); /* another PUNCH_MS, through the relay */
    return 0;
}

void net_add_candidates(const char* const* addrs, const char* const* kinds, int n)
{
    for (int i = 0; i < n && n_candidates < NET_CANDIDATES; i++) {
        tgg_net_addr a;
        if (addrs[i] == NULL || tgg_net_resolve(addrs[i], 0, &a) != TGG_NET_OK || a.port == 0 ||
            candidate_of(&a) >= 0)
        {
            continue;
        }
        candidates[n_candidates] = a;
        snprintf(candidate_kinds[n_candidates], sizeof candidate_kinds[0], "%s",
                 kinds != NULL && kinds[i] != NULL ? kinds[i] : "");
        turn_permit(&a, 0);
        tgg_log("  the other game also at %s (%s)", addrs[i], candidate_kinds[n_candidates]);
        n_candidates++;
    }
    punch_started_ms = net_now_ms();
}
