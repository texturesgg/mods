/* The session: two games find each other, check they can play together, keep
 * in touch between matches, pass messages, and agree on each match. The API
 * other mods call (include/netplay.h) is here.
 *
 *   - Hello: each side says hello every 100 ms until the other has its hello
 *     (the build, the mod-set id, the side). Side 0 with no peer address
 *     plays the first game whose hello arrives.
 *   - Between matches, a lobby packet goes out every 100 ms: the round trip,
 *     every message the other side hasn't acknowledged, and which of the
 *     last match's inputs this side has.
 *   - Messages are numbered; a receiver takes them in order and says which
 *     it has, and the sender sends the rest again until it does.
 *   - A match: side 0's netplay_play sends it (its number, seed and setup, as
 *     the console lays a StartMeleeData out); side 1's says it's ready. Each
 *     side starts once it has both.
 *
 * The environment can open a session too (TGG_NETPLAY_PEER), for a match the
 * boot sets up (TGG_BOOT_MATCH) and the replay test. */
#include "internal.h"
#include "netplay.h"

#include "tgg/control.h"
#include "tgg/events.h"
#include "tgg/rollback.h"
#include "tgg/settings.h"

#include <melee/mn/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Session ss;

int setting_delay;
TGG_SETTING_INT_LOCAL(setting_delay, "delay", "Input delay (frames)", 2, 0, 15);
int setting_controller;
TGG_SETTING_CHOICE_LOCAL(setting_controller, "controller", "Your controller", 0, "Any", "Port 1",
                   "Port 2", "Port 3", "Port 4");

#define OUT_MAX 32
#define IN_MAX 32
/* How long the other side may say nothing before the session ends */
#define SILENCE_MS 10000

static struct {
    WireMsg out[OUT_MAX]; /* sent, not yet acknowledged, oldest first */
    int n_out;
    uint32_t next_seq;    /* the next message's number */
    uint32_t in_next;     /* the next message number expected */
    WireMsg in[IN_MAX];   /* netplay_send's from the other side, not yet read */
    int in_first, n_in;

    /* The next match */
    int playing_next;     /* this side called netplay_play */
    int peer_ready;       /* side 1 said it's ready (side 0) */
    int have_match;       /* side 0's match arrived (side 1) */
    uint32_t number, seed;
    uint8_t console[TGG_CONSOLE_MATCH_SIZE];
    uint32_t last_number; /* the last match started */
} sn;

static void reset(void)
{
    memset(&ss, 0, sizeof ss);
    memset(&sn, 0, sizeof sn);
    sn.next_seq = 1;
    sn.in_next = 1;
}

uint32_t session_clock(void)
{
    return (uint32_t) (net_now_ms() - ss.opened_ms) + 1;
}

void session_got_ping(uint32_t ping)
{
    if (ping > ss.peer_ping) {
        ss.peer_ping = ping;
        ss.peer_ping_ms = net_now_ms();
    }
}

uint16_t session_pong_age(void)
{
    const double age = ss.peer_ping == 0 ? 0 : net_now_ms() - ss.peer_ping_ms;
    return (uint16_t) (age < 65535 ? age : 65535);
}

void session_got_pong(uint32_t pong, uint16_t age)
{
    if (pong == 0) {
        return;
    }
    const double rtt = (double) session_clock() - (double) pong - age;
    if (rtt >= 0 && rtt < 5000) {
        ss.rtt_ms = ss.rtt_ms == 0 ? rtt : ss.rtt_ms * 0.9 + rtt * 0.1;
    }
}

void session_send_packet(const WirePacket* p)
{
    uint8_t buf[WIRE_MAX_BYTES];
    const size_t len = wire_encode(p, buf);
    net_send(buf, len);
}

static void send_hello(void)
{
    WirePacket p;
    memset(&p, 0, sizeof p);
    p.type = WIRE_HELLO;
    snprintf(p.u.hello.build, sizeof p.u.hello.build, "%s", tgg_game_build());
    snprintf(p.u.hello.mod_set, sizeof p.u.hello.mod_set, "%s", tgg_mod_set_id());
    snprintf(p.u.hello.versions, sizeof p.u.hello.versions, "%s", tgg_mod_set_versions());
    snprintf(p.u.hello.target, sizeof p.u.hello.target, "%s", TGG_GAME_TARGET);
    p.u.hello.side = (uint8_t) ss.side;
    p.u.hello.delay = (uint8_t) ss.delay;
    p.u.hello.heard = (uint8_t) ss.greeted;
    p.u.hello.seed = ss.seed;
    p.u.hello.replay = ss.replay ? replaytest_hash() : 0;
    session_send_packet(&p);
}

void session_send_lobby(int force)
{
    const double now = net_now_ms();
    if (!force && now - ss.last_lobby_ms < 100) {
        return;
    }
    ss.last_lobby_ms = now;
    WirePacket p;
    memset(&p, 0, sizeof p);
    p.type = WIRE_LOBBY;
    WireLobby* l = &p.u.lobby;
    l->ping = session_clock();
    l->pong = ss.peer_ping;
    l->pong_age = session_pong_age();
    l->msg_ack = sn.in_next - 1;
    l->number = match_number();
    l->input_ack = match_input_ack();
    for (int i = 0; i < sn.n_out && l->count < WIRE_MAX_MSGS; i++) {
        l->msgs[l->count++] = sn.out[i];
    }
    session_send_packet(&p);
}

static void send_bye(void)
{
    WirePacket p;
    memset(&p, 0, sizeof p);
    p.type = WIRE_BYE;
    for (int i = 0; i < 3; i++) {
        session_send_packet(&p);
    }
    const double until = net_now_ms() + 100;
    while (net_now_ms() < until) {
        fakenet_flush();
        net_wait_ms(5);
        /* The session is over: what arrives now goes unread */
        uint8_t buf[WIRE_MAX_BYTES];
        while (net_recv(buf, sizeof buf) != 0) {
        }
    }
}

static void stop_env(const char* why, int code)
{
    tgg_log("%s", why);
    tgg_log("stopping: the two games can't play together");
    fflush(stdout);
    fflush(stderr);
    _Exit(code);
}

void session_fail(const char* why)
{
    if (ss.state == NETPLAY_OFF || ss.state == NETPLAY_FAILED) {
        return;
    }
    tgg_log("%s", why);
    if (ss.env) {
        stop_env(why, 4);
    }
    if (ss.state == NETPLAY_PLAYING) {
        match_abandon();
    }
    send_bye();
    net_close();
    ss.state = NETPLAY_FAILED;
    snprintf(ss.why, sizeof ss.why, "%s", why);
}

static int queue(uint8_t kind, const void* data, int len)
{
    if (len < 0 || len > WIRE_MSG_MAX || sn.n_out == OUT_MAX) {
        return -1;
    }
    WireMsg* m = &sn.out[sn.n_out++];
    memset(m, 0, sizeof *m);
    m->seq = sn.next_seq++;
    m->kind = kind;
    m->len = (uint16_t) len;
    memcpy(m->data, data, (size_t) len);
    session_send_lobby(1);
    return 0;
}

/* ---- Receiving */

static void put_be32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

static uint32_t get_be32(const uint8_t* p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

static void got_msg(const WireMsg* m)
{
    if (m->kind == MSG_USER) {
        if (sn.n_in < IN_MAX) {
            sn.in[(sn.in_first + sn.n_in++) % IN_MAX] = *m;
        }
    } else if (m->kind == MSG_MATCH && ss.side == 1 && m->len == 8 + TGG_CONSOLE_MATCH_SIZE) {
        sn.have_match = 1;
        sn.number = get_be32(m->data);
        sn.seed = get_be32(m->data + 4);
        memcpy(sn.console, m->data + 8, TGG_CONSOLE_MATCH_SIZE);
    } else if (m->kind == MSG_READY && ss.side == 0) {
        sn.peer_ready = 1;
    }
}

/* "tgg-melee <version>" of a build "tgg-melee <version> <layout id>" */
static void game_version(const char* build, char* out, size_t cap)
{
    snprintf(out, cap, "%s", build);
    char* last = strrchr(out, ' ');
    if (last != NULL) {
        *last = '\0';
    }
}

static int got_hello(const WireHello* h)
{
    char why[320];
    if (h->side == ss.side) {
        snprintf(why, sizeof why, "Both games are side %d: one hosts, the other joins.", ss.side);
        session_fail(why);
        return 0;
    }
    /* On one platform the builds and the mods' libraries must be the same. Across
     * platforms they can't be: the game's version and the mods' versions must
     * be, and the checksums show if they play differently anyway */
    const int same_target = strcmp(h->target, TGG_GAME_TARGET) == 0;
    char ours[96], theirs[96];
    game_version(tgg_game_build(), ours, sizeof ours);
    game_version(h->build, theirs, sizeof theirs);
    if (same_target ? strcmp(h->build, tgg_game_build()) != 0 : strcmp(ours, theirs) != 0) {
        snprintf(why, sizeof why, "The other game is another version: %s here, %s there.",
                 same_target ? tgg_game_build() : ours, same_target ? h->build : theirs);
        session_fail(why);
        return 0;
    }
    const char* mine = same_target ? tgg_mod_set_id() : tgg_mod_set_versions();
    const char* other = same_target ? h->mod_set : h->versions;
    if (strcmp(other, mine) != 0) {
        tgg_log("this game's mods: %s", tgg_mod_set_names());
        snprintf(why, sizeof why,
                 "The other game has other mods (mod set %s here, %s there). The log lists "
                 "this game's.", mine[0] != '\0' ? mine : "none", other[0] != '\0' ? other : "none");
        session_fail(why);
        return 0;
    }
    if (h->replay != (ss.replay ? replaytest_hash() : 0)) {
        session_fail("The two games play different replays.");
        return 0;
    }
    if (ss.side == 1) {
        ss.seed = h->seed;
    }
    if (!net_has_peer()) {
        net_adopt();
    }
    if (!ss.greeted) {
        ss.greeted = 1;
        tgg_log("%s said hello: same build and mods (%s: %s)", net_peer_name(),
                tgg_mod_set_id()[0] != '\0' ? tgg_mod_set_id() : "none", tgg_mod_set_names());
        if (!same_target) {
            tgg_log("the other game runs on %s: its mods are compared by version, not by build",
                    h->target);
        }
        send_hello();
    }
    if (h->heard) {
        ss.heard = 1;
    }
    return 1;
}

static void got_lobby(const WireLobby* l)
{
    ss.heard = 1;
    session_got_ping(l->ping);
    session_got_pong(l->pong, l->pong_age);
    int kept = 0;
    for (int i = 0; i < sn.n_out; i++) {
        if (sn.out[i].seq > l->msg_ack) {
            sn.out[kept++] = sn.out[i];
        }
    }
    sn.n_out = kept;
    match_got_ack(l->number, l->input_ack);
    for (int i = 0; i < l->count; i++) {
        if (l->msgs[i].seq == sn.in_next) {
            sn.in_next++;
            got_msg(&l->msgs[i]);
        }
    }
}

void session_receive(void)
{
    if (ss.state == NETPLAY_OFF || ss.state == NETPLAY_FAILED) {
        return;
    }
    fakenet_flush();
    uint8_t buf[WIRE_MAX_BYTES];
    size_t len;
    while (ss.state != NETPLAY_FAILED && (len = net_recv(buf, sizeof buf)) != 0) {
        WirePacket p;
        if (!wire_decode(buf, len, &p)) {
            continue;
        }
        if (p.type == WIRE_HELLO) {
            if (!got_hello(&p.u.hello)) {
                continue;
            }
        } else if (!net_has_peer() || !ss.greeted) {
            continue;
        } else if (p.type == WIRE_INPUT) {
            ss.heard = 1;
            match_got_inputs(&p.u.inputs);
        } else if (p.type == WIRE_LOBBY) {
            got_lobby(&p.u.lobby);
        } else if (p.type == WIRE_BYE) {
            session_fail(ss.state == NETPLAY_PLAYING ? "The other player left the match."
                                                     : "The other player left.");
            continue;
        }
        ss.last_heard_ms = net_now_ms();
    }
    if ((ss.state == NETPLAY_WAITING || ss.state == NETPLAY_CONNECTING) && ss.greeted &&
        ss.heard)
    {
        ss.state = NETPLAY_CONNECTED;
        ss.last_heard_ms = net_now_ms();
        tgg_log("connected to %s as side %d", net_peer_name(), ss.side);
    }
}

/* ---- Starting a match */

static void maybe_start(void)
{
    if (ss.state != NETPLAY_STARTING) {
        return;
    }
    const int go = ss.side == 0 ? sn.peer_ready : sn.have_match;
    if (!go) {
        return;
    }
    static StartMeleeData data;
    memset(&data, 0, sizeof data);
    tgg_match_from_console(&data, sn.console);
    sn.playing_next = 0;
    sn.peer_ready = 0;
    sn.have_match = 0;
    sn.last_number = sn.number;
    if (match_begin(sn.number, sn.seed, &data) != 0) {
        session_fail("There's no screen to start the match from.");
    }
}

void session_match_over(void)
{
    ss.matches++;
    if (ss.state == NETPLAY_PLAYING) {
        ss.state = NETPLAY_CONNECTED;
        /* match_end is simulation code, which can't read the clock: the next
         * frame starts the silence from then */
        ss.last_heard_ms = -1;
    }
}

/* Each displayed frame outside a match: the network */
static void on_frame(void)
{
    if (ss.state == NETPLAY_OFF || ss.state == NETPLAY_FAILED || ss.state == NETPLAY_PLAYING) {
        return;
    }
    session_receive();
    if (ss.state == NETPLAY_FAILED) {
        return;
    }
    const double now = net_now_ms();
    if (ss.last_heard_ms < 0) {
        ss.last_heard_ms = now;
    }
    if (ss.state == NETPLAY_WAITING || ss.state == NETPLAY_CONNECTING || !ss.heard) {
        if (net_has_peer() && now - ss.last_hello_ms > 100) {
            ss.last_hello_ms = now;
            send_hello();
        }
        return;
    }
    if (now - ss.last_heard_ms > SILENCE_MS) {
        session_fail("Lost the connection to the other game.");
        return;
    }
    session_send_lobby(0);
    match_linger();
    maybe_start();
}
TGG_ON(frame, on_frame);

/* ---- The API */

static int open_session(int side, const char* peer, int port, const char** why)
{
    static char reason[200];
    if (ss.state != NETPLAY_OFF) {
        netplay_close();
    }
    reset();
    ss.side = side != 0;
    ss.delay = setting_delay;
    const char* delay = getenv("TGG_NETPLAY_DELAY");
    if (delay != NULL && delay[0] != '\0') {
        ss.delay = atoi(delay);
    }
    if (ss.delay < 0 || ss.delay > 15) {
        ss.delay = 2;
    }
    ss.max_rollback = 8;
    const char* rollback = getenv("TGG_NETPLAY_ROLLBACK");
    if (rollback != NULL && rollback[0] != '\0') {
        ss.max_rollback = atoi(rollback);
    }
    if (ss.max_rollback < 1 || ss.max_rollback > TGG_ROLLBACK_MAX) {
        ss.max_rollback = 8;
    }
    if (side == 1 && peer == NULL) {
        snprintf(reason, sizeof reason, "The joining side needs the host's address.");
        if (why != NULL) {
            *why = reason;
        }
        return -1;
    }
    const char* failed = net_open(port, peer);
    if (failed != NULL) {
        snprintf(reason, sizeof reason, "%s", failed);
        reason[0] = (char) (reason[0] >= 'a' && reason[0] <= 'z' ? reason[0] - 32 : reason[0]);
        if (why != NULL) {
            *why = reason;
        }
        tgg_log("%s", failed);
        return -1;
    }
    fakenet_init(getenv("TGG_NETPLAY_FAKE"));
    /* Side 0's seed starts a match the boot sets up; side 1 takes it from the
     * hello */
    FILE* f = fopen("/dev/urandom", "rb");
    if (f == NULL || fread(&ss.seed, sizeof ss.seed, 1, f) != 1) {
        ss.seed = (uint32_t) net_now_ms();
    }
    if (f != NULL) {
        fclose(f);
    }
    tgg_rollback_init(ss.max_rollback);
    ss.opened_ms = net_now_ms();
    ss.last_heard_ms = ss.opened_ms;
    ss.state = peer == NULL ? NETPLAY_WAITING : NETPLAY_CONNECTING;
    if (peer != NULL) {
        tgg_log("side %d: saying hello to %s from port %d; %d frames of delay, rolls back up "
                "to %d", ss.side, peer, port, ss.delay, ss.max_rollback);
    } else {
        tgg_log("side %d: waiting on port %d; %d frames of delay, rolls back up to %d", ss.side,
                port, ss.delay, ss.max_rollback);
    }
    return 0;
}

int netplay_open(int side, const char* peer, int port, const char** why)
{
    return open_session(side, peer, port, why);
}

void netplay_close(void)
{
    if (ss.state == NETPLAY_OFF) {
        return;
    }
    if (ss.state != NETPLAY_FAILED) {
        send_bye();
        net_close();
    }
    if (ss.state == NETPLAY_PLAYING) {
        match_abandon();
    }
    reset();
    tgg_rollback_init(0);
}

void netplay_status(netplay_info* out)
{
    memset(out, 0, sizeof *out);
    out->state = ss.state;
    out->side = ss.side;
    snprintf(out->peer, sizeof out->peer, "%s", net_peer_name());
    out->rtt_ms = (float) ss.rtt_ms;
    out->matches = ss.matches;
    snprintf(out->why, sizeof out->why, "%s", ss.why);
}

int netplay_send(const void* data, int len)
{
    if (ss.state != NETPLAY_CONNECTED && ss.state != NETPLAY_STARTING) {
        return -1;
    }
    return queue(MSG_USER, data, len);
}

int netplay_receive(void* buf, int cap)
{
    if (sn.n_in == 0) {
        return 0;
    }
    const WireMsg* m = &sn.in[sn.in_first];
    sn.in_first = (sn.in_first + 1) % IN_MAX;
    sn.n_in--;
    const int n = m->len < cap ? m->len : cap;
    if (n > 0) {
        memcpy(buf, m->data, (size_t) n);
    }
    return n;
}

int netplay_play(const StartMeleeData* data)
{
    if (ss.state != NETPLAY_CONNECTED) {
        return -1;
    }
    if (ss.side == 0) {
        int ports[2];
        if (data == NULL || match_ports(data, ports) != 0) {
            return -1;
        }
        uint8_t msg[8 + TGG_CONSOLE_MATCH_SIZE];
        sn.number = sn.last_number + 1;
        FILE* f = fopen("/dev/urandom", "rb");
        if (f == NULL || fread(&sn.seed, sizeof sn.seed, 1, f) != 1) {
            sn.seed = (uint32_t) net_now_ms() * 2654435761u;
        }
        if (f != NULL) {
            fclose(f);
        }
        tgg_match_to_console(data, sn.console);
        put_be32(msg, sn.number);
        put_be32(msg + 4, sn.seed);
        memcpy(msg + 8, sn.console, TGG_CONSOLE_MATCH_SIZE);
        if (queue(MSG_MATCH, msg, (int) sizeof msg) != 0) {
            return -1;
        }
    } else if (queue(MSG_READY, "", 0) != 0) {
        return -1;
    }
    sn.playing_next = 1;
    ss.state = NETPLAY_STARTING;
    maybe_start();
    return 0;
}

/* ---- The environment: TGG_NETPLAY_PEER and the rest */

static int env_int(const char* name, int fallback)
{
    const char* v = getenv(name);
    return v != NULL && v[0] != '\0' ? atoi(v) : fallback;
}

static void init(void)
{
    const char* peer = getenv("TGG_NETPLAY_PEER");
    if (peer == NULL || peer[0] == '\0') {
        return;
    }
    const int side = env_int("TGG_NETPLAY_SIDE", 0) != 0;
    const char* replay = getenv("TGG_NETPLAY_REPLAY");
    const int is_replay = replay != NULL && replay[0] != '\0';
    if (is_replay && !replaytest_open(replay)) {
        fflush(stdout);
        fflush(stderr);
        _Exit(3);
    }
    const char* why = NULL;
    if (open_session(side, peer, env_int("TGG_NETPLAY_PORT", 51000), &why) != 0) {
        /* Asked for netplay and can't: playing on alone would only mislead */
        stop_env(why, 3);
    }
    ss.env = 1;
    ss.replay = is_replay;
    if (is_replay) {
        ss.seed = replaytest_start_seed();
    }
    match_expect_boot();
}
TGG_INIT(init);

/* Before the game exits: tell the other side */
static void on_quit(void)
{
    if (ss.state != NETPLAY_OFF && ss.state != NETPLAY_FAILED) {
        match_linger_until_done();
        send_bye();
        net_close();
    }
}
TGG_ON(quit, on_quit);
