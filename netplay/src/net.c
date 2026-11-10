/* The UDP socket and the peers, over the port's tgg/net.h: the same on
 * Linux, macOS and Windows. A joiner has one peer, the host; the host has one
 * per joiner. */
#include "internal.h"

#include "tgg/net.h"

#include <stdio.h>
#include <string.h>

static tgg_socket* sock;
static tgg_net_addr peers[NET_PEERS], last_from;
static char names[NET_PEERS][TGG_NET_ADDR_TEXT];
static int n_peers, have_last, last_peer = -1;

const char* net_open(int port, const char* peer_spec)
{
    static char why[160];
    net_close();
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

    const int err = tgg_net_open(port, &sock);
    if (err != TGG_NET_OK) {
        snprintf(why, sizeof why, "can't use port %d: %s", port, tgg_net_error(err));
        net_close();
        return why;
    }
    return NULL;
}

void net_close(void)
{
    tgg_net_close(sock);
    sock = NULL;
    n_peers = 0;
    have_last = 0;
    last_peer = -1;
}

void net_send_now(int peer, const void* data, size_t len)
{
    if (sock != NULL && peer >= 0 && peer < n_peers) {
        (void) tgg_net_send(sock, &peers[peer], data, len);
    }
}

void net_reply(const void* data, size_t len)
{
    if (sock != NULL && have_last) {
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
            return 0;
        }
        last_from = from;
        have_last = 1;
        last_peer = -1;
        for (int i = 0; i < n_peers; i++) {
            if (tgg_net_addr_equal(&from, &peers[i])) {
                last_peer = i;
            }
        }
        return (size_t) n;
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
    tgg_net_addr_format(&peers[n_peers], names[n_peers], sizeof names[n_peers]);
    last_peer = n_peers++;
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
