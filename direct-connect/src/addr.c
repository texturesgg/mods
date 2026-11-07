/* This machine's IPv4 addresses, for the host to tell the other player: the
 * tailnet's first, then the local network's. The one file with platform
 * calls. */
#define _DEFAULT_SOURCE
#include "dc.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* A Tailscale address: in 100.64.0.0/10, alone on its interface (a /32).
 * Some ISPs hand out 100.64.0.0/10 addresses on the local network too, with
 * a wider mask. */
static int tailnet(const struct ifaddrs* i, uint32_t a)
{
    if ((a & 0xFFC00000u) != 0x64400000u) {
        return 0;
    }
    if (i->ifa_name != NULL && strncmp(i->ifa_name, "tailscale", 9) == 0) {
        return 1;
    }
    return i->ifa_netmask != NULL &&
           ((const struct sockaddr_in*) i->ifa_netmask)->sin_addr.s_addr == 0xFFFFFFFFu;
}

int local_addresses(char out[][48], int max)
{
    struct ifaddrs* list = NULL;
    if (getifaddrs(&list) != 0) {
        return 0;
    }
    int n = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (struct ifaddrs* i = list; i != NULL && n < max; i = i->ifa_next) {
            if (i->ifa_addr == NULL || i->ifa_addr->sa_family != AF_INET) {
                continue;
            }
            const struct in_addr in = ((const struct sockaddr_in*) i->ifa_addr)->sin_addr;
            const uint32_t a = ntohl(in.s_addr);
            if ((a >> 24) == 127 || tailnet(i, a) != (pass == 0)) {
                continue;
            }
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &in, ip, sizeof ip);
            int dup = 0;
            for (int k = 0; k < n; k++) {
                dup |= strcmp(out[k], ip) == 0;
            }
            if (!dup) {
                snprintf(out[n++], 48, "%s", ip);
            }
        }
    }
    freeifaddrs(list);
    return n;
}

const char* local_name(void)
{
    static char name[64];
    if (gethostname(name, sizeof name) != 0) {
        name[0] = '\0';
    }
    name[sizeof name - 1] = '\0';
    char* dot = strchr(name, '.');
    if (dot != NULL) {
        *dot = '\0';
    }
    return name;
}
