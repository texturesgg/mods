/* The UDP socket and the peer, over the port's tgg/net.h: the same on
 * Linux, macOS and Windows. */
#include "internal.h"

#include "tgg/net.h"

#include <stdio.h>
#include <string.h>

static tgg_socket* sock;
static tgg_net_addr peer, last_from;
static int have_peer;
static char peer_name[TGG_NET_ADDR_TEXT];

const char* net_open(int port, const char* peer_spec)
{
    static char why[160];
    net_close();
    if (peer_spec != NULL) {
        const char* colon = strrchr(peer_spec, ':');
        const int err = tgg_net_resolve(peer_spec, 0, &peer);
        if (colon == NULL || err == TGG_NET_ERR_ARG || (err == TGG_NET_OK && peer.port == 0)) {
            snprintf(why, sizeof why, "\"%s\" isn't an address and port", peer_spec);
            return why;
        }
        if (err != TGG_NET_OK) {
            snprintf(why, sizeof why, "can't find %.*s", (int) (colon - peer_spec), peer_spec);
            return why;
        }
        have_peer = 1;
        tgg_net_addr_format(&peer, peer_name, sizeof peer_name);
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
    have_peer = 0;
    peer_name[0] = '\0';
}

void net_send_now(const void* data, size_t len)
{
    if (sock != NULL && have_peer) {
        (void) tgg_net_send(sock, &peer, data, len);
    }
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
        if (!have_peer) {
            last_from = from;
            return (size_t) n;
        }
        if (tgg_net_addr_equal(&from, &peer)) {
            return (size_t) n;
        }
    }
    return 0;
}

void net_adopt(void)
{
    if (!have_peer) {
        peer = last_from;
        have_peer = 1;
        tgg_net_addr_format(&peer, peer_name, sizeof peer_name);
    }
}

int net_has_peer(void)
{
    return have_peer;
}

const char* net_peer_name(void)
{
    return peer_name;
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
