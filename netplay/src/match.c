/* A match with the other game, with rollback.
 *
 *   - Start: both games load the same match (session.c agreed on it), and
 *     each holds its first frame until the other's first packet of the match
 *     arrives, so both start together.
 *   - Every frame of the match, this side's input goes out delay frames ahead
 *     of the frame it plays on. Each packet carries every input the other side
 *     hasn't acknowledged, so a lost packet costs nothing but time.
 *   - This side's input is the controller this person holds, whichever port
 *     it's in (the "controller" setting), played in this side's port. Ports
 *     nobody plays read as empty on both machines.
 *   - pad_read gives each frame both ports' inputs: this side's, and the
 *     other side's if it has arrived, else a guess (the last one that did).
 *     The guess used is kept; when the real input arrives and differs, the
 *     advance event rolls back to that frame (tgg_rollback) and the frames
 *     since run again on what's now known.
 *   - A frame is confirmed once both inputs are known and it has run on them
 *     (tgg_rollback_confirm). Packets carry the checksum of the newest
 *     confirmed frame, and each side compares it with its own: the first
 *     frame that differs is a desync, which is logged.
 *   - The game holds a frame (advance's hold) when running it would guess
 *     further ahead than it can roll back, and now and then when it is ahead
 *     of the other side, so both run at the same pace.
 *   - After the match, the inputs go on until the other side has them all,
 *     since it can't leave the match before it has. */
#include "internal.h"
#include "netplay.h"

#include "tgg/events.h"
#include "tgg/rollback.h"
#include "tgg/screen.h"

#include <dolphin/pad.h>
#include <melee/ft/forward.h>
#include <melee/mn/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/random.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Frames of input kept, as a ring */
#define HIST 1024
#define SUMS 1024
/* How long a match waits for the other side before it gives up */
#define SILENCE_MS 10000

typedef struct Slot {
    int frame; /* the frame it holds, or -1 */
    WireInput in;
} Slot;

static struct {
    uint32_t number;  /* 0: none yet */
    int pending;      /* set up for, its match_setup hasn't come */
    int live;         /* its frames are running */
    int over;         /* it ended */
    int abandoned;    /* the session ended during it: no more holding */
    int reported;     /* its summary is in the log */
    uint32_t seed;
    int ports[2];     /* side 0's and side 1's controller ports */
    int controller;   /* the port of this person's controller, or -1 */
    uint32_t hash;    /* this side's match setup */
    int peer_in;      /* a packet of this match came from the other side */

    /* Inputs by frame */
    Slot local[HIST];
    int local_last;   /* the newest frame of this side's input */
    Slot remote[HIST];
    int remote_last;  /* every frame up to it has the other side's input */
    Slot used[HIST];  /* the other side's input each frame last ran on */
    int rollback_to;  /* the oldest frame that ran on a wrong guess, or INT_MAX */
    int peer_acked;   /* the newest frame of ours the other side has */
    int peer_frame;   /* the other side's next frame, as last heard */
    double peer_frame_ms;
    int confirmed;
    int sum_ready;    /* the newest frame confirmed and run as confirmed */
    int peer_sum_newest;
    int compared_upto;
    int last_sync_hold;
    double started_ms;

    /* Checksums the other side sent, by frame */
    struct {
        int frame;
        uint64_t sum;
    } peer_sums[SUMS];

    /* What happened, for the log */
    int desync_frame;
    unsigned desyncs, sums_compared, rollbacks, rolled_frames, max_depth, holds;
} mt;

static void reset(uint32_t number, uint32_t seed)
{
    memset(&mt, 0, sizeof mt);
    mt.number = number;
    mt.seed = seed;
    mt.pending = 1;
    mt.controller = -1;
    mt.rollback_to = INT_MAX;
    mt.remote_last = -1;
    mt.local_last = -1;
    mt.peer_acked = -1;
    mt.confirmed = -1;
    mt.sum_ready = -1;
    mt.peer_sum_newest = -1;
    mt.compared_upto = -1;
    mt.desync_frame = -1;
    mt.last_sync_hold = -100;
    for (int i = 0; i < HIST; i++) {
        mt.local[i].frame = mt.remote[i].frame = mt.used[i].frame = -1;
    }
    for (int i = 0; i < SUMS; i++) {
        mt.peer_sums[i].frame = -1;
    }
}

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

int match_ports(const StartMeleeData* data, int ports[2])
{
    int n = 0;
    for (int i = 0; i < 4; i++) {
        if (data->players[i].slot_type == Gm_PKind_Human) {
            if (n < 2) {
                ports[n] = i;
            }
            n++;
        }
    }
    return n == 2 ? 0 : -1;
}

int match_begin(uint32_t number, uint32_t seed, const StartMeleeData* data)
{
    reset(number, seed);
    if (match_ports(data, mt.ports) != 0) {
        return -1;
    }
    if (tgg_screen_play_match(data) != 0) {
        return -1;
    }
    ss.state = NETPLAY_PLAYING;
    mt.started_ms = net_now_ms();
    tgg_log("match %u: side %d plays port %d", number, ss.side, mt.ports[ss.side] + 1);
    return 0;
}

void match_expect_boot(void)
{
    reset(1, 0);
    mt.ports[0] = 0;
    mt.ports[1] = 1;
    if (ss.replay) {
        mt.ports[0] = replaytest_port(0);
        mt.ports[1] = replaytest_port(1);
    } else {
        const char* players = getenv("TGG_NETPLAY_PLAYERS");
        int a = 0, b = 0;
        if (players != NULL && sscanf(players, "%d,%d", &a, &b) == 2 && a >= 1 && a <= 4 &&
            b >= 1 && b <= 4 && a != b)
        {
            mt.ports[0] = a - 1;
            mt.ports[1] = b - 1;
        }
    }
}

uint32_t match_number(void)
{
    return mt.number;
}

int32_t match_input_ack(void)
{
    return mt.remote_last;
}

/* ---- Sending */

static void send_inputs(void)
{
    WirePacket p;
    memset(&p, 0, sizeof p);
    p.type = WIRE_INPUT;
    WireInputs* in = &p.u.inputs;
    in->number = mt.number;
    in->frame = mt.live && !mt.over ? tgg_frame() : -1;
    in->ack = mt.remote_last;
    in->match = mt.hash;
    /* The newest frame confirmed before this advance: a rollback asked for in
     * this one hasn't run yet, so a frame confirmed now may still change */
    in->sum_frame = -1;
    uint64_t sum;
    if (mt.sum_ready >= 0 && tgg_frame_checksum(mt.sum_ready, &sum) == 0) {
        in->sum_frame = mt.sum_ready;
        in->sum = sum;
    }
    in->ping = session_clock();
    in->pong = ss.peer_ping;
    in->pong_age = session_pong_age();
    /* Every input the other side hasn't acknowledged, oldest first, as many
     * as fit */
    int first = mt.peer_acked + 1;
    if (first < mt.local_last - WIRE_MAX_INPUTS + 1) {
        first = mt.local_last - WIRE_MAX_INPUTS + 1;
    }
    if (first < 0) {
        first = 0;
    }
    in->first = first;
    for (int f = first; f <= mt.local_last && in->count < WIRE_MAX_INPUTS; f++) {
        if (!have(mt.local, f)) {
            break;
        }
        in->inputs[in->count++] = at(mt.local, f)->in;
    }
    session_send_packet(&p);
}

/* ---- Receiving */

void match_got_ack(uint32_t number, int32_t ack)
{
    if (number == mt.number && ack > mt.peer_acked) {
        mt.peer_acked = ack;
    }
}

void match_got_inputs(const WireInputs* in)
{
    session_got_ping(in->ping);
    session_got_pong(in->pong, in->pong_age);
    if (in->number != mt.number || mt.number == 0) {
        return;
    }
    mt.peer_in = 1;
    if (in->match != 0 && mt.hash != 0 && in->match != mt.hash) {
        session_fail("The two games set up different matches.");
        return;
    }
    if (in->ack > mt.peer_acked) {
        mt.peer_acked = in->ack;
    }
    if (in->frame > mt.peer_frame) {
        mt.peer_frame = in->frame;
        mt.peer_frame_ms = net_now_ms();
    }
    for (int i = 0; i < in->count; i++) {
        const int f = in->first + i;
        if (f <= mt.remote_last || have(mt.remote, f)) {
            continue;
        }
        put(mt.remote, f, &in->inputs[i]);
        /* A frame that ran on a guess, and the guess was wrong */
        if (have(mt.used, f) && !wire_same(&at(mt.used, f)->in, &in->inputs[i]) &&
            f < mt.rollback_to)
        {
            mt.rollback_to = f;
        }
    }
    while (have(mt.remote, mt.remote_last + 1)) {
        mt.remote_last++;
    }
    if (in->sum_frame >= 0) {
        mt.peer_sums[(unsigned) in->sum_frame % SUMS].frame = in->sum_frame;
        mt.peer_sums[(unsigned) in->sum_frame % SUMS].sum = in->sum;
        if (in->sum_frame > mt.peer_sum_newest) {
            mt.peer_sum_newest = in->sum_frame;
        }
    }
}

/* Compares the other side's checksums with ours, for frames confirmed here
 * before this advance (a rollback asked for now hasn't run yet). The other
 * side sends one frame's checksum per packet, the newest it has settled;
 * frames it hasn't sent yet wait for it. */
static void compare_sums(int upto)
{
    if (upto > mt.peer_sum_newest) {
        upto = mt.peer_sum_newest;
    }
    for (int f = mt.compared_upto + 1; f <= upto; f++) {
        const int i = (unsigned) f % SUMS;
        if (mt.peer_sums[i].frame != f) {
            continue;
        }
        uint64_t ours;
        if (tgg_frame_checksum(f, &ours) != 0) {
            continue;
        }
        mt.sums_compared++;
        if (ours != mt.peer_sums[i].sum) {
            mt.desyncs++;
            if (mt.desync_frame < 0) {
                mt.desync_frame = f;
                tgg_log("desync: frame %d's checksum is %016llx here, %016llx there", f,
                        (unsigned long long) ours, (unsigned long long) mt.peer_sums[i].sum);
            }
        }
    }
    if (upto > mt.compared_upto) {
        mt.compared_upto = upto;
    }
}

/* ---- The frame loop */

/* This side's inputs up to frame `upto` that the controller doesn't give:
 * the first delay frames, and the replay test's */
static void fill_local(int upto)
{
    while (mt.local_last < upto) {
        const int f = mt.local_last + 1;
        WireInput in = wire_neutral();
        if (ss.replay) {
            in = replaytest_input(mt.ports[ss.side], f);
        } else if (f >= ss.delay) {
            /* Live play records the controller in pad_read */
            break;
        }
        put(mt.local, f, &in);
        mt.local_last = f;
    }
}

static void hold(tgg_advance* adv, int frame)
{
    adv->hold = 1;
    mt.holds++;
    if (net_now_ms() - ss.last_heard_ms > SILENCE_MS) {
        char why[120];
        snprintf(why, sizeof why, "Nothing from the other game for 10 seconds (frame %d).", frame);
        session_fail(why);
        adv->hold = 0;
    }
}

static void on_advance(tgg_advance* adv)
{
    if (ss.state == NETPLAY_OFF || ss.state == NETPLAY_FAILED) {
        return;
    }
    if (ss.env && ss.state < NETPLAY_CONNECTED) {
        /* Before the boot's match: hold until both sides have said hello (the
         * frame event says it) */
        if (adv->frame < 0) {
            adv->hold = 1;
        }
        return;
    }
    if (ss.env && ss.state == NETPLAY_CONNECTED && mt.pending) {
        ss.state = NETPLAY_PLAYING;
    }
    if (ss.state != NETPLAY_PLAYING) {
        return;
    }
    session_receive();
    if (ss.state != NETPLAY_PLAYING || mt.abandoned) {
        return;
    }
    if (adv->frame < 0 || !mt.live) {
        /* Loading the match: keep in touch, and say what side 1 may still
         * need to start it */
        session_send_lobby(0);
        return;
    }
    const int frame = adv->frame;
    if (!mt.peer_in) {
        /* The first frame waits for the other side to reach the match */
        session_send_lobby(0);
        send_inputs();
        hold(adv, frame);
        return;
    }
    if (!mt.over) {
        fill_local(frame + ss.delay);
    }

    /* A wrong guess: run again from the oldest frame it was used on */
    const int confirmed_before = mt.confirmed;
    if (mt.rollback_to != INT_MAX) {
        const int to = mt.rollback_to;
        mt.rollback_to = INT_MAX;
        if (to < frame) {
            const int r = tgg_rollback(to);
            if (r == 0) {
                mt.rollbacks++;
                mt.rolled_frames += (unsigned) (frame - to);
                if ((unsigned) (frame - to) > mt.max_depth) {
                    mt.max_depth = (unsigned) (frame - to);
                }
            } else {
                tgg_log("frame %d: can't roll back to frame %d (%d)", frame, to, r);
            }
        }
    }
    /* Confirmed: both inputs known and run on */
    const int confirmed = mt.remote_last < frame - 1 ? mt.remote_last : frame - 1;
    if (confirmed > mt.confirmed) {
        mt.confirmed = confirmed;
        tgg_rollback_confirm(confirmed);
    }
    mt.sum_ready = confirmed_before;
    compare_sums(confirmed_before);

    /* Hold when this frame would guess further ahead than a rollback reaches,
     * or when this side is ahead of the other */
    int wait = frame - mt.remote_last > ss.max_rollback;
    if (!wait && mt.peer_frame > 0) {
        const double frame_ms = 1000.0 / 60.0;
        const double peer_now =
            mt.peer_frame + ((net_now_ms() - mt.peer_frame_ms) + ss.rtt_ms / 2) / frame_ms;
        const double ahead = frame - peer_now;
        if (ahead > 1.0 && frame - mt.last_sync_hold >= 10) {
            mt.last_sync_hold = frame;
            wait = 1;
        }
    }
    if (wait) {
        hold(adv, frame);
    }
    send_inputs();
}
TGG_ON(advance, on_advance);

/* The match's setup: its seed, and a hash of the setup to check the other
 * side has the same one */
static void on_setup(const StartMeleeData* data)
{
    if (!mt.pending || (ss.state != NETPLAY_PLAYING && !ss.env)) {
        return;
    }
    mt.pending = 0;
    mt.live = 1;
    *HSD_RandSeedPtr = ss.env ? ss.seed : mt.seed;
    const uint8_t* p = (const uint8_t*) data;
    uint32_t h = 0x811c9dc5u;
    /* The rules' callbacks are addresses, which differ between games; the
     * players and the stage are what matter */
    for (size_t i = offsetof(StartMeleeData, players); i < sizeof *data; i++) {
        h = (h ^ p[i]) * 0x01000193u;
    }
    h = (h ^ (uint32_t) data->rules.stkind) * 0x01000193u;
    mt.hash = h != 0 ? h : 1;
}
TGG_ON(match_setup, on_setup);

/* The port of the controller this person holds: the setting's, else the one
 * in use, else the first one plugged in */
static int pick_controller(const PADStatus* raw)
{
    if (setting_controller >= 1 && setting_controller <= 4) {
        return setting_controller - 1;
    }
    if (mt.controller >= 0 && raw[mt.controller].err == PAD_ERR_NONE) {
        return mt.controller;
    }
    for (int port = 0; port < 4; port++) {
        if (raw[port].err == PAD_ERR_NONE) {
            return port;
        }
    }
    return -1;
}

/* Each run of each frame: both ports' inputs */
static void on_pads(tgg_pads* pads)
{
    const int frame = tgg_frame();
    if (!mt.live || frame < 0 || ss.state != NETPLAY_PLAYING) {
        return;
    }
    const int me = mt.ports[ss.side];
    const int them = mt.ports[!ss.side];
    if (!ss.replay && !tgg_resim() && !mt.over) {
        /* This person's controller, read now, plays delay frames later */
        const int port = pick_controller(pads->raw);
        if (port != mt.controller && port >= 0) {
            tgg_log("frame %d: playing with the controller in port %d", frame, port + 1);
        }
        mt.controller = port;
        WireInput in = wire_neutral();
        if (port >= 0) {
            in = wire_from_pad(&pads->status[port], &pads->raw[port]);
        }
        fill_local(frame + ss.delay - 1);
        if (!have(mt.local, frame + ss.delay)) {
            put(mt.local, frame + ss.delay, &in);
            mt.local_last = frame + ss.delay;
        }
    }
    const WireInput mine = have(mt.local, frame) ? at(mt.local, frame)->in : wire_neutral();
    WireInput theirs;
    if (have(mt.remote, frame)) {
        theirs = at(mt.remote, frame)->in;
    } else if (mt.remote_last >= 0) {
        theirs = at(mt.remote, mt.remote_last)->in;
    } else {
        theirs = wire_neutral();
    }
    put(mt.used, frame, &theirs);
    wire_to_pad(&mine, &pads->status[me], &pads->raw[me]);
    wire_to_pad(&theirs, &pads->status[them], &pads->raw[them]);
    /* The ports nobody plays are empty on both machines */
    for (int port = 0; port < 4; port++) {
        if (port != me && port != them) {
            memset(&pads->raw[port], 0, sizeof pads->raw[port]);
            pads->raw[port].err = PAD_ERR_NO_CONTROLLER;
            tgg_pad_process(&pads->status[port], &pads->raw[port]);
        }
    }
    if (ss.replay) {
        replaytest_seed(frame);
    }
}
TGG_ON(pad_read, on_pads);

/* What the match did, for the log: how it ended (`ended`), or where it was
 * when the game left it */
static void report(int ended)
{
    if (mt.reported) {
        return;
    }
    mt.reported = 1;
    const int end = tgg_frame();
    char result[160] = "";
    size_t used = 0;
    for (int s = 0; s < 2; s++) {
        const int port = mt.ports[s];
        const int w = snprintf(result + used, sizeof result - used, "%sP%d %d stocks left",
                               s != 0 ? ", " : "", port + 1, Player_GetStocks(port));
        used = w > 0 && used + (size_t) w < sizeof result ? used + (size_t) w : used;
    }
    uint64_t sum = 0;
    const int last = end - 1;
    const int have_sum = tgg_frame_checksum(last, &sum) == 0;
    tgg_log("match %u %s at frame %d: %s; frame %d's checksum %016llx%s", mt.number,
            ended ? "over" : "left", end, result, last, (unsigned long long) sum,
            have_sum ? "" : " (not kept)");
    tgg_log("match %u: %u rollbacks (%u frames, deepest %u), %u holds, rtt %.0f ms", mt.number,
            mt.rollbacks, mt.rolled_frames, mt.max_depth, mt.holds, ss.rtt_ms);
    if (mt.desync_frame >= 0) {
        tgg_log("match %u: checksums: %u compared, %u differ, the first at frame %d", mt.number,
                mt.sums_compared, mt.desyncs, mt.desync_frame);
    } else {
        tgg_log("match %u: checksums: %u compared, all the same", mt.number, mt.sums_compared);
    }
}

static void on_match_end(void)
{
    if (!mt.live || mt.over) {
        return;
    }
    mt.over = 1;
    report(1);
    session_match_over();
}
TGG_ON(match_end, on_match_end);

void match_linger(void)
{
    if (mt.number != 0 && (mt.over || mt.abandoned) && mt.peer_acked < mt.local_last &&
        ss.state != NETPLAY_FAILED)
    {
        static double last;
        if (net_now_ms() - last >= 30) {
            last = net_now_ms();
            send_inputs();
        }
    }
}

void match_linger_until_done(void)
{
    if (mt.live && !mt.over) {
        report(0);
    }
    const double until = net_now_ms() + 3000;
    while (net_now_ms() < until && ss.state != NETPLAY_FAILED && mt.number != 0 &&
           mt.peer_acked < mt.local_last)
    {
        session_receive();
        send_inputs();
        session_send_lobby(1);
        net_wait_ms(5);
    }
}

void match_abandon(void)
{
    if (!mt.live || mt.over || mt.abandoned) {
        return;
    }
    mt.abandoned = 1;
    report(0);
    tgg_end_match();
}
