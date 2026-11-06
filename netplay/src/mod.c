/* Rollback netplay for two players.
 *
 * Settings (environment variables, read once at startup):
 *   TGG_NETPLAY_PEER      the other game, host:port (netplay is off without it)
 *   TGG_NETPLAY_PORT      the UDP port to listen on (default 51000)
 *   TGG_NETPLAY_SIDE      0 or 1: side 0 hosts (its seed starts the match)
 *   TGG_NETPLAY_DELAY     frames of input delay (default 2)
 *   TGG_NETPLAY_ROLLBACK  the most frames to roll back (default 8)
 *   TGG_NETPLAY_PLAYERS   the controller ports of side 0 and side 1 (default
 *                         "1,2"); this game's controller plays its side's port
 *   TGG_NETPLAY_FAKE      a fake network for tests (fakenet.c)
 *   TGG_NETPLAY_REPLAY    the replay test (replaytest.c): play a Slippi
 *                         replay's match, this side's inputs from the replay
 *
 * How a match goes:
 *   - Handshake: before the match, the advance event holds the game until
 *     both sides have said hello: the game build, the mod-set id, the side
 *     and the seed (side 0's), and in the replay test which replay. Anything
 *     different stops the game with a message.
 *   - Every frame of the match, this side's input goes out delay frames ahead
 *     of the frame it plays on. Each packet carries every input the other side
 *     hasn't acknowledged, so a lost packet costs nothing but time.
 *   - pad_read gives each frame both ports' inputs: this side's, and the
 *     other side's if it has arrived, else a guess (the last one that did).
 *     The guess used is kept; when the real input arrives and differs, the
 *     advance event rolls back to that frame (tgg_rollback) and the frames
 *     since run again on what's now known.
 *   - A frame is confirmed once both inputs are known and it has run on
 *     them (tgg_rollback_confirm). Packets carry the checksum of the newest
 *     confirmed frame, and each side compares it with its own: the first
 *     frame that differs is a desync, which is logged.
 *   - The game holds a frame (advance's hold) when running it would guess
 *     further ahead than it can roll back, and now and then when it is
 *     ahead of the other side, so both run at the same pace.
 *   - At the end (the quit event) it keeps sending until the other side has
 *     every input, for up to three seconds.
 */
#include "netplay.h"

#include "tgg/control.h"
#include "tgg/events.h"
#include "tgg/rollback.h"

#include <melee/mn/types.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/random.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Frames of input kept, as a ring */
#define HIST 1024
#define SUMS 1024

typedef struct Slot {
    int frame; /* the frame it holds, or -1 */
    WireInput in;
} Slot;

static struct {
    int on;
    int side;
    int delay;
    int max_rollback;
    int ports[2]; /* side 0's and side 1's controller ports */
    int replay;   /* the replay test */
    uint32_t seed;
    double started_ms;

    /* The handshake */
    int greeted;      /* the other side's hello arrived and matched */
    int heard_back;   /* it has ours: an input packet came */
    double last_hello;
    uint32_t match_hash; /* this side's match setup */

    /* Inputs by frame */
    Slot local[HIST];
    int local_last; /* the newest frame of this side's input */
    Slot remote[HIST];
    int remote_last; /* every frame up to it has the other side's input */
    Slot used[HIST]; /* the other side's input each frame last ran on */
    int rollback_to; /* the oldest frame that ran on a wrong guess, or INT_MAX */
    int peer_acked;  /* the newest frame of ours the other side has */
    int peer_frame;  /* the other side's next frame, as last heard */
    double peer_frame_ms;
    uint32_t peer_ping;
    double rtt_ms;
    int confirmed;
    int sum_ready; /* the newest frame confirmed and run as confirmed */
    int peer_sum_newest;
    int compared_upto;

    /* Checksums the other side sent, by frame */
    struct {
        int frame;
        uint64_t sum;
    } peer_sums[SUMS];

    /* What happened, for the log */
    int desync_frame;
    unsigned desyncs, sums_compared, rollbacks, rolled_frames, max_depth, holds;
    unsigned packets_in, packets_out;
    int bye;
    int match_over;
    double held_since;
} np;

static int setting(const char* name, int fallback)
{
    const char* v = getenv(name);
    return v != NULL && v[0] != '\0' ? atoi(v) : fallback;
}

static void init(void)
{
    const char* peer = getenv("TGG_NETPLAY_PEER");
    if (peer == NULL || peer[0] == '\0') {
        tgg_log("set TGG_NETPLAY_PEER to the other game's host:port to play over the network");
        return;
    }
    np.side = setting("TGG_NETPLAY_SIDE", 0) != 0;
    np.delay = setting("TGG_NETPLAY_DELAY", 2);
    if (np.delay < 0 || np.delay > 15) {
        np.delay = 2;
    }
    np.max_rollback = setting("TGG_NETPLAY_ROLLBACK", 8);
    if (np.max_rollback < 1 || np.max_rollback > TGG_ROLLBACK_MAX) {
        np.max_rollback = 8;
    }
    np.ports[0] = 0;
    np.ports[1] = 1;
    const char* players = getenv("TGG_NETPLAY_PLAYERS");
    if (players != NULL && players[0] != '\0') {
        int a = 0, b = 0;
        if (sscanf(players, "%d,%d", &a, &b) == 2 && a >= 1 && a <= 4 && b >= 1 && b <= 4 && a != b) {
            np.ports[0] = a - 1;
            np.ports[1] = b - 1;
        }
    }
    const char* replay = getenv("TGG_NETPLAY_REPLAY");
    if (replay != NULL && replay[0] != '\0') {
        if (!replaytest_open(replay)) {
            return;
        }
        np.replay = 1;
        np.ports[0] = replaytest_port(0);
        np.ports[1] = replaytest_port(1);
        np.seed = replaytest_start_seed();
    } else {
        /* Side 0's seed starts the match; side 1 takes it from the hello */
        FILE* f = fopen("/dev/urandom", "rb");
        if (f == NULL || fread(&np.seed, sizeof np.seed, 1, f) != 1) {
            np.seed = (uint32_t) net_now_ms();
        }
        if (f != NULL) {
            fclose(f);
        }
    }
    const char* why = net_open(setting("TGG_NETPLAY_PORT", 51000), peer);
    if (why != NULL) {
        tgg_log("%s", why);
        return;
    }
    fakenet_init(getenv("TGG_NETPLAY_FAKE"));
    tgg_rollback_init(np.max_rollback);
    np.on = 1;
    np.rollback_to = INT_MAX;
    np.remote_last = -1;
    np.local_last = -1;
    np.peer_acked = -1;
    np.confirmed = -1;
    np.sum_ready = -1;
    np.peer_sum_newest = -1;
    np.compared_upto = -1;
    np.desync_frame = -1;
    for (int i = 0; i < HIST; i++) {
        np.local[i].frame = np.remote[i].frame = np.used[i].frame = -1;
    }
    for (int i = 0; i < SUMS; i++) {
        np.peer_sums[i].frame = -1;
    }
    np.started_ms = net_now_ms();
    tgg_log("side %d, port %d, playing with %s: %d frames of delay, rolls back up to %d", np.side,
            np.ports[np.side] + 1, peer, np.delay, np.max_rollback);
}
TGG_INIT(init);

static Slot* at(Slot* ring, int frame)
{
    return &ring[(unsigned) frame % HIST];
}

static int have(Slot* ring, int frame)
{
    return frame >= 0 && at(ring, frame)->frame == frame;
}

static void put(Slot* ring, int frame, const WireInput* in)
{
    Slot* s = at(ring, frame);
    s->frame = frame;
    s->in = *in;
}

/* ---- Sending */

static void send_packet(const WirePacket* p)
{
    uint8_t buf[WIRE_MAX_BYTES];
    const size_t len = wire_encode(p, buf);
    net_send(buf, len);
    np.packets_out++;
}

static void send_hello(void)
{
    WirePacket p;
    memset(&p, 0, sizeof p);
    p.type = WIRE_HELLO;
    snprintf(p.u.hello.build, sizeof p.u.hello.build, "%s", tgg_game_build());
    snprintf(p.u.hello.mod_set, sizeof p.u.hello.mod_set, "%s", tgg_mod_set_id());
    p.u.hello.side = (uint8_t) np.side;
    p.u.hello.delay = (uint8_t) np.delay;
    p.u.hello.seed = np.seed;
    p.u.hello.replay = np.replay ? replaytest_hash() : 0;
    send_packet(&p);
}

static void send_inputs(void)
{
    WirePacket p;
    memset(&p, 0, sizeof p);
    p.type = WIRE_INPUT;
    WireInputs* in = &p.u.inputs;
    in->frame = tgg_frame();
    in->ack = np.remote_last;
    in->match = np.match_hash;
    /* The newest frame confirmed before this advance: a rollback asked for in
     * this one hasn't run yet, so a frame confirmed now may still change */
    in->sum_frame = -1;
    uint64_t sum;
    if (np.sum_ready >= 0 && tgg_frame_checksum(np.sum_ready, &sum) == 0) {
        in->sum_frame = np.sum_ready;
        in->sum = sum;
    }
    in->ping = (uint32_t) (net_now_ms() - np.started_ms);
    in->pong = np.peer_ping;
    /* Every input the other side hasn't acknowledged, oldest first, as many
     * as fit */
    int first = np.peer_acked + 1;
    if (first < np.local_last - WIRE_MAX_INPUTS + 1) {
        first = np.local_last - WIRE_MAX_INPUTS + 1;
    }
    if (first < 0) {
        first = 0;
    }
    in->first = first;
    for (int f = first; f <= np.local_last && in->count < WIRE_MAX_INPUTS; f++) {
        if (!have(np.local, f)) {
            break;
        }
        in->inputs[in->count++] = at(np.local, f)->in;
    }
    send_packet(&p);
}

/* ---- Receiving */

static void fail(const char* fmt, const char* ours, const char* theirs)
{
    tgg_log(fmt, ours, theirs);
    tgg_log("stopping: the two games can't play together");
    fflush(stdout);
    fflush(stderr);
    _Exit(3);
}

static void got_hello(const WireHello* h)
{
    if (h->side == np.side) {
        char a[8], b[8];
        snprintf(a, sizeof a, "%d", np.side);
        snprintf(b, sizeof b, "%d", h->side);
        fail("both games are side %s (the other says %s): set TGG_NETPLAY_SIDE to 0 on one, 1 "
             "on the other", a, b);
    }
    if (strcmp(h->build, tgg_game_build()) != 0) {
        fail("this game is %s, the other is %s", tgg_game_build(), h->build);
    }
    if (strcmp(h->mod_set, tgg_mod_set_id()) != 0) {
        fail("this game's mod set is %s, the other's is %s; this one runs: see the log's "
             "mod-set line", tgg_mod_set_id(), h->mod_set);
    }
    if (h->replay != (np.replay ? replaytest_hash() : 0)) {
        fail("the two games play different replays%s%s", "", "");
    }
    if (np.side == 1 && !np.replay) {
        np.seed = h->seed;
    }
    if (!np.greeted) {
        np.greeted = 1;
        tgg_log("the other game said hello: same build and mods (%s: %s)",
                tgg_mod_set_id()[0] != '\0' ? tgg_mod_set_id() : "none",
                tgg_mod_set_names());
    }
}

static void got_inputs(const WireInputs* in)
{
    np.heard_back = 1;
    if (in->match != 0 && np.match_hash != 0 && in->match != np.match_hash) {
        fail("the two games set up different matches%s%s", "", "");
    }
    if (in->ack > np.peer_acked) {
        np.peer_acked = in->ack;
    }
    if (in->frame > np.peer_frame) {
        np.peer_frame = in->frame;
        np.peer_frame_ms = net_now_ms();
    }
    np.peer_ping = in->ping;
    if (in->pong != 0) {
        const double rtt = (net_now_ms() - np.started_ms) - in->pong;
        if (rtt >= 0) {
            np.rtt_ms = np.rtt_ms == 0 ? rtt : np.rtt_ms * 0.9 + rtt * 0.1;
        }
    }
    for (int i = 0; i < in->count; i++) {
        const int f = in->first + i;
        if (f <= np.remote_last || have(np.remote, f)) {
            continue;
        }
        put(np.remote, f, &in->inputs[i]);
        /* A frame that ran on a guess, and the guess was wrong */
        if (have(np.used, f) && !wire_same(&at(np.used, f)->in, &in->inputs[i]) &&
            f < np.rollback_to)
        {
            np.rollback_to = f;
        }
    }
    while (have(np.remote, np.remote_last + 1)) {
        np.remote_last++;
    }
    if (in->sum_frame >= 0) {
        np.peer_sums[(unsigned) in->sum_frame % SUMS].frame = in->sum_frame;
        np.peer_sums[(unsigned) in->sum_frame % SUMS].sum = in->sum;
        if (in->sum_frame > np.peer_sum_newest) {
            np.peer_sum_newest = in->sum_frame;
        }
    }
}

static void receive(void)
{
    fakenet_flush();
    uint8_t buf[WIRE_MAX_BYTES];
    size_t len;
    while ((len = net_recv(buf, sizeof buf)) != 0) {
        WirePacket p;
        if (!wire_decode(buf, len, &p)) {
            continue;
        }
        np.packets_in++;
        if (p.type == WIRE_HELLO) {
            got_hello(&p.u.hello);
        } else if (p.type == WIRE_INPUT && np.greeted) {
            got_inputs(&p.u.inputs);
        } else if (p.type == WIRE_BYE) {
            np.bye = 1;
        }
    }
}

/* Compares the other side's checksums with ours, for frames confirmed here
 * before this advance (a rollback asked for now hasn't run yet). The other
 * side sends one frame's checksum per packet, the newest it has settled;
 * frames it hasn't sent yet wait for it. */
static void compare_sums(int upto)
{
    if (upto > np.peer_sum_newest) {
        upto = np.peer_sum_newest;
    }
    for (int f = np.compared_upto + 1; f <= upto; f++) {
        const int i = (unsigned) f % SUMS;
        if (np.peer_sums[i].frame != f) {
            continue;
        }
        uint64_t ours;
        if (tgg_frame_checksum(f, &ours) != 0) {
            continue;
        }
        np.sums_compared++;
        if (ours != np.peer_sums[i].sum) {
            np.desyncs++;
            if (np.desync_frame < 0) {
                np.desync_frame = f;
                tgg_log("desync: frame %d's checksum is %016llx here, %016llx there", f,
                        (unsigned long long) ours, (unsigned long long) np.peer_sums[i].sum);
            }
        }
    }
    if (upto > np.compared_upto) {
        np.compared_upto = upto;
    }
}

/* ---- The frame loop */

/* This side's inputs up to frame `upto` */
static void fill_local(int upto)
{
    while (np.local_last < upto) {
        const int f = np.local_last + 1;
        WireInput in = wire_neutral();
        if (np.replay) {
            in = replaytest_input(np.ports[np.side], f);
        } else if (f >= np.delay) {
            /* Live play records the controller in pad_read */
            break;
        }
        put(np.local, f, &in);
        np.local_last = f;
    }
}

static void on_advance(tgg_advance* adv)
{
    if (!np.on) {
        return;
    }
    receive();
    if (!np.greeted || (!np.heard_back && adv->frame < 0)) {
        /* Before the match: hold until both sides have said hello */
        if (net_now_ms() - np.last_hello > 100) {
            np.last_hello = net_now_ms();
            send_hello();
            if (np.greeted) {
                send_inputs();
            }
        }
        adv->hold = 1;
        return;
    }
    if (adv->frame < 0) {
        send_inputs();
        return;
    }
    const int frame = adv->frame;
    if (!np.match_over) {
        fill_local(frame + np.delay);
    }

    /* A wrong guess: run again from the oldest frame it was used on */
    const int confirmed_before = np.confirmed;
    if (np.rollback_to != INT_MAX) {
        const int to = np.rollback_to;
        np.rollback_to = INT_MAX;
        if (to < frame) {
            const int r = tgg_rollback(to);
            if (r == 0) {
                np.rollbacks++;
                np.rolled_frames += (unsigned) (frame - to);
                if ((unsigned) (frame - to) > np.max_depth) {
                    np.max_depth = (unsigned) (frame - to);
                }
            } else {
                tgg_log("frame %d: can't roll back to frame %d (%d)", frame, to, r);
            }
        }
    }
    /* Confirmed: both inputs known and run on */
    int confirmed = np.remote_last < frame - 1 ? np.remote_last : frame - 1;
    if (confirmed > np.confirmed) {
        np.confirmed = confirmed;
        tgg_rollback_confirm(confirmed);
    }
    np.sum_ready = confirmed_before;
    compare_sums(confirmed_before);

    /* Hold when this frame would guess further ahead than a rollback reaches,
     * or when this side is ahead of the other */
    int hold = frame - np.remote_last > np.max_rollback;
    if (!hold && np.peer_frame > 0) {
        const double frame_ms = 1000.0 / 60.0;
        const double peer_now = np.peer_frame + ((net_now_ms() - np.peer_frame_ms) +
                                                 np.rtt_ms / 2) / frame_ms;
        const double ahead = frame - peer_now;
        static int last_sync_hold;
        if (ahead > 1.0 && frame - last_sync_hold >= 10) {
            last_sync_hold = frame;
            hold = 1;
        }
    }
    if (hold) {
        adv->hold = 1;
        np.holds++;
        if (np.held_since == 0) {
            np.held_since = net_now_ms();
        } else if (net_now_ms() - np.held_since > 10000) {
            tgg_log("frame %d: nothing from the other game for 10 seconds%s", frame,
                    np.bye ? " (it has left)" : "");
            fflush(stdout);
            fflush(stderr);
            _Exit(4);
        }
    } else {
        np.held_since = 0;
    }
    send_inputs();
}
TGG_ON(advance, on_advance);

/* The match's setup: its seed, and a hash of the setup to check the other
 * side has the same one */
static void on_setup(const StartMeleeData* data)
{
    if (!np.on) {
        return;
    }
    *HSD_RandSeedPtr = np.seed;
    const uint8_t* p = (const uint8_t*) data;
    uint32_t h = 0x811c9dc5u;
    /* The rules' callbacks are addresses, which differ between games; the
     * players and the stage are what matter */
    for (size_t i = offsetof(StartMeleeData, players); i < sizeof *data; i++) {
        h = (h ^ p[i]) * 0x01000193u;
    }
    h = (h ^ (uint32_t) data->rules.stkind) * 0x01000193u;
    np.match_hash = h != 0 ? h : 1;
}
TGG_ON(match_setup, on_setup);

/* Each run of each frame: both ports' inputs */
static void on_pads(tgg_pads* pads)
{
    const int frame = tgg_frame();
    if (!np.on || frame < 0) {
        return;
    }
    const int me = np.ports[np.side];
    const int them = np.ports[!np.side];
    if (!np.replay && !tgg_resim() && !np.match_over) {
        /* This side's controller, read now, plays delay frames later */
        const WireInput in = wire_from_pad(&pads->status[me], &pads->raw[me]);
        fill_local(frame + np.delay - 1);
        if (!have(np.local, frame + np.delay)) {
            put(np.local, frame + np.delay, &in);
            np.local_last = frame + np.delay;
        }
    }
    const WireInput mine = have(np.local, frame) ? at(np.local, frame)->in : wire_neutral();
    WireInput theirs;
    if (have(np.remote, frame)) {
        theirs = at(np.remote, frame)->in;
    } else if (np.remote_last >= 0) {
        theirs = at(np.remote, np.remote_last)->in;
    } else {
        theirs = wire_neutral();
    }
    put(np.used, frame, &theirs);
    wire_to_pad(&mine, &pads->status[me], &pads->raw[me]);
    wire_to_pad(&theirs, &pads->status[them], &pads->raw[them]);
    if (np.replay) {
        replaytest_seed(frame);
    }
}
TGG_ON(pad_read, on_pads);

static void on_match_end(void)
{
    np.match_over = 1;
}
TGG_ON(match_end, on_match_end);

/* Before the game exits: stay until the other side has all of this side's
 * inputs, so it can finish too */
static void on_quit(void)
{
    if (!np.on) {
        return;
    }
    const double until = net_now_ms() + 3000;
    while (net_now_ms() < until && !np.bye && np.peer_acked < np.local_last) {
        receive();
        send_inputs();
        net_sleep_ms(5);
    }
    WirePacket p;
    memset(&p, 0, sizeof p);
    p.type = WIRE_BYE;
    for (int i = 0; i < 5; i++) {
        send_packet(&p);
    }
    const double flush_until = net_now_ms() + 200;
    while (net_now_ms() < flush_until) {
        fakenet_flush();
        net_sleep_ms(5);
    }
    tgg_log("%u rollbacks (%u frames, deepest %u), %u holds, rtt %.0f ms, %u packets out, %u in",
            np.rollbacks, np.rolled_frames, np.max_depth, np.holds, np.rtt_ms, np.packets_out,
            np.packets_in);
    if (np.desync_frame >= 0) {
        tgg_log("checksums: %u compared, %u differ, the first at frame %d", np.sums_compared,
                np.desyncs, np.desync_frame);
    } else {
        tgg_log("checksums: %u compared, all the same", np.sums_compared);
    }
}
TGG_ON(quit, on_quit);
