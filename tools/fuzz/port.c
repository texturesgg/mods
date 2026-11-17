/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The port's side of the API the fuzzed mods call (port.h). Compiled with
 * the SDK's headers, so each function here has the type a mod expects. */
#include "port.h"

#include "tgg/content.h"
#include "tgg/control.h"
#include "tgg/draw.h"
#include "tgg/game/events.h"
#include "tgg/match.h"
#include "tgg/mod.h"
#include "tgg/rollback.h"
#include "tgg/screen.h"
#include "tgg/session.h"

#include <dolphin/pad.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/random.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- The network */

#define QUEUE 64

typedef struct Datagram {
    int from;
    size_t len;
    uint8_t data[2048];
} Datagram;

static struct {
    Datagram q[QUEUE];
    int first, count;
    double now;
    char resolved[FZ_ADDRS][64];
    int n_resolved;
    int sockets;
} net;

int fz_players;
int fz_frame = -1;

static int rollback_max;
static int resim;

void fz_reset(void)
{
    memset(&net, 0, sizeof net);
    net.now = 1000;
    fz_players = 0;
    fz_frame = -1;
    rollback_max = 0;
    resim = 0;
}

tgg_net_addr fz_addr(int k)
{
    k = (int) ((unsigned) k % FZ_ADDRS);
    tgg_net_addr a;
    memset(&a, 0, sizeof a);
    a.family = TGG_NET_IPV4;
    if (k == 0) {
        a.ip[0] = 1, a.ip[1] = 2, a.ip[2] = 3, a.ip[3] = 4;
        a.port = 51000;
    } else if (k < 4) {
        a.ip[0] = 5, a.ip[1] = 6, a.ip[2] = 7, a.ip[3] = (uint8_t) k;
        a.port = (uint16_t) (51000 + k);
    } else {
        /* What tgg_net_resolve gave its (k - 4)th name */
        a.ip[0] = 10, a.ip[1] = 0, a.ip[2] = 0, a.ip[3] = (uint8_t) k;
        a.port = 3478;
    }
    return a;
}

void fz_push(int k, const void* data, size_t len)
{
    if (net.count == QUEUE || len > sizeof net.q[0].data) {
        return;
    }
    Datagram* d = &net.q[(net.first + net.count++) % QUEUE];
    d->from = k;
    d->len = len;
    memcpy(d->data, data, len);
}

void fz_advance_ms(double ms)
{
    net.now += ms;
}

int tgg_net_open_(const void* self, int port, tgg_socket** out)
{
    (void) self, (void) port;
    static int socket_;
    net.sockets++;
    *out = (tgg_socket*) &socket_;
    return 0;
}

void tgg_net_close(tgg_socket* sock)
{
    (void) sock;
}

int tgg_net_local_port(const tgg_socket* sock)
{
    (void) sock;
    return 51000;
}

int tgg_net_send(tgg_socket* sock, const tgg_net_addr* to, const void* data, size_t len)
{
    (void) sock, (void) to, (void) data;
    return (int) len;
}

int tgg_net_recv(tgg_socket* sock, void* buf, size_t cap, tgg_net_addr* from)
{
    (void) sock;
    if (net.count == 0) {
        return 0;
    }
    Datagram* d = &net.q[net.first];
    net.first = (net.first + 1) % QUEUE;
    net.count--;
    *from = fz_addr(d->from);
    if (d->len > cap) {
        return TGG_NET_ERR_TOO_BIG;
    }
    memcpy(buf, d->data, d->len);
    return (int) d->len;
}

int tgg_net_wait(tgg_socket* sock, int timeout_ms)
{
    (void) sock;
    /* The real one waits: the clock moves on */
    if (net.count == 0 && timeout_ms > 0) {
        net.now += timeout_ms;
    }
    return net.count > 0;
}

int tgg_net_resolve(const char* host, int default_port, tgg_net_addr* out)
{
    char name[64];
    snprintf(name, sizeof name, "%s", host);
    char* colon = strrchr(name, ':');
    int port = default_port;
    if (colon != NULL) {
        port = atoi(colon + 1);
        *colon = '\0';
    }
    unsigned a, b, c, d;
    if (sscanf(name, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        memset(out, 0, sizeof *out);
        out->family = TGG_NET_IPV4;
        out->ip[0] = (uint8_t) a, out->ip[1] = (uint8_t) b, out->ip[2] = (uint8_t) c,
        out->ip[3] = (uint8_t) d;
        out->port = (uint16_t) port;
        return 0;
    }
    int i = 0;
    while (i < net.n_resolved && strcmp(net.resolved[i], name) != 0) {
        i++;
    }
    if (i == net.n_resolved) {
        if (4 + i >= FZ_ADDRS) {
            return TGG_NET_ERR_NOT_FOUND;
        }
        snprintf(net.resolved[net.n_resolved++], sizeof net.resolved[0], "%s", name);
    }
    *out = fz_addr(4 + i);
    out->port = (uint16_t) port;
    return 0;
}

int tgg_net_addr_equal(const tgg_net_addr* a, const tgg_net_addr* b)
{
    /* Resolved names answer on any port: STUN asks on two */
    return a->family == b->family && memcmp(a->ip, b->ip, sizeof a->ip) == 0 &&
           (a->port == b->port || a->ip[0] == 10);
}

int tgg_net_addr_format(const tgg_net_addr* addr, char* buf, size_t size)
{
    return snprintf(buf, size, "%u.%u.%u.%u:%u", addr->ip[0], addr->ip[1], addr->ip[2],
                    addr->ip[3], addr->port);
}

const char* tgg_net_error(int code)
{
    (void) code;
    return "a fuzz error";
}

int tgg_net_interfaces(tgg_net_interface* out, int max)
{
    if (max < 1) {
        return 0;
    }
    memset(out, 0, sizeof *out);
    snprintf(out->name, sizeof out->name, "eth0");
    out->addr = fz_addr(1);
    out->addr.port = 0;
    out->prefix = 24;
    return 1;
}

/* Fixed, so the fuzzer can answer a STUN or TURN transaction */
int tgg_net_random(void* buf, size_t len)
{
    memset(buf, 0, len);
    return 0;
}

double tgg_net_time_ms(void)
{
    /* Time passes while the mod runs, as on a real clock: a loop that waits
     * on it ends */
    net.now += 0.05;
    return net.now;
}

/* ---- The mod set, the same on both sides */

const char* tgg_game_build(void)
{
    return "fuzz-build";
}

const char* tgg_mod_set_id(void)
{
    return "fuzz-set";
}

const char* tgg_mod_set_names(void)
{
    return "tgg.netplay";
}

const char* tgg_mod_set_versions(void)
{
    return "tgg.netplay 0.0.0";
}

const char* tgg_mod_set_all(void)
{
    return "tgg.netplay 0.0.0";
}

const char* tgg_mod_set_display(void)
{
    return "";
}

const char* tgg_mod_set_profile(void)
{
    return "{\"mods\":[]}";
}

uint64_t tgg_content_hash(void)
{
    return 0;
}

/* ---- The match and rollback */

int tgg_match_player(const tgg_match* match, int player)
{
    (void) match;
    return player < fz_players ? TGG_PLAYER_HUMAN : TGG_PLAYER_NONE;
}

int tgg_match_stage_kind(const tgg_match* match)
{
    (void) match;
    return 0;
}

int tgg_screen_play_match(const tgg_match* match)
{
    (void) match;
    fz_frame = 0;
    return 0;
}

void tgg_boot_match(const tgg_match* match)
{
    (void) match;
}

int tgg_end_match(void)
{
    return 0;
}

int tgg_frame(void)
{
    return fz_frame;
}

int tgg_resim(void)
{
    return resim;
}

int tgg_rollback_init(int frames)
{
    rollback_max = frames;
    return 0;
}

/* Runs pad_read again for each frame from `frame` up to the one running,
 * as the port's re-run does */
int tgg_rollback(int frame)
{
    if (frame < 0 || frame >= fz_frame || fz_frame - frame > rollback_max) {
        return -1;
    }
    tgg_event_pad_read_fn* pad_read = (tgg_event_pad_read_fn*) fz_event("pad_read");
    const int now = fz_frame;
    resim = 1;
    for (int f = frame; f < now && pad_read != NULL; f++) {
        fz_frame = f;
        tgg_pads pads;
        fz_pads(&pads);
        pad_read(&pads);
    }
    resim = 0;
    fz_frame = now;
    return 0;
}

void tgg_rollback_confirm(int frame)
{
    (void) frame;
}

int tgg_frame_checksum(int frame, uint64_t* sum)
{
    if (frame < 0 || frame >= fz_frame) {
        return -1;
    }
    *sum = (uint64_t) frame * 0x9E3779B97F4A7C15ull;
    return 0;
}

int tgg_frame_checksum_parts(int frame, tgg_frame_parts* out)
{
    memset(out, 0, sizeof *out);
    if (frame < 0 || frame >= fz_frame) {
        return -1;
    }
    for (int i = 0; i < TGG_FRAME_PARTS; i++) {
        out->part[i] = (uint64_t) frame + (uint64_t) i;
    }
    return 0;
}

void tgg_pad_process(HSD_PadStatus* status, const PADStatus* raw)
{
    (void) status, (void) raw;
}

void tgg_save_hold(int hold)
{
    (void) hold;
}

/* ---- Sessions, drawing and the log */

int tgg_session_players(void)
{
    return 1;
}

int tgg_session_provide(const tgg_session_provider* provider)
{
    (void) provider;
    return 0;
}

void tgg_session_match_offered(const tgg_match* match)
{
    (void) match;
}

void tgg_session_posted(int from, const void* data, int len)
{
    /* Touch every byte, so a bad length shows */
    volatile uint8_t sink = 0;
    for (int i = 0; i < len; i++) {
        sink ^= ((const uint8_t*) data)[i];
    }
    (void) from, (void) sink;
}

tgg_rect tgg_draw_bounds(void)
{
    return (tgg_rect) {0, 0, 640, 480};
}

void tgg_draw_rect(float x, float y, float w, float h, tgg_color color)
{
    (void) x, (void) y, (void) w, (void) h, (void) color;
}

float tgg_draw_text(float x, float y, float size, tgg_color color, int align, const char* text)
{
    (void) x, (void) y, (void) color, (void) align;
    return size * (float) strlen(text);
}

void tgg_log_(const void* self, const char* fmt, ...)
{
    (void) self;
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (getenv("FUZZ_LOG") != NULL) {
        fprintf(stderr, "log: %s\n", line);
    }
}

/* ---- The game's data a mod reads */

static PadLibData pad_lib;
PadLibData* tgg_gd_HSD_PadLibData = &pad_lib;
static u32 rand_seed;
static u32* rand_seed_ptr = &rand_seed;
u32** tgg_gd_HSD_RandSeedPtr = &rand_seed_ptr;

/* ---- The mod's records (TGG_ON), as the port reads them */

extern const tgg_decl __start_tggdecls[] __attribute__((weak));
extern const tgg_decl __stop_tggdecls[] __attribute__((weak));

const void* fz_event(const char* event)
{
    /* Records are 8-aligned and of more than one kind and size: walk them
     * by their magic */
    for (const uint8_t* p = (const uint8_t*) __start_tggdecls;
         p + sizeof(tgg_decl) <= (const uint8_t*) __stop_tggdecls; p += 8)
    {
        const tgg_decl* d = (const tgg_decl*) p;
        if (d->magic == TGG_DECL_MAGIC && d->kind == TGG_KIND_EVENT &&
            strcmp(d->symbol, event) == 0)
        {
            return d->fn;
        }
    }
    return NULL;
}

/* ---- Content and mods, by id: none loaded */

int tgg_character_kind(const char* id)
{
    (void) strlen(id);
    return -1;
}

int tgg_costume_pick(int player, const char* id)
{
    (void) player, (void) strlen(id);
    return -1;
}

int tgg_stage_kind(const char* id)
{
    (void) strlen(id);
    return -1;
}

const char* tgg_mod_loaded(const char* id)
{
    (void) strlen(id);
    return NULL;
}

void fz_pads(tgg_pads* pads)
{
    static HSD_PadStatus status[4];
    static PADStatus raw[4];
    memset(status, 0, sizeof status);
    memset(raw, 0, sizeof raw);
    for (int i = 0; i < 4; i++) {
        raw[i].err = PAD_ERR_NONE;
    }
    pads->status = status;
    pads->raw = raw;
}
