/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Fuzzes tgg.netplay from the network: each input opens a session (host or
 * joiner, direct or through the meeting point), then plays a script of
 * datagrams from other games and servers, frames, match frames and API
 * calls through the mod's own code, with the port faked (port.c).
 *
 * Datagrams come raw, or as packets wire_encode makes from fuzz bytes with
 * the session's token and the same build and mods, so the fuzzer gets past
 * the hello and into lobbies, profiles and matches. */
#include "../../netplay/src/internal.h"
#include "netplay.h"
#include "port.h"

#include "tgg/gen/events.h"
#include "tgg/game/events.h"
#include "tgg/match.h"

#include <melee/pl/player.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The longest script: enough for a hello, a match and some frames */
#define STEPS 256
/* Match frames one step runs at most */
#define FRAMES 8

static uint8_t encoded[WIRE_MAX_BYTES];

static void push_packet(int from, const WirePacket* p)
{
    const size_t n = wire_encode(p, encoded);
    if (n <= sizeof encoded) {
        fz_push(from, encoded, n);
    }
}

static void fill(Fz* f, void* out, size_t n)
{
    uint8_t* o = out;
    for (size_t i = 0; i < n; i++) {
        o[i] = (uint8_t) fz_byte(f);
    }
}

static void hello(Fz* f, int from)
{
    WirePacket p;
    memset(&p, 0, sizeof p);
    p.type = WIRE_HELLO;
    WireHello* h = &p.u.hello;
    const unsigned flags = fz_byte(f);
    snprintf(h->build, sizeof h->build, "%s", tgg_game_build());
    snprintf(h->mod_set, sizeof h->mod_set, "%s", tgg_mod_set_id());
    snprintf(h->versions, sizeof h->versions, "%s", tgg_mod_set_versions());
    snprintf(h->target, sizeof h->target, "%s", flags & 1 ? "fuzz-target" : TGG_TARGET);
    snprintf(h->all, sizeof h->all, "%s", tgg_mod_set_all());
    if (flags & 2) {
        /* Different mods: the mismatch screen and its profile */
        snprintf(h->mod_set, sizeof h->mod_set, "other-set");
    }
    if (flags & 4) {
        const uint8_t* text;
        const size_t n = fz_bytes(f, &text, sizeof h->display - 1);
        memcpy(h->display, text, n);
    }
    h->host = (uint8_t) (flags >> 3 & 1);
    h->heard = (uint8_t) (flags >> 4 & 1);
    h->strict = (uint8_t) (flags >> 5 & 1);
    h->assign = (uint8_t) fz_byte(f);
    h->seed = fz_byte(f);
    h->game = fz_byte(f);
    h->token = flags & 0x40 ? (uint64_t) fz_byte(f) : ss.token;
    push_packet(from, &p);
}

static void inputs(Fz* f, int from)
{
    WirePacket p;
    memset(&p, 0, sizeof p);
    p.type = WIRE_INPUT;
    WireInputs* in = &p.u.inputs;
    in->number = fz_byte(f) % 4;
    in->frame = (int8_t) fz_byte(f) + fz_frame;
    in->match = 0;
    in->sum_frame = (int8_t) fz_byte(f) + fz_frame;
    in->sum = fz_byte(f);
    in->ping = fz_u16(f);
    in->pong = fz_u16(f);
    in->pong_age = (uint16_t) fz_u16(f);
    for (int s = 0; s < SIDES; s++) {
        in->ack[s] = (int8_t) fz_byte(f) + fz_frame;
    }
    in->runs = (uint8_t) (fz_byte(f) % (WIRE_MAX_RUNS + 1));
    int total = 0;
    for (int r = 0; r < in->runs; r++) {
        in->run[r].side = (uint8_t) (fz_byte(f) % (SIDES + 1));
        in->run[r].first = (int8_t) fz_byte(f) + fz_frame;
        in->run[r].count = (uint8_t) (fz_byte(f) % (WIRE_MAX_INPUTS - total + 1));
        total += in->run[r].count;
    }
    for (int i = 0; i < total; i++) {
        fill(f, &in->inputs[i], sizeof in->inputs[i]);
    }
    in->blame.frame = -1;
    if (fz_byte(f) & 1) {
        in->blame.frame = (int8_t) fz_byte(f) + fz_frame;
        fill(f, in->blame.part, sizeof in->blame.part);
        in->blame.mods = (uint8_t) (fz_byte(f) % (WIRE_BLAME_MODS + 1));
        fill(f, in->blame.mod_id, sizeof in->blame.mod_id);
        fill(f, in->blame.mod_sum, sizeof in->blame.mod_sum);
    }
    push_packet(from, &p);
}

static void lobby(Fz* f, int from)
{
    WirePacket p;
    memset(&p, 0, sizeof p);
    p.type = WIRE_LOBBY;
    WireLobby* l = &p.u.lobby;
    l->ping = fz_u16(f);
    l->pong = fz_u16(f);
    l->msg_ack = fz_byte(f);
    l->number = fz_byte(f) % 4;
    for (int s = 0; s < SIDES; s++) {
        l->input_ack[s] = (int8_t) fz_byte(f);
    }
    l->count = (uint8_t) (fz_byte(f) % 4);
    size_t room = WIRE_MSGS_BYTES;
    for (int i = 0; i < l->count; i++) {
        WireMsg* m = &l->msgs[i];
        m->seq = fz_byte(f);
        m->kind = (uint8_t) (fz_byte(f) % 9);
        m->from = (uint8_t) (fz_byte(f) % (SIDES + 1));
        const uint8_t* data;
        const size_t max = room < WIRE_MSG_MAX ? room : WIRE_MSG_MAX;
        m->len = (uint16_t) fz_bytes(f, &data, max);
        memcpy(m->data, data, m->len);
        room -= m->len < room ? m->len : room;
    }
    push_packet(from, &p);
}

static void profile(Fz* f, int from)
{
    WirePacket p;
    memset(&p, 0, sizeof p);
    if (fz_byte(f) & 1) {
        p.type = WIRE_PROFILE_ASK;
        p.u.profile_ask = fz_u16(f);
    } else {
        p.type = WIRE_PROFILE;
        WireProfile* pr = &p.u.profile;
        pr->total = fz_u16(f);
        pr->offset = fz_u16(f);
        const uint8_t* data;
        pr->len = (uint16_t) fz_bytes(f, &data, WIRE_PROFILE_CHUNK);
        memcpy(pr->data, data, pr->len);
    }
    push_packet(from, &p);
}

static void call(const char* event)
{
    void (*fn)(void) = (void (*)(void)) fz_event(event);
    if (fn != NULL) {
        fn();
    }
}

static tgg_match match_data;

/* Runs the match's frames as the port would: advance, then (unless it
 * holds) the frame's pad_read */
static void match_frames(int n)
{
    tgg_event_advance_fn* advance = (tgg_event_advance_fn*) fz_event("advance");
    tgg_event_pad_read_fn* pad_read = (tgg_event_pad_read_fn*) fz_event("pad_read");
    for (int i = 0; i < n && fz_frame >= 0 && ss.state == NETPLAY_PLAYING; i++) {
        tgg_advance adv;
        memset(&adv, 0, sizeof adv);
        adv.frame = fz_frame;
        advance(&adv);
        if (adv.hold) {
            fz_advance_ms(16);
            continue;
        }
        tgg_pads pads;
        fz_pads(&pads);
        pad_read(&pads);
        fz_frame++;
        fz_advance_ms(16);
    }
}

static void api(Fz* f)
{
    uint8_t buf[512];
    const unsigned which = fz_byte(f) % 9;
    if (which == 0) {
        netplay_lock();
    } else if (which == 1) {
        const uint8_t* data;
        const size_t n = fz_bytes(f, &data, 500);
        netplay_send(data, (int) n);
    } else if (which == 2) {
        int from;
        while (netplay_receive(buf, (int) (fz_byte(f) % sizeof buf), &from) > 0) {
        }
    } else if (which == 3) {
        memset(&match_data, 0, sizeof match_data);
        fz_players = ss.players;
        netplay_play(&match_data);
    } else if (which == 4) {
        tgg_event_match_setup_fn* setup = (tgg_event_match_setup_fn*) fz_event("match_setup");
        setup(&match_data);
    } else if (which == 5) {
        call("match_end");
        fz_frame = -1;
    } else if (which == 8) {
        /* The match the session would start once its games are in, as
         * MSG_MATCH and MSG_READY do: straight into match frames */
        if (ss.state != NETPLAY_PLAYING && ss.link[0].greeted) {
            if (ss.players < 2) {
                ss.players = 2;
            }
            fz_players = ss.players;
            memset(&match_data, 0, sizeof match_data);
            if (match_begin(1 + fz_byte(f) % 3, fz_byte(f), &match_data) == 0) {
                tgg_event_match_setup_fn* setup =
                    (tgg_event_match_setup_fn*) fz_event("match_setup");
                setup(&match_data);
            }
        }
    } else if (which == 6) {
        const char* why;
        netplay_relay("turn.example:3478", "user", "pass", &why);
    } else {
        netplay_info info;
        netplay_status(&info);
        char text[64];
        netplay_relay_address(text, (int) sizeof text);
        netplay_host_profile();
        for (int s = 0; s < SIDES; s++) {
            netplay_rtt(s);
        }
        call("hud_draw");
    }
}

static void open_session(Fz* f)
{
    const unsigned mode = fz_byte(f);
    const int side = mode & 1;
    const char* why;
    if (mode & 2) {
        const char* addrs[] = {"1.2.3.4:51000", "5.6.7.1:51001", "5.6.7.2:51002"};
        const char* kinds[] = {"stun", "local", "relay"};
        netplay_prepare(0, &why);
        netplay_open_meet(side, addrs, kinds, 1 + (int) (mode >> 2 & 1) * 2, "1f2e3d4c", &why);
    } else {
        netplay_open(side, side != 0 ? "1.2.3.4:51000" : NULL, 0, &why);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    Fz f = {data, size};
    fz_reset();
    netplay_close();
    open_session(&f);
    for (int step = 0; step < STEPS && f.n > 0; step++) {
        const unsigned op = fz_byte(&f);
        const int from = (int) (op >> 4);
        switch (op & 7) {
        case 0: {
            const uint8_t* raw;
            const size_t n = fz_bytes(&f, &raw, WIRE_MAX_BYTES + 16);
            fz_push(from, raw, n);
            break;
        }
        case 1:
            hello(&f, from);
            break;
        case 2:
            inputs(&f, from);
            break;
        case 3:
            lobby(&f, from);
            break;
        case 4:
            profile(&f, from);
            break;
        case 5:
            fz_advance_ms(fz_byte(&f) * 8.0);
            call("frame");
            break;
        case 6:
            match_frames(1 + (int) (fz_byte(&f) % FRAMES));
            break;
        default:
            api(&f);
            break;
        }
    }
    call("frame");
    match_frames(FRAMES);
    netplay_close();
    return 0;
}

/* ---- What the driver leaves out: the save's unlocks (unlocks.c) and the
 * replay test (replaytest.c), which read the game and files, not the network */

void unlocks_follow(int in_session)
{
    (void) in_session;
}

int replaytest_open(const char* path)
{
    (void) path;
    return -1;
}

int replaytest_port(int side)
{
    return side;
}

uint32_t replaytest_start_seed(void)
{
    return 0;
}

uint32_t replaytest_hash(void)
{
    return 0;
}

WireInput replaytest_input(int port, int frame)
{
    (void) port, (void) frame;
    return wire_neutral();
}

void replaytest_seed(int frame)
{
    (void) frame;
}

int replaytest_ended(int frame)
{
    (void) frame;
    return 0;
}

/* The game's: the stocks left in a match's report */
int Player_GetStocks(int slot)
{
    (void) slot;
    return 4;
}
