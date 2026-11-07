/* UDP with POSIX sockets: everything platform-specific about the network is
 * here, so Winsock replaces this file alone. */
#define _POSIX_C_SOURCE 200809L
#include "internal.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int sock = -1;
static struct sockaddr_in peer, last_from;
static int have_peer;
static char peer_name[64];

static void name_peer(void)
{
    char ip[INET_ADDRSTRLEN] = "";
    inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof ip);
    snprintf(peer_name, sizeof peer_name, "%s:%u", ip, (unsigned) ntohs(peer.sin_port));
}

const char* net_open(int port, const char* peer_spec)
{
    static char why[160];
    net_close();
    if (peer_spec != NULL) {
        char host[128];
        const char* colon = strrchr(peer_spec, ':');
        if (colon == NULL || colon == peer_spec || (size_t) (colon - peer_spec) >= sizeof host ||
            atoi(colon + 1) <= 0)
        {
            snprintf(why, sizeof why, "\"%s\" isn't an address and port", peer_spec);
            return why;
        }
        memcpy(host, peer_spec, (size_t) (colon - peer_spec));
        host[colon - peer_spec] = '\0';
        struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM };
        struct addrinfo* found = NULL;
        if (getaddrinfo(host, colon + 1, &hints, &found) != 0 || found == NULL) {
            snprintf(why, sizeof why, "can't find %s", host);
            return why;
        }
        memcpy(&peer, found->ai_addr, sizeof peer);
        freeaddrinfo(found);
        have_peer = 1;
        name_peer();
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        snprintf(why, sizeof why, "can't make a socket: %s", strerror(errno));
        have_peer = 0;
        return why;
    }
    struct sockaddr_in local = { .sin_family = AF_INET, .sin_port = htons((uint16_t) port),
                                 .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (bind(sock, (struct sockaddr*) &local, sizeof local) != 0) {
        snprintf(why, sizeof why, "can't use port %d: %s", port, strerror(errno));
        net_close();
        return why;
    }
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK);
    return NULL;
}

void net_close(void)
{
    if (sock >= 0) {
        close(sock);
    }
    sock = -1;
    have_peer = 0;
    peer_name[0] = '\0';
}

void net_send_now(const void* data, size_t len)
{
    if (sock >= 0 && have_peer) {
        (void) sendto(sock, data, len, 0, (struct sockaddr*) &peer, sizeof peer);
    }
}

size_t net_recv(void* buf, size_t cap)
{
    while (sock >= 0) {
        struct sockaddr_storage from;
        socklen_t from_len = sizeof from;
        const ssize_t n = recvfrom(sock, buf, cap, 0, (struct sockaddr*) &from, &from_len);
        if (n <= 0) {
            return 0;
        }
        const struct sockaddr_in* a = (const struct sockaddr_in*) &from;
        if (a->sin_family != AF_INET) {
            continue;
        }
        if (!have_peer) {
            last_from = *a;
            return (size_t) n;
        }
        if (a->sin_port == peer.sin_port && a->sin_addr.s_addr == peer.sin_addr.s_addr) {
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
        name_peer();
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
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec * 1000.0 + (double) ts.tv_nsec / 1e6;
}

void net_sleep_ms(int ms)
{
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (long) (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
