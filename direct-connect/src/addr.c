/* This machine's IPv4 addresses, for the host to tell the other player: the
 * tailnet's first, then the local network's. From the port's tgg/net.h. */
#include "dc.h"

#include "tgg/net.h"

#include <stdio.h>
#include <string.h>

/* A Tailscale address: in 100.64.0.0/10, alone on its interface (a /32), or
 * on an interface named for Tailscale. Some ISPs hand out 100.64.0.0/10
 * addresses on the local network too, with a wider mask. */
static int tailnet(const tgg_net_interface* i)
{
    const uint8_t* ip = i->addr.ip;
    if (ip[0] != 100 || (ip[1] & 0xC0) != 64) {
        return 0;
    }
    static const char name[] = "tailscale";
    int named = 1;
    for (size_t k = 0; k < sizeof name - 1; k++) {
        const char c = i->name[k];
        named &= (c >= 'A' && c <= 'Z' ? c + 32 : c) == name[k];
    }
    return named || i->prefix == 32;
}

int local_addresses(char out[][48], int max)
{
    tgg_net_interface list[64];
    int count = tgg_net_interfaces(list, 64);
    if (count < 0) {
        return 0;
    }
    if (count > 64) {
        count = 64;
    }
    int n = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < count && n < max; i++) {
            if (list[i].addr.family != TGG_NET_IPV4 || (list[i].flags & TGG_NET_IF_LOOPBACK) ||
                list[i].addr.ip[0] == 127 || tailnet(&list[i]) != (pass == 0))
            {
                continue;
            }
            char ip[48]; /* an IPv4 address fits */
            tgg_net_addr_format(&list[i].addr, ip, sizeof ip);
            int dup = 0;
            for (int k = 0; k < n; k++) {
                dup |= strcmp(out[k], ip) == 0;
            }
            if (!dup) {
                snprintf(out[n++], 48, "%s", ip);
            }
        }
    }
    return n;
}

const char* local_name(void)
{
    return tgg_net_host_name();
}
