/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A match with the other games, with rollback.
 *
 *   - Start: every game loads the same match (session.c agreed on it), and
 *     each holds its first frame until a packet of the match has come on each
 *     of its links, so all start together.
 *   - Every frame of the match, this game's input goes out delay frames ahead
 *     of the frame it plays on. A joiner sends its own to the host; the host
 *     sends each joiner everyone else's, its own and the other joiners'. Each
 *     packet carries every input the other end hasn't acknowledged, so a lost
 *     packet costs nothing but time.
 *   - This game's input is the controller this person holds, whichever port
 *     it's in (the "controller" setting), played in this side's port: side 0
 *     plays the match's first human port, side 1 the second, and so on. Ports
 *     nobody plays read as empty on every machine.
 *   - pad_read gives each frame every side's input: this game's, and each
 *     other side's if it has arrived, else a guess (the last one that did).
 *     The guess used is kept; when the real input arrives and differs, the
 *     advance event rolls back to that frame (tgg_rollback) and the frames
 *     since run again on what's now known.
 *   - A frame is confirmed once every side's input is known and it has run on
 *     them (tgg_rollback_confirm). Packets carry the checksum of the newest
 *     confirmed frame; joiners compare theirs with the host's, and the host
 *     with each joiner's. The first frame that differs is a desync, which is
 *     logged.
 *   - The game holds a frame (advance's hold) when running it would guess
 *     further ahead than it can roll back, and now and then when it is ahead
 *     of a game it talks to, so all run at the same pace.
 *   - After the match, the inputs go on until every link has them all, since
 *     a game can't leave the match before it has. */
#include "internal.h"
#include "netplay.h"

#include "tgg/draw.h"
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
/* How long a match waits for the other games before it gives up */
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
    int players;
    int ports[SIDES]; /* each side's controller port */
    int controller;   /* the port of this person's controller, or -1 */
    uint32_t hash;    /* this game's match setup */
    int link_in[NET_PEERS]; /* a packet of this match came on the link */

    /* Inputs by side and frame; this game's own are in[ss.side] */
    Slot in[SIDES][HIST];
    int last[SIDES];        /* every frame up to it has the side's input */
    Slot used[SIDES][HIST]; /* the input each frame last ran on, guessed or not */
    int rollback_to;  /* the oldest frame that ran on a wrong guess, or INT_MAX */
    int acked[NET_PEERS][SIDES]; /* the newest frame of each side's input each link has */
    int link_frame[NET_PEERS];   /* each link's next frame, as last heard */
    double link_frame_ms[NET_PEERS];
    int confirmed;
    int sum_ready;    /* the newest frame confirmed and run as confirmed */
    int last_sync_hold;
    double started_ms;

    /* Checksums each link sent, by frame */
    struct {
        int frame;
        uint64_t sum;
    } sums[NET_PEERS][SUMS];
    int sum_newest[NET_PEERS];
    int compared_upto[NET_PEERS];

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
    mt.confirmed = -1;
    mt.sum_ready = -1;
    mt.desync_frame = -1;
    ss.desync[0] = '\0';
    mt.last_sync_hold = -100;
    for (int s = 0; s < SIDES; s++) {
        mt.last[s] = -1;
        for (int i = 0; i < HIST; i++) {
            mt.in[s][i].frame = mt.used[s][i].frame = -1;
        }
    }
    for (int l = 0; l < NET_PEERS; l++) {
        mt.sum_newest[l] = -1;
        mt.compared_upto[l] = -1;
        for (int s = 0; s < SIDES; s++) {
            mt.acked[l][s] = -1;
        }
        for (int i = 0; i < SUMS; i++) {
            mt.sums[l][i].frame = -1;
        }
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

int match_ports(const StartMeleeData* data, int ports[SIDES])
{
    int n = 0;
    for (int i = 0; i < 4; i++) {
        if (data->players[i].slot_type == Gm_PKind_Human && n < SIDES) {
            ports[n++] = i;
        }
    }
    return n > 0 ? n : -1;
}

int match_begin(uint32_t number, uint32_t seed, const StartMeleeData* data)
{
    reset(number, seed);
    mt.players = match_ports(data, mt.ports);
    if (mt.players != ss.players) {
        return -1;
    }
    if (tgg_screen_play_match(data) != 0) {
        return -1;
    }
    ss.state = NETPLAY_PLAYING;
    mt.started_ms = net_now_ms();
    tgg_log("match %u: %d games, side %d plays port %d", number, mt.players, ss.side,
            mt.ports[ss.side] + 1);
    return 0;
}

void match_expect_boot(void)
{
    reset(1, 0);
}

void match_expect(uint32_t number, uint32_t seed, const StartMeleeData* data)
{
    reset(number, seed);
    mt.players = match_ports(data, mt.ports);
    ss.state = NETPLAY_PLAYING;
    tgg_log("match %u: %d games, side %d plays port %d (from the game's menus)", number,
            mt.players, ss.side, mt.ports[ss.side] + 1);
}

/* The boot's match: side s plays port s + 1, or as TGG_NETPLAY_PLAYERS
 * ("1,3,4") or the replay say */
static void boot_ports(void)
{
    mt.players = ss.players;
    for (int s = 0; s < SIDES; s++) {
        mt.ports[s] = s;
    }
    if (ss.replay) {
        mt.ports[0] = replaytest_port(0);
        mt.ports[1] = replaytest_port(1);
        return;
    }
    const char* players = getenv("TGG_NETPLAY_PLAYERS");
    int p[SIDES] = { 0 };
    if (players != NULL && sscanf(players, "%d,%d,%d,%d", &p[0], &p[1], &p[2], &p[3]) >= 2) {
        for (int s = 0; s < mt.players; s++) {
            if (p[s] >= 1 && p[s] <= 4) {
                mt.ports[s] = p[s] - 1;
            }
        }
    }
}

uint32_t match_number(void)
{
    return mt.number;
}

uint32_t match_seed(void)
{
    return mt.seed;
}

void match_input_acks(int32_t out[SIDES])
{
    for (int s = 0; s < SIDES; s++) {
        out[s] = s < mt.players ? mt.last[s] : -1;
    }
}

/* Whether link `link` needs side `side`'s inputs from this game: a joiner
 * sends its own to the host, and the host everyone else's to each joiner */
static int link_needs(int link, int side)
{
    if (side >= mt.players || side == link_side(link)) {
        return 0;
    }
    return ss.side == 0 || side == ss.side;
}

/* ---- Sending */

static void send_inputs(int link)
{
    WirePacket p;
    memset(&p, 0, sizeof p);
    p.type = WIRE_INPUT;
    WireInputs* in = &p.u.inputs;
    in->number = mt.number;
    in->frame = mt.live && !mt.over ? tgg_frame() : -1;
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
    in->pong = ss.link[link].peer_ping;
    in->pong_age = session_pong_age(link);
    match_input_acks(in->ack);
    /* Each side's inputs the link hasn't acknowledged, oldest first, a share
     * of the packet each */
    int sides = 0;
    for (int s = 0; s < mt.players; s++) {
        sides += link_needs(link, s);
    }
    const int share = sides > 0 ? WIRE_MAX_INPUTS / sides : 0;
    int total = 0;
    for (int s = 0; s < mt.players && in->runs < WIRE_MAX_RUNS; s++) {
        if (!link_needs(link, s)) {
            continue;
        }
        const int first = mt.acked[link][s] + 1 > 0 ? mt.acked[link][s] + 1 : 0;
        int count = 0;
        for (int f = first; f <= mt.last[s] && count < share; f++) {
            if (!have(mt.in[s], f)) {
                break;
            }
            in->inputs[total + count++] = at(mt.in[s], f)->in;
        }
        if (count > 0) {
            in->run[in->runs].side = (uint8_t) s;
            in->run[in->runs].first = first;
            in->run[in->runs].count = (uint8_t) count;
            in->runs++;
            total += count;
        }
    }
    session_send_packet(link, &p);
}

static void send_all_inputs(void)
{
    for (int l = 0; l < net_peers(); l++) {
        if (ss.link[l].greeted) {
            send_inputs(l);
        }
    }
}

/* ---- Receiving */

void match_got_ack(int link, uint32_t number, const int32_t ack[SIDES])
{
    if (number != mt.number) {
        return;
    }
    for (int s = 0; s < SIDES; s++) {
        if (ack[s] > mt.acked[link][s]) {
            mt.acked[link][s] = ack[s];
        }
    }
}

void match_got_inputs(int link, const WireInputs* in)
{
    session_got_ping(link, in->ping);
    session_got_pong(link, in->pong, in->pong_age);
    if (in->number != mt.number || mt.number == 0) {
        return;
    }
    mt.link_in[link] = 1;
    if (in->match != 0 && mt.hash != 0 && in->match != mt.hash) {
        session_fail("The games set up different matches.");
        return;
    }
    match_got_ack(link, in->number, in->ack);
    if (in->frame > mt.link_frame[link]) {
        mt.link_frame[link] = in->frame;
        mt.link_frame_ms[link] = net_now_ms();
    }
    int next = 0;
    for (int r = 0; r < in->runs; r++) {
        const WireRun* run = &in->run[r];
        const int s = run->side;
        const int from = next;
        next += run->count;
        /* The host takes each joiner's own inputs; a joiner, everyone's */
        if (s >= mt.players || s == ss.side || (ss.side == 0 && s != link_side(link))) {
            continue;
        }
        for (int i = 0; i < run->count; i++) {
            const int f = run->first + i;
            if (f <= mt.last[s] || have(mt.in[s], f)) {
                continue;
            }
            const WireInput* got = &in->inputs[from + i];
            put(mt.in[s], f, got);
            /* A frame that ran on a guess, and the guess was wrong */
            if (have(mt.used[s], f) && !wire_same(&at(mt.used[s], f)->in, got) &&
                f < mt.rollback_to)
            {
                mt.rollback_to = f;
            }
        }
        while (have(mt.in[s], mt.last[s] + 1)) {
            mt.last[s]++;
        }
    }
    if (in->sum_frame >= 0) {
        mt.sums[link][(unsigned) in->sum_frame % SUMS].frame = in->sum_frame;
        mt.sums[link][(unsigned) in->sum_frame % SUMS].sum = in->sum;
        if (in->sum_frame > mt.sum_newest[link]) {
            mt.sum_newest[link] = in->sum_frame;
        }
    }
}

/* Compares each link's checksums with ours, for frames confirmed here before
 * this advance (a rollback asked for now hasn't run yet). A link sends one
 * frame's checksum per packet, the newest it has settled; frames it hasn't
 * sent yet wait for it. */
static void compare_sums(int confirmed)
{
    for (int l = 0; l < net_peers(); l++) {
        const int upto = confirmed < mt.sum_newest[l] ? confirmed : mt.sum_newest[l];
        for (int f = mt.compared_upto[l] + 1; f <= upto; f++) {
            const int i = (unsigned) f % SUMS;
            if (mt.sums[l][i].frame != f) {
                continue;
            }
            uint64_t ours;
            if (tgg_frame_checksum(f, &ours) != 0) {
                continue;
            }
            mt.sums_compared++;
            if (ours == mt.sums[l][i].sum) {
                continue;
            }
            mt.desyncs++;
            if (mt.desync_frame >= 0) {
                continue;
            }
            mt.desync_frame = f;
            tgg_log("desync: frame %d's checksum is %016llx here, %016llx on side %d", f,
                    (unsigned long long) ours, (unsigned long long) mt.sums[l][i].sum,
                    link_side(l));
            char diff[200];
            session_display_difference(l, diff, sizeof diff);
            if (diff[0] != '\0') {
                tgg_log("desync: display mods only one game has, the likely cause: %s", diff);
                snprintf(ss.desync, sizeof ss.desync,
                         "The games stopped matching. Mods only one of you has: %s.", diff);
            } else {
                snprintf(ss.desync, sizeof ss.desync, "The games stopped matching.");
            }
        }
        if (upto > mt.compared_upto[l]) {
            mt.compared_upto[l] = upto;
        }
    }
}

/* ---- The frame loop */

/* This game's inputs up to frame `upto` that the controller doesn't give:
 * the first delay frames, and the replay test's */
static void fill_local(int upto)
{
    const int me = ss.side;
    while (mt.last[me] < upto) {
        const int f = mt.last[me] + 1;
        WireInput in = wire_neutral();
        if (ss.replay) {
            in = replaytest_input(mt.ports[me], f);
        } else if (f >= ss.delay) {
            /* Live play records the controller in pad_read */
            break;
        }
        put(mt.in[me], f, &in);
        mt.last[me] = f;
    }
}

static void hold(tgg_advance* adv, int frame)
{
    adv->hold = 1;
    mt.holds++;
    if (session_silence_ms() > SILENCE_MS) {
        char why[120];
        snprintf(why, sizeof why, "Nothing from another game for 10 seconds (frame %d).", frame);
        session_fail(why);
        adv->hold = 0;
    }
}

/* The newest frame every other side's input is known up to */
static int others_known(void)
{
    int known = INT_MAX;
    for (int s = 0; s < mt.players; s++) {
        if (s != ss.side && mt.last[s] < known) {
            known = mt.last[s];
        }
    }
    return known;
}

static void on_advance(tgg_advance* adv)
{
    if (ss.state == NETPLAY_OFF || ss.state == NETPLAY_FAILED) {
        return;
    }
    if (ss.env && !ss.locked) {
        /* Before the boot's match: hold until the session is on (the frame
         * event says it) */
        if (adv->frame < 0) {
            adv->hold = 1;
        }
        return;
    }
    if (ss.env && ss.state == NETPLAY_CONNECTED && mt.pending) {
        boot_ports();
        ss.state = NETPLAY_PLAYING;
        tgg_log("match 1: %d games, side %d plays port %d", mt.players, ss.side,
                mt.ports[ss.side] + 1);
    }
    if (ss.state != NETPLAY_PLAYING) {
        return;
    }
    session_receive();
    if (ss.state != NETPLAY_PLAYING || mt.abandoned) {
        return;
    }
    if (adv->frame < 0 || !mt.live) {
        /* Loading the match: keep in touch, and say what a joiner may still
         * need to start it */
        session_send_lobby(0);
        return;
    }
    const int frame = adv->frame;
    for (int l = 0; l < net_peers(); l++) {
        if (ss.link[l].greeted && !mt.link_in[l]) {
            /* The first frame waits for every link to reach the match */
            session_send_lobby(0);
            send_all_inputs();
            hold(adv, frame);
            return;
        }
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
    /* Confirmed: every input known and run on */
    const int known = others_known();
    const int confirmed = known < frame - 1 ? known : frame - 1;
    if (confirmed > mt.confirmed) {
        mt.confirmed = confirmed;
        tgg_rollback_confirm(confirmed);
    }
    mt.sum_ready = confirmed_before;
    compare_sums(confirmed_before);

    /* Hold when this frame would guess further ahead than a rollback reaches,
     * or when this game is ahead of one it talks to */
    int wait = frame - known > ss.max_rollback;
    if (!wait) {
        const double frame_ms = 1000.0 / 60.0;
        double slowest = 1e18;
        for (int l = 0; l < net_peers(); l++) {
            if (!ss.link[l].greeted || mt.link_frame[l] <= 0) {
                continue;
            }
            const double now = mt.link_frame[l] +
                               ((net_now_ms() - mt.link_frame_ms[l]) + ss.link[l].rtt_ms / 2) /
                                   frame_ms;
            if (now < slowest) {
                slowest = now;
            }
        }
        if (slowest < 1e17 && frame - slowest > 1.0 && frame - mt.last_sync_hold >= 10) {
            mt.last_sync_hold = frame;
            wait = 1;
        }
    }
    if (wait) {
        hold(adv, frame);
    }
    send_all_inputs();
}
TGG_ON(advance, on_advance);

/* The match's setup: its seed, and a hash of the setup to check the other
 * games have the same one */
static void on_setup(const StartMeleeData* data)
{
    if (!mt.pending || (ss.state != NETPLAY_PLAYING && !ss.env)) {
        return;
    }
    mt.pending = 0;
    mt.live = 1;
    /* match_seed gives the same seed: the port starts the first frame from it
     * too, whatever the setup drew from this game's save */
    mt.seed = ss.env ? ss.seed : mt.seed;
    *HSD_RandSeedPtr = mt.seed;
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

/* Each run of each frame: every side's input */
static void on_pads(tgg_pads* pads)
{
    const int frame = tgg_frame();
    if (!mt.live || frame < 0 || ss.state != NETPLAY_PLAYING) {
        return;
    }
    const int me = ss.side;
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
        if (!have(mt.in[me], frame + ss.delay)) {
            put(mt.in[me], frame + ss.delay, &in);
            mt.last[me] = frame + ss.delay;
        }
    }
    int played[4] = { 0 };
    for (int s = 0; s < mt.players; s++) {
        WireInput in;
        if (have(mt.in[s], frame)) {
            in = at(mt.in[s], frame)->in;
        } else if (s != me && mt.last[s] >= 0) {
            in = at(mt.in[s], mt.last[s])->in;
        } else {
            in = wire_neutral();
        }
        if (s != me) {
            put(mt.used[s], frame, &in);
        }
        const int port = mt.ports[s];
        wire_to_pad(&in, &pads->status[port], &pads->raw[port]);
        played[port] = 1;
    }
    /* The ports nobody plays are empty on every machine */
    for (int port = 0; port < 4; port++) {
        if (!played[port]) {
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
    char result[200] = "";
    size_t used = 0;
    for (int s = 0; s < mt.players; s++) {
        const int port = mt.ports[s];
        const int w = snprintf(result + used, sizeof result - used, "%sP%d %d stocks left",
                               s != 0 ? ", " : "", port + 1, Player_GetStocks(port));
        used = w > 0 && used + (size_t) w < sizeof result ? used + (size_t) w : used;
    }
    uint64_t sum = 0;
    const int last = end - 1;
    const int have_sum = tgg_frame_checksum(last, &sum) == 0;
    double rtt = 0;
    for (int l = 0; l < NET_PEERS; l++) {
        rtt = ss.link[l].rtt_ms > rtt ? ss.link[l].rtt_ms : rtt;
    }
    tgg_log("match %u %s at frame %d: %s; frame %d's checksum %016llx%s", mt.number,
            ended ? "over" : "left", end, result, last, (unsigned long long) sum,
            have_sum ? "" : " (not kept)");
    tgg_log("match %u: %u rollbacks (%u frames, deepest %u), %u holds, rtt %.0f ms", mt.number,
            mt.rollbacks, mt.rolled_frames, mt.max_depth, mt.holds, rtt);
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

/* Whether a link still lacks inputs this game sends it */
static int links_lack(void)
{
    for (int l = 0; l < net_peers(); l++) {
        for (int s = 0; s < mt.players && ss.link[l].greeted; s++) {
            if (link_needs(l, s) && mt.acked[l][s] < mt.last[s]) {
                return 1;
            }
        }
    }
    return 0;
}

void match_linger(void)
{
    if (mt.number != 0 && (mt.over || mt.abandoned) && ss.state != NETPLAY_FAILED &&
        links_lack())
    {
        static double last;
        if (net_now_ms() - last >= 30) {
            last = net_now_ms();
            send_all_inputs();
        }
    }
}

void match_linger_until_done(void)
{
    if (mt.live && !mt.over) {
        report(0);
    }
    const double until = net_now_ms() + 3000;
    while (net_now_ms() < until && ss.state != NETPLAY_FAILED && mt.number != 0 && links_lack()) {
        session_receive();
        send_all_inputs();
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

/* From the first frame the games disagree on, the match says so on screen: it
 * plays on, but it isn't the same match on every game any more */
static void on_hud(void)
{
    if (!mt.live || mt.desync_frame < 0) {
        return;
    }
    const tgg_rect b = tgg_draw_bounds();
    const char* text = "The games stopped matching. This match won't count.";
    tgg_draw_rect(b.x, b.y + 8, b.w, 34, TGG_RGBA(0, 0, 0, 170));
    tgg_draw_text(b.x + b.w / 2, b.y + 14, 22, TGG_RGBA(255, 210, 90, 255), TGG_ALIGN_CENTER, text);
}
TGG_ON(hud_draw, on_hud);
