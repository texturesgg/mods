/* UDP to one peer with POSIX sockets: everything platform-specific about the
 * network is here, so Winsock replaces this file alone. */
#define _POSIX_C_SOURCE 200809L
#include "netplay.h"

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
static struct sockaddr_storage peer;
static socklen_t peer_len;

const char* net_open(int port, const char* peer_spec)
{
    static char why[160];
    char host[128];
    const char* colon = strrchr(peer_spec, ':');
    if (colon == NULL || colon == peer_spec || (size_t) (colon - peer_spec) >= sizeof host) {
        snprintf(why, sizeof why, "the peer \"%s\" isn't host:port", peer_spec);
        return why;
    }
    memcpy(host, peer_spec, (size_t) (colon - peer_spec));
    host[colon - peer_spec] = '\0';
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM };
    struct addrinfo* found = NULL;
    if (getaddrinfo(host, colon + 1, &hints, &found) != 0 || found == NULL) {
        snprintf(why, sizeof why, "can't find the peer \"%s\"", peer_spec);
        return why;
    }
    memcpy(&peer, found->ai_addr, found->ai_addrlen);
    peer_len = found->ai_addrlen;
    freeaddrinfo(found);

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        snprintf(why, sizeof why, "can't make a socket: %s", strerror(errno));
        return why;
    }
    struct sockaddr_in local = { .sin_family = AF_INET, .sin_port = htons((uint16_t) port),
                                 .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (bind(sock, (struct sockaddr*) &local, sizeof local) != 0) {
        snprintf(why, sizeof why, "can't use port %d: %s", port, strerror(errno));
        close(sock);
        sock = -1;
        return why;
    }
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK);
    return NULL;
}

void net_send_now(const void* data, size_t len)
{
    if (sock >= 0) {
        (void) sendto(sock, data, len, 0, (struct sockaddr*) &peer, peer_len);
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
        const struct sockaddr_in* b = (const struct sockaddr_in*) &peer;
        if (a->sin_port == b->sin_port && a->sin_addr.s_addr == b->sin_addr.s_addr) {
            return (size_t) n;
        }
    }
    return 0;
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
