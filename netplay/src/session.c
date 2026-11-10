/* The session: two to four games find each other, check they can play
 * together, keep in touch between matches, pass messages, and agree on each
 * match. The API other mods call (include/netplay.h) is here.
 *
 * The games meet in a star: each joiner talks to the host only, and the host
 * passes on to the others what one joiner sends (its messages, and its inputs
 * in match.c). The host's link L is the joiner playing side L + 1.
 *
 *   - Hello: a joiner says hello to the host every 100 ms until the host has
 *     its hello (the build, the mod-set id). The host takes each game whose
 *     hello arrives with the same build and mods, gives it the next side in
 *     its hello back, and tells every game how many are in (MSG_ROSTER).
 *   - A joiner whose game differs from the host's asks for the host's
 *     profile (NETPLAY_MISMATCH) and gets it in pieces; the host answers and
 *     keeps waiting.
 *   - Locking: the host stops taking games (netplay_lock, or its first
 *     netplay_play) and tells the others; every game then provides the port's
 *     session (tgg/session.h) with that many players.
 *   - Between matches, a lobby packet goes out on each link every 100 ms: the
 *     round trip, every message the other end hasn't acknowledged, and which
 *     of the last match's inputs this game has.
 *   - Messages are numbered on each link; a receiver takes them in order and
 *     says which it has, and the sender sends the rest again until it does.
 *   - A match: the host's netplay_play sends it to every joiner (its number,
 *     seed and setup, as the console lays a StartMeleeData out); each
 *     joiner's says it's ready. A joiner starts once it has the match, the
 *     host once every joiner is ready.
 *
 * The environment can open a session too (TGG_NETPLAY_PEER, or
 * TGG_NETPLAY_GAMES for a host that waits for that many games), for a match
 * the boot sets up (TGG_BOOT_MATCH) and the replay test. */
#include "internal.h"
#include "netplay.h"

#include "tgg/control.h"
#include "tgg/events.h"
#include "tgg/net.h"
#include "tgg/rollback.h"
#include "tgg/session.h"
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
int setting_strict;
TGG_SETTING_CHOICE_LOCAL(setting_strict, "match", "When hosting, mods that must match", 0,
                         "Gameplay mods", "All mods");

/* Messages waiting to be acknowledged, per link: a shared screen sends one
 * menu input per player per frame, and the host passes each on */
#define OUT_MAX 256
#define IN_MAX 64
/* How long another game may say nothing before the session ends */
#define SILENCE_MS 10000

/* Messages on one link */
typedef struct Queue {
    WireMsg out[OUT_MAX]; /* sent, not yet acknowledged, oldest first */
    int n_out;
    uint32_t next_seq;    /* the next message's number */
    uint32_t in_next;     /* the next message number expected */
} Queue;

static struct {
    Queue q[NET_PEERS];
    WireMsg in[IN_MAX];   /* netplay_send's from the other games, not yet read */
    int in_first, n_in;

    /* The next match */
    int ready[NET_PEERS]; /* the host: each joiner said it's ready */
    int have_match;       /* a joiner: the host's match arrived */
    uint32_t number, seed;
    uint8_t console[TGG_CONSOLE_MATCH_SIZE];
    uint32_t last_number; /* the last match started */
} sn;

/* NETPLAY_MISMATCH: the host's profile as it arrives */
static struct {
    char data[WIRE_PROFILE_MAX + 1];
    uint32_t total;
    uint32_t have;
    int whole;
    double last_ask_ms, last_got_ms;
} pf;

static int hosting(void)
{
    return ss.side == 0;
}

int link_side(int link)
{
    return hosting() ? link + 1 : 0;
}

int side_link(int side)
{
    return hosting() ? side - 1 : 0;
}

/* Games that met this one: the host's joiners, or a joiner's host */
static int links_greeted(void)
{
    int n = 0;
    for (int l = 0; l < net_peers(); l++) {
        n += ss.link[l].greeted;
    }
    return n;
}

static void reset(void)
{
    tgg_session_provide(NULL);
    memset(&ss, 0, sizeof ss);
    memset(&sn, 0, sizeof sn);
    memset(&pf, 0, sizeof pf);
    for (int l = 0; l < NET_PEERS; l++) {
        sn.q[l].next_seq = 1;
        sn.q[l].in_next = 1;
    }
}

uint32_t session_clock(void)
{
    return (uint32_t) (net_now_ms() - ss.opened_ms) + 1;
}

void session_got_ping(int link, uint32_t ping)
{
    Link* k = &ss.link[link];
    if (ping > k->peer_ping) {
        k->peer_ping = ping;
        k->peer_ping_ms = net_now_ms();
    }
}

uint16_t session_pong_age(int link)
{
    const Link* k = &ss.link[link];
    const double age = k->peer_ping == 0 ? 0 : net_now_ms() - k->peer_ping_ms;
    return (uint16_t) (age < 65535 ? age : 65535);
}

void session_got_pong(int link, uint32_t pong, uint16_t age)
{
    if (pong == 0) {
        return;
    }
    Link* k = &ss.link[link];
    const double rtt = (double) session_clock() - (double) pong - age;
    if (rtt >= 0 && rtt < 5000) {
        k->rtt_ms = k->rtt_ms == 0 ? rtt : k->rtt_ms * 0.9 + rtt * 0.1;
    }
}

double session_silence_ms(void)
{
    double longest = 0;
    const double now = net_now_ms();
    for (int l = 0; l < net_peers(); l++) {
        if (ss.link[l].greeted && ss.link[l].last_heard_ms >= 0 &&
            now - ss.link[l].last_heard_ms > longest)
        {
            longest = now - ss.link[l].last_heard_ms;
        }
    }
    return longest;
}

void session_send_packet(int link, const WirePacket* p)
{
    uint8_t buf[WIRE_MAX_BYTES];
    const size_t len = wire_encode(p, buf);
    net_send(link, buf, len);
}

/* A hello that says who this game is */
static void hello_packet(WirePacket* p)
{
    memset(p, 0, sizeof *p);
    p->type = WIRE_HELLO;
    snprintf(p->u.hello.build, sizeof p->u.hello.build, "%s", tgg_game_build());
    snprintf(p->u.hello.mod_set, sizeof p->u.hello.mod_set, "%s", tgg_mod_set_id());
    snprintf(p->u.hello.versions, sizeof p->u.hello.versions, "%s", tgg_mod_set_versions());
    snprintf(p->u.hello.target, sizeof p->u.hello.target, "%s", TGG_GAME_TARGET);
    p->u.hello.host = (uint8_t) hosting();
    snprintf(p->u.hello.all, sizeof p->u.hello.all, "%s", tgg_mod_set_all());
    snprintf(p->u.hello.display, sizeof p->u.hello.display, "%s", tgg_mod_set_display());
    p->u.hello.strict = (uint8_t) (hosting() && setting_strict == 1);
    p->u.hello.token = ss.token;
    p->u.hello.game = ss.game;
}

static void send_hello(int link)
{
    WirePacket p;
    hello_packet(&p);
    p.u.hello.assign = (uint8_t) (hosting() ? link_side(link) : 0);
    p.u.hello.delay = (uint8_t) ss.delay;
    p.u.hello.heard = (uint8_t) ss.link[link].greeted;
    p.u.hello.seed = ss.seed;
    p.u.hello.replay = ss.replay ? replaytest_hash() : 0;
    session_send_packet(link, &p);
}

static void send_lobby(int link)
{
    Link* k = &ss.link[link];
    const Queue* q = &sn.q[link];
    k->last_lobby_ms = net_now_ms();
    WirePacket p;
    memset(&p, 0, sizeof p);
    p.type = WIRE_LOBBY;
    WireLobby* l = &p.u.lobby;
    l->ping = session_clock();
    l->pong = k->peer_ping;
    l->pong_age = session_pong_age(link);
    l->msg_ack = q->in_next - 1;
    l->number = match_number();
    match_input_acks(l->input_ack);
    size_t bytes = 0;
    for (int i = 0; i < q->n_out && l->count < WIRE_MAX_MSGS; i++) {
        bytes += wire_msg_bytes(&q->out[i]);
        if (bytes > WIRE_MSGS_BYTES) {
            break;
        }
        l->msgs[l->count++] = q->out[i];
    }
    session_send_packet(link, &p);
}

void session_send_lobby(int force)
{
    const double now = net_now_ms();
    for (int l = 0; l < net_peers(); l++) {
        if (ss.link[l].greeted && (force || now - ss.link[l].last_lobby_ms >= 100)) {
            send_lobby(l);
        }
    }
}

static void send_bye(void)
{
    WirePacket p;
    memset(&p, 0, sizeof p);
    p.type = WIRE_BYE;
    for (int i = 0; i < 3; i++) {
        for (int l = 0; l < net_peers(); l++) {
            session_send_packet(l, &p);
        }
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

static void exit_env(int code)
{
    tgg_log("stopping: the games can't play together");
    fflush(stdout);
    fflush(stderr);
    _Exit(code);
}

static void stop_env(const char* why, int code)
{
    tgg_log("%s", why);
    exit_env(code);
}

void session_fail(const char* why)
{
    if (ss.state == NETPLAY_OFF || ss.state == NETPLAY_FAILED) {
        return;
    }
    tgg_log("%s", why);
    tgg_session_provide(NULL);
    if (ss.state == NETPLAY_PLAYING) {
        match_abandon(); /* logs where the match was */
    }
    if (ss.env) {
        exit_env(4);
    }
    send_bye();
    net_close();
    ss.state = NETPLAY_FAILED;
    snprintf(ss.why, sizeof ss.why, "%s", why);
}

/* Set while queuing from simulation code, which can't use the network: the
 * message goes with the link's next lobby packet, due at once */
static int queue_later;

/* Queues a message on `link`, as from side `from`; sends it at once */
static int queue_to(int link, uint8_t kind, int from, const void* data, int len)
{
    Queue* q = &sn.q[link];
    if (len < 0 || len > WIRE_MSG_MAX || q->n_out == OUT_MAX) {
        return -1;
    }
    WireMsg* m = &q->out[q->n_out++];
    memset(m, 0, sizeof *m);
    m->seq = q->next_seq++;
    m->kind = kind;
    m->from = (uint8_t) from;
    m->len = (uint16_t) len;
    memcpy(m->data, data, (size_t) len);
    if (queue_later) {
        ss.link[link].last_lobby_ms = -1e9;
    } else {
        send_lobby(link);
    }
    return 0;
}

/* Queues a message on every link but `except` (-1: none); -1 if any can't
 * take it, before any does */
static int queue_all(uint8_t kind, int from, const void* data, int len, int except)
{
    if (len < 0 || len > WIRE_MSG_MAX) {
        return -1;
    }
    for (int l = 0; l < net_peers(); l++) {
        if (l != except && ss.link[l].greeted && sn.q[l].n_out == OUT_MAX) {
            return -1;
        }
    }
    for (int l = 0; l < net_peers(); l++) {
        if (l != except && ss.link[l].greeted) {
            queue_to(l, kind, from, data, len);
        }
    }
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

/* ---- The session provider (tgg/session.h) ----------------------------------
 * Once the host locks the session, this mod is the port's session: shared
 * screens run in lockstep on the menu inputs every game sends here, and
 * tgg_play_match starts a match through netplay_play. */

#define LOCK_RING 64

static struct {
    uint32_t input[SIDES][LOCK_RING];
    uint32_t tick[SIDES][LOCK_RING]; /* tick + 1, 0 when empty */
} ls;

static int provider_players(void)
{
    return ss.players;
}

static int provider_me(void)
{
    return ss.side;
}

static uint32_t provider_seed(void)
{
    return ss.seed;
}

static void got_input(int side, const uint8_t* data)
{
    const uint32_t tick = get_be32(data);
    const int at = (int) (tick % LOCK_RING);
    ls.input[side][at] = get_be32(data + 4);
    ls.tick[side][at] = tick + 1;
}

static void provider_send_input(uint32_t tick, uint32_t menu)
{
    uint8_t msg[8];
    put_be32(msg, tick);
    put_be32(msg + 4, menu);
    got_input(ss.side, msg);
    if (queue_all(MSG_INPUT, ss.side, msg, (int) sizeof msg, -1) != 0) {
        session_fail("Too much to send to the other games.");
    }
}

static int provider_inputs(uint32_t tick, uint32_t* out)
{
    const int at = (int) (tick % LOCK_RING);
    for (int s = 0; s < ss.players; s++) {
        if (ls.tick[s][at] != tick + 1) {
            return 0;
        }
    }
    for (int s = 0; s < ss.players; s++) {
        out[s] = ls.input[s][at];
    }
    return 1;
}

static int provider_play_match(const StartMeleeData* data)
{
    /* Every game calls it with the same match: the host sends it, the
     * joiners take it */
    return netplay_play(hosting() ? data : NULL);
}

static int provider_post(const void* data, int len)
{
    return queue_all(MSG_POST, ss.side, data, len, -1);
}

/* A seed for a match, from the system */
static uint32_t fresh_seed(void)
{
    uint32_t seed;
    FILE* f = fopen("/dev/urandom", "rb");
    if (f == NULL || fread(&seed, sizeof seed, 1, f) != 1) {
        seed = (uint32_t) net_now_ms() * 2654435761u;
    }
    if (f != NULL) {
        fclose(f);
    }
    return seed;
}

/* The host: its game's menus set up a match, which every game plays */
static int provider_offer_match(const StartMeleeData* data)
{
    int ports[SIDES];
    const int humans = match_ports(data, ports);
    if (!hosting() || ss.state != NETPLAY_CONNECTED || humans != ss.players) {
        tgg_log("can't play the game's match: %s", !hosting() ? "not hosting"
                : ss.state != NETPLAY_CONNECTED ? "not between matches"
                : "it doesn't have one human player for each game");
        tgg_log("(state %d, %d humans for %d games)", ss.state, humans, ss.players);
        return -1;
    }
    uint8_t msg[8 + TGG_CONSOLE_MATCH_SIZE];
    sn.number = sn.last_number + 1;
    sn.seed = fresh_seed();
    put_be32(msg, sn.number);
    put_be32(msg + 4, sn.seed);
    tgg_match_to_console(data, msg + 8);
    /* From the game's VS mode, in simulation code */
    queue_later = 1;
    const int queued = queue_all(MSG_OFFER, 0, msg, (int) sizeof msg, -1);
    queue_later = 0;
    if (queued != 0) {
        return -1;
    }
    sn.last_number = sn.number;
    match_expect(sn.number, sn.seed, data);
    return 0;
}

static const tgg_session_provider provider = {
    .players = provider_players,
    .me = provider_me,
    .seed = provider_seed,
    .send_input = provider_send_input,
    .inputs = provider_inputs,
    .play_match = provider_play_match,
    .post = provider_post,
    .offer_match = provider_offer_match,
};

/* The session's games are settled: provide it */
static void start_session(void)
{
    ss.locked = 1;
    memset(&ls, 0, sizeof ls);
    tgg_log("the session is on: %d games, this one side %d", ss.players, ss.side);
    tgg_session_provide(&provider);
}

/* The host: tells every game how many are in, and whether that's all */
static void send_roster(void)
{
    const uint8_t msg[2] = { (uint8_t) ss.players, (uint8_t) ss.locked };
    queue_all(MSG_ROSTER, 0, msg, (int) sizeof msg, -1);
}

static void got_msg(int link, const WireMsg* m)
{
    /* What a joiner sends, the host passes on to the others, as from it */
    const int from = hosting() ? link_side(link) : m->from;
    if (from < 0 || from >= SIDES) {
        return;
    }
    if (hosting() && (m->kind == MSG_USER || m->kind == MSG_INPUT || m->kind == MSG_POST)) {
        if (queue_all(m->kind, from, m->data, m->len, link) != 0) {
            session_fail("Too much to send to the other games.");
            return;
        }
    }
    if (m->kind == MSG_USER) {
        if (sn.n_in < IN_MAX) {
            WireMsg* in = &sn.in[(sn.in_first + sn.n_in++) % IN_MAX];
            *in = *m;
            in->from = (uint8_t) from;
        }
    } else if (m->kind == MSG_MATCH && !hosting() && m->len == 8 + TGG_CONSOLE_MATCH_SIZE) {
        sn.have_match = 1;
        sn.number = get_be32(m->data);
        sn.seed = get_be32(m->data + 4);
        memcpy(sn.console, m->data + 8, TGG_CONSOLE_MATCH_SIZE);
    } else if (m->kind == MSG_READY && hosting()) {
        sn.ready[link] = 1;
    } else if (m->kind == MSG_INPUT && m->len == 8 && from < ss.players) {
        got_input(from, m->data);
    } else if (m->kind == MSG_ROSTER && !hosting() && m->len == 2) {
        ss.players = m->data[0] <= SIDES ? m->data[0] : SIDES;
        if (m->data[1] && !ss.locked) {
            start_session();
        }
    } else if (m->kind == MSG_POST) {
        tgg_session_posted(from, m->data, m->len);
    } else if (m->kind == MSG_OFFER && !hosting() && m->len == 8 + TGG_CONSOLE_MATCH_SIZE &&
               ss.state == NETPLAY_CONNECTED) {
        static StartMeleeData data;
        memset(&data, 0, sizeof data);
        tgg_match_from_console(&data, m->data + 8);
        sn.last_number = get_be32(m->data);
        match_expect(sn.last_number, get_be32(m->data + 4), &data);
        tgg_session_match_offered(&data);
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

/* Whether "id version" is in the list "a 1.0, b 2.0" */
static int listed(const char* list, const char* entry, size_t n)
{
    for (const char* p = list; *p != '\0';) {
        const char* end = strstr(p, ", ");
        const size_t len = end != NULL ? (size_t) (end - p) : strlen(p);
        if (len == n && strncmp(p, entry, n) == 0) {
            return 1;
        }
        p += len + (end != NULL ? 2 : 0);
    }
    return 0;
}

/* Appends each entry of `a` that `b` lacks, tagged */
static void only_in(const char* a, const char* b, const char* tag, char* out, size_t cap)
{
    for (const char* p = a; *p != '\0';) {
        const char* end = strstr(p, ", ");
        const size_t len = end != NULL ? (size_t) (end - p) : strlen(p);
        if (!listed(b, p, len)) {
            const size_t at = strlen(out);
            snprintf(out + at, cap - at, "%s%.*s (%s)", at > 0 ? ", " : "", (int) len, p, tag);
        }
        p += len + (end != NULL ? 2 : 0);
    }
}

static void display_difference(const char* theirs, char* out, size_t cap)
{
    out[0] = '\0';
    only_in(tgg_mod_set_display(), theirs, "yours", out, cap);
    only_in(theirs, tgg_mod_set_display(), "theirs", out, cap);
}

void session_display_difference(int link, char* out, size_t cap)
{
    display_difference(link >= 0 && link < NET_PEERS ? ss.link[link].display : "", out, cap);
}

/* Whether the game that sent `h` can't play this one: another game version or
 * other mods. `why` says how, for the log. */
static int differs(const WireHello* h, char* why, size_t cap)
{
    /* On one platform the builds and the mods' libraries must be the same. Across
     * platforms they can't be: the game's version and the mods' versions must
     * be, and the checksums show if they play differently anyway */
    const int same_target = strcmp(h->target, TGG_GAME_TARGET) == 0;
    char ours[96], theirs[96];
    game_version(tgg_game_build(), ours, sizeof ours);
    game_version(h->build, theirs, sizeof theirs);
    if (same_target ? strcmp(h->build, tgg_game_build()) != 0 : strcmp(ours, theirs) != 0) {
        snprintf(why, cap, "The other game is another version: %s here, %s there.",
                 same_target ? tgg_game_build() : ours, same_target ? h->build : theirs);
        return 1;
    }
    const char* mine = same_target ? tgg_mod_set_id() : tgg_mod_set_versions();
    const char* other = same_target ? h->mod_set : h->versions;
    if (strcmp(other, mine) != 0) {
        snprintf(why, cap, "The other game has other mods (mod set %s here, %s there).",
                 mine[0] != '\0' ? mine : "none", other[0] != '\0' ? other : "none");
        return 1;
    }
    return 0;
}

/* ---- The host's profile, for a joiner whose game differs
 *
 * The host answers a hello from a game that differs with its own hello and
 * keeps waiting. That joiner asks for the host's profile a piece at a time
 * (WIRE_PROFILE_ASK with the offset it has), and the host sends each piece
 * (WIRE_PROFILE). The joiner's connect mod shows the difference and can
 * restart into the profile, then join again. */

static void send_profile_ask(void)
{
    WirePacket p;
    memset(&p, 0, sizeof p);
    p.type = WIRE_PROFILE_ASK;
    p.u.profile_ask = pf.have;
    session_send_packet(0, &p);
    pf.last_ask_ms = net_now_ms();
}

/* The joiner: the host's game differs */
static void start_mismatch(const WireHello* h, const char* why)
{
    tgg_log("%s", why);
    tgg_session_provide(NULL);
    tgg_log("this game's mods: %s", tgg_mod_set_names()[0] != '\0' ? tgg_mod_set_names() : "none");
    tgg_log("the host runs %s; asking for its profile", h->build);
    ss.state = NETPLAY_MISMATCH;
    char ours[96], theirs[96];
    game_version(tgg_game_build(), ours, sizeof ours);
    game_version(h->build, theirs, sizeof theirs);
    snprintf(ss.why, sizeof ss.why, "%s",
             strcmp(ours, theirs) != 0 ? "The host runs another game version."
                                       : "The host has other mods.");
    memset(&pf, 0, sizeof pf);
    pf.last_got_ms = net_now_ms();
    send_profile_ask();
}

/* The host: a game it won't take said hello; it hears why in our hello */
static void turn_away(const char* why, int no_room)
{
    static char logged[64];
    static double logged_ms;
    const char* from = net_last_name();
    const double now = net_now_ms();
    if (strcmp(from, logged) != 0 || now - logged_ms > 10000) {
        snprintf(logged, sizeof logged, "%s", from);
        logged_ms = now;
        tgg_log("%s said hello: %s%s", from, why,
                no_room ? "" : " Sending it this game's profile if it asks; still waiting");
    }
    WirePacket p;
    hello_packet(&p);
    p.u.hello.assign = no_room ? WIRE_NO_ROOM : 0;
    uint8_t buf[WIRE_MAX_BYTES];
    net_reply(buf, wire_encode(&p, buf));
}

/* The host: a piece of its profile for the game that asked */
static void got_profile_ask(uint32_t offset)
{
    static char mine[WIRE_PROFILE_MAX + 1];
    static uint32_t mine_len;
    static int ready;
    if (offset == 0 || !ready) {
        ready = 1;
        const char* profile = tgg_mod_set_profile();
        const size_t n = profile != NULL ? strlen(profile) : 0;
        mine_len = (uint32_t) (n <= WIRE_PROFILE_MAX ? n : WIRE_PROFILE_MAX + 1);
        if (n <= WIRE_PROFILE_MAX) {
            memcpy(mine, profile != NULL ? profile : "", n);
        }
        tgg_log("%s asked for this game's profile (%u bytes)", net_last_name(),
                (unsigned) n);
    }
    WirePacket p;
    memset(&p, 0, sizeof p);
    p.type = WIRE_PROFILE;
    p.u.profile.total = mine_len;
    p.u.profile.offset = offset;
    if (mine_len <= WIRE_PROFILE_MAX && offset < mine_len) {
        const uint32_t left = mine_len - offset;
        p.u.profile.len = (uint16_t) (left < WIRE_PROFILE_CHUNK ? left : WIRE_PROFILE_CHUNK);
        memcpy(p.u.profile.data, mine + offset, p.u.profile.len);
    }
    uint8_t buf[WIRE_MAX_BYTES];
    net_reply(buf, wire_encode(&p, buf));
}

/* The joiner: a piece of the host's profile */
static void got_profile(const WireProfile* pr)
{
    if (pf.whole) {
        return;
    }
    pf.last_got_ms = net_now_ms();
    if (pr->total > WIRE_PROFILE_MAX) {
        session_fail("The host's list of mods is too long to send.");
        return;
    }
    if (pr->offset != pf.have || pr->offset + pr->len > pr->total) {
        return; /* an old piece */
    }
    memcpy(pf.data + pf.have, pr->data, pr->len);
    pf.have += pr->len;
    pf.total = pr->total;
    if (pf.have < pf.total) {
        send_profile_ask();
        return;
    }
    pf.data[pf.have] = '\0';
    pf.whole = 1;
    tgg_log("the host's profile: %s", pf.data);
}

/* The host: a hello from link `link`, or from a game that isn't one yet (-1).
 * The link, or -1 when the hello goes no further. */
static int host_got_hello(int link, const WireHello* h)
{
    char why[320];
    if (h->token != ss.token) {
        return -1; /* not from a game the meeting point sent here */
    }
    if (h->host) {
        session_fail("Both games host: one hosts, the others join.");
        return -1;
    }
    for (int l = 0; link < 0 && l < net_peers(); l++) {
        if (ss.link[l].greeted && ss.link[l].game == h->game) {
            return -1; /* a game already in, by another of its addresses */
        }
    }
    if (link < 0 && (ss.locked || net_peers() == NET_PEERS)) {
        turn_away(ss.locked ? "the session has started." : "the session is full.", 1);
        return -1;
    }
    if (differs(h, why, sizeof why)) {
        if (link < 0 || !ss.link[link].greeted) {
            turn_away(why, 0); /* that game may come back with these mods */
            return -1;
        }
        tgg_log("this game's mods: %s", tgg_mod_set_names());
        session_fail(why);
        return -1;
    }
    if (setting_strict == 1 && strcmp(h->all, tgg_mod_set_all()) != 0) {
        char diff[200];
        display_difference(h->display, diff, sizeof diff);
        snprintf(why, sizeof why, "The host wants every mod to match, display mods too%s%s.",
                 diff[0] != '\0' ? ": " : "", diff);
        turn_away(why, 0);
        return -1;
    }
    if (h->replay != (ss.replay ? replaytest_hash() : 0)) {
        session_fail("The games play different replays.");
        return -1;
    }
    if (link < 0) {
        link = net_adopt();
        if (link < 0) {
            return -1;
        }
    }
    Link* k = &ss.link[link];
    if (!k->greeted) {
        k->game = h->game;
        k->greeted = 1;
        k->last_heard_ms = net_now_ms();
        snprintf(k->display, sizeof k->display, "%s", h->display);
        tgg_log("%s said hello: same build and mods (%s: %s); it plays side %d",
                net_peer_name(link), tgg_mod_set_id()[0] != '\0' ? tgg_mod_set_id() : "none",
                tgg_mod_set_names(), link_side(link));
        if (strcmp(h->target, TGG_GAME_TARGET) != 0) {
            tgg_log("it runs on %s: its mods are compared by version, not by build", h->target);
        }
        send_hello(link);
        ss.players = 1 + links_greeted();
        send_roster();
    }
    if (h->heard) {
        k->heard = 1;
    }
    return link;
}

/* A joiner: a hello from the host (link 0). 0, or -1 when it goes no further. */
static int join_got_hello(int link, const WireHello* h)
{
    char why[320];
    if (h->token != ss.token) {
        return -1; /* not the session the meeting point set up */
    }
    if (link != 0 && (ss.token == 0 || net_take_host() != 0)) {
        return -1; /* not the game this one joins */
    }
    link = 0;
    if (!h->host) {
        session_fail("The other game joins too: one game hosts, the others join it.");
        return -1;
    }
    if (h->assign == WIRE_NO_ROOM) {
        session_fail("The host's session is full or has started.");
        return -1;
    }
    Link* k = &ss.link[0];
    snprintf(k->display, sizeof k->display, "%s", h->display);
    if (differs(h, why, sizeof why)) {
        if (!k->greeted && !ss.env) {
            start_mismatch(h, why);
        } else {
            tgg_log("this game's mods: %s", tgg_mod_set_names());
            session_fail(why);
        }
        return -1;
    }
    if (h->strict && strcmp(h->all, tgg_mod_set_all()) != 0) {
        char diff[200];
        session_display_difference(0, diff, sizeof diff);
        snprintf(why, sizeof why, "The host wants every mod to match, display mods too%s%s.",
                 diff[0] != '\0' ? ": " : "", diff);
        session_fail(why);
        return -1;
    }
    if (h->replay != (ss.replay ? replaytest_hash() : 0)) {
        session_fail("The games play different replays.");
        return -1;
    }
    if (h->assign == 0 || h->assign >= SIDES) {
        return -1; /* the host hasn't taken this game yet */
    }
    ss.seed = h->seed;
    if (!k->greeted) {
        k->greeted = 1;
        k->last_heard_ms = net_now_ms();
        ss.side = h->assign;
        tgg_log("%s said hello: same build and mods (%s: %s); this game plays side %d",
                net_peer_name(0), tgg_mod_set_id()[0] != '\0' ? tgg_mod_set_id() : "none",
                tgg_mod_set_names(), ss.side);
        if (strcmp(h->target, TGG_GAME_TARGET) != 0) {
            tgg_log("the host runs on %s: its mods are compared by version, not by build",
                    h->target);
        }
        send_hello(0);
    }
    if (h->heard) {
        k->heard = 1;
    }
    return 0;
}

/* A packet from a netplay with another wire version: a joiner can't go on;
 * the host keeps waiting */
static void other_wire(int version)
{
    char why[160];
    snprintf(why, sizeof why,
             "The other game has another version of netplay (wire %d here, %d there).",
             WIRE_VERSION, version);
    if (hosting() && net_last_peer() < 0) {
        static double logged_ms = -1e9;
        if (net_now_ms() - logged_ms > 10000) {
            logged_ms = net_now_ms();
            tgg_log("%s said hello: %s Still waiting", net_last_name(), why);
        }
        return;
    }
    session_fail(why);
}

static void got_lobby(int link, const WireLobby* l)
{
    Link* k = &ss.link[link];
    Queue* q = &sn.q[link];
    k->heard = 1;
    session_got_ping(link, l->ping);
    session_got_pong(link, l->pong, l->pong_age);
    int kept = 0;
    for (int i = 0; i < q->n_out; i++) {
        if (q->out[i].seq > l->msg_ack) {
            q->out[kept++] = q->out[i];
        }
    }
    q->n_out = kept;
    match_got_ack(link, l->number, l->input_ack);
    for (int i = 0; i < l->count && ss.state != NETPLAY_FAILED; i++) {
        if (l->msgs[i].seq == q->in_next) {
            q->in_next++;
            got_msg(link, &l->msgs[i]);
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
        int version = 0;
        int link = net_last_peer();
        const int decoded = wire_decode(buf, len, &p, &version);
        if (decoded < 0) {
            other_wire(version);
            continue;
        }
        if (decoded == 0) {
            continue;
        }
        if (p.type == WIRE_PROFILE_ASK) {
            if (hosting() && link < 0) {
                got_profile_ask(p.u.profile_ask);
            }
            continue;
        }
        if (p.type == WIRE_PROFILE) {
            if (ss.state == NETPLAY_MISMATCH && link == 0) {
                got_profile(&p.u.profile);
            }
            continue;
        }
        if (p.type == WIRE_HELLO) {
            if (ss.state == NETPLAY_MISMATCH) {
                continue;
            }
            link = hosting() ? host_got_hello(link, &p.u.hello) : join_got_hello(link, &p.u.hello);
            if (link < 0) {
                continue;
            }
        } else if (link < 0 || !ss.link[link].greeted) {
            continue;
        } else if (p.type == WIRE_INPUT) {
            ss.link[link].heard = 1;
            match_got_inputs(link, &p.u.inputs);
        } else if (p.type == WIRE_LOBBY) {
            got_lobby(link, &p.u.lobby);
        } else if (p.type == WIRE_BYE) {
            session_fail(ss.state == NETPLAY_PLAYING ? "Another player left the match."
                                                     : "Another player left.");
            continue;
        }
        ss.link[link].last_heard_ms = net_now_ms();
    }
    if ((ss.state == NETPLAY_WAITING || ss.state == NETPLAY_CONNECTING) && ss.link[0].greeted &&
        ss.link[0].heard)
    {
        ss.state = NETPLAY_CONNECTED;
        tgg_log("connected to %s as side %d", net_peer_name(0), ss.side);
    }
}

/* ---- Starting a match */

static void maybe_start(void)
{
    if (ss.state != NETPLAY_STARTING) {
        return;
    }
    if (hosting()) {
        for (int l = 0; l < net_peers(); l++) {
            if (ss.link[l].greeted && !sn.ready[l]) {
                return;
            }
        }
    } else if (!sn.have_match) {
        return;
    }
    static StartMeleeData data;
    memset(&data, 0, sizeof data);
    tgg_match_from_console(&data, sn.console);
    memset(sn.ready, 0, sizeof sn.ready);
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
        for (int l = 0; l < NET_PEERS; l++) {
            ss.link[l].last_heard_ms = -1;
        }
    }
}

/* The host, through the meeting point: says hello to every candidate of the
 * games on their way, every 100 ms, so its router lets their hellos in */
static void punch(double now)
{
    const double age = net_punch_age_ms();
    if (!hosting() || age < 0 || age > PUNCH_MS || now - ss.last_punch_ms < 100) {
        return;
    }
    ss.last_punch_ms = now;
    WirePacket p;
    hello_packet(&p);
    p.u.hello.assign = 0;
    uint8_t buf[WIRE_MAX_BYTES];
    const size_t n = wire_encode(&p, buf);
    net_punch_send(buf, n);
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
    if (ss.state == NETPLAY_MISMATCH) {
        if (!pf.whole && now - pf.last_got_ms > SILENCE_MS) {
            session_fail("Lost the connection to the host.");
        } else if (!pf.whole && now - pf.last_ask_ms > 100) {
            send_profile_ask();
        }
        return;
    }
    for (int l = 0; l < net_peers(); l++) {
        Link* k = &ss.link[l];
        if (k->last_heard_ms < 0) {
            k->last_heard_ms = now;
        }
        if (!k->heard && now - k->last_hello_ms > 100) {
            k->last_hello_ms = now;
            send_hello(l);
        }
    }
    if (ss.token != 0 && (ss.state == NETPLAY_WAITING || ss.state == NETPLAY_CONNECTING)) {
        punch(now);
        if (net_punch_age_ms() > PUNCH_MS && !ss.link[0].greeted) {
            session_fail("Couldn't reach the other game: the routers in between didn't let it through.");
            return;
        }
    }
    if (ss.state == NETPLAY_WAITING || ss.state == NETPLAY_CONNECTING) {
        return;
    }
    if (session_silence_ms() > SILENCE_MS) {
        session_fail(hosting() ? "Lost the connection to another game."
                               : "Lost the connection to the host.");
        return;
    }
    session_send_lobby(0);
    match_linger();
    /* The environment's host locks once its games are in */
    if (ss.env && hosting() && !ss.locked && ss.players >= ss.want) {
        netplay_lock();
    }
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
    ss.side = side != 0 ? -1 : 0;
    ss.players = 1;
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
    if (side != 0 && peer == NULL) {
        snprintf(reason, sizeof reason, "Joining needs the host's address.");
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
    /* The host's seed starts a match the boot sets up; joiners take it from
     * its hello */
    FILE* f = fopen("/dev/urandom", "rb");
    if (f == NULL || fread(&ss.seed, sizeof ss.seed, 1, f) != 1) {
        ss.seed = (uint32_t) net_now_ms();
    }
    if (f != NULL) {
        fclose(f);
    }
    tgg_rollback_init(ss.max_rollback);
    if (tgg_net_random(&ss.game, sizeof ss.game) != TGG_NET_OK) {
        ss.game = (uint32_t) net_now_ms();
    }
    ss.opened_ms = net_now_ms();
    ss.state = side != 0 ? NETPLAY_CONNECTING : NETPLAY_WAITING;
    if (side != 0) {
        tgg_log("joining %s from port %d; %d frames of delay, rolls back up to %d", peer, port,
                ss.delay, ss.max_rollback);
    } else if (peer != NULL) {
        tgg_log("hosting on port %d, saying hello to %s; %d frames of delay, rolls back up to "
                "%d", port, peer, ss.delay, ss.max_rollback);
    } else {
        tgg_log("hosting on port %d; %d frames of delay, rolls back up to %d", port, ss.delay,
                ss.max_rollback);
    }
    return 0;
}

int netplay_open(int side, const char* peer, int port, const char** why)
{
    return open_session(side, peer, port, why);
}

int netplay_lock(void)
{
    if (!hosting() || ss.state != NETPLAY_CONNECTED) {
        return -1;
    }
    if (!ss.locked) {
        ss.players = 1 + links_greeted();
        start_session();
        send_roster();
    }
    return 0;
}

void netplay_close(void)
{
    if (ss.state == NETPLAY_OFF) {
        net_close(); /* a socket netplay_prepare opened */
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
    out->side = ss.side > 0 ? ss.side : 0;
    out->players = ss.players;
    out->locked = ss.locked;
    snprintf(out->peer, sizeof out->peer, "%s", net_peer_name(0));
    for (int l = 0; l < NET_PEERS; l++) {
        if (ss.link[l].rtt_ms > out->rtt_ms) {
            out->rtt_ms = (float) ss.link[l].rtt_ms;
        }
    }
    out->matches = ss.matches;
    snprintf(out->why, sizeof out->why, "%s", ss.why);
    snprintf(out->desync, sizeof out->desync, "%s", ss.desync);
}

const char* netplay_host_profile(void)
{
    return ss.state == NETPLAY_MISMATCH && pf.whole ? pf.data : NULL;
}

int netplay_send(const void* data, int len)
{
    if (ss.state != NETPLAY_CONNECTED && ss.state != NETPLAY_STARTING) {
        return -1;
    }
    return queue_all(MSG_USER, ss.side, data, len, -1);
}

int netplay_receive(void* buf, int cap, int* from)
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
    if (from != NULL) {
        *from = m->from;
    }
    return n;
}

int netplay_play(const StartMeleeData* data)
{
    if (ss.state != NETPLAY_CONNECTED) {
        return -1;
    }
    if (hosting()) {
        int ports[SIDES];
        if (netplay_lock() != 0 || data == NULL || match_ports(data, ports) != ss.players) {
            return -1;
        }
        uint8_t msg[8 + TGG_CONSOLE_MATCH_SIZE];
        sn.number = sn.last_number + 1;
        sn.seed = fresh_seed();
        tgg_match_to_console(data, sn.console);
        put_be32(msg, sn.number);
        put_be32(msg + 4, sn.seed);
        memcpy(msg + 8, sn.console, TGG_CONSOLE_MATCH_SIZE);
        if (queue_all(MSG_MATCH, 0, msg, (int) sizeof msg, -1) != 0) {
            return -1;
        }
    } else if (queue_to(0, MSG_READY, ss.side, "", 0) != 0) {
        return -1;
    }
    ss.state = NETPLAY_STARTING;
    maybe_start();
    return 0;
}

/* ---- The environment: TGG_NETPLAY_PEER, TGG_NETPLAY_GAMES and the rest */

static int env_int(const char* name, int fallback)
{
    const char* v = getenv(name);
    return v != NULL && v[0] != '\0' ? atoi(v) : fallback;
}

static void init(void)
{
    const char* peer = getenv("TGG_NETPLAY_PEER");
    const int games = env_int("TGG_NETPLAY_GAMES", 0);
    if (peer != NULL && peer[0] == '\0') {
        peer = NULL;
    }
    if (peer == NULL && games == 0) {
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
    ss.want = games >= 2 && games <= SIDES ? games : 2;
    ss.replay = is_replay;
    if (is_replay) {
        ss.seed = replaytest_start_seed();
    }
    /* Without a boot match, a plain session: a mod starts the matches */
    const char* boot = getenv("TGG_BOOT_MATCH");
    if (is_replay || (boot != NULL && boot[0] != '\0')) {
        match_expect_boot();
    }
}
TGG_INIT(init);

/* Before the game exits: tell the other games */
static void on_quit(void)
{
    if (ss.state != NETPLAY_OFF && ss.state != NETPLAY_FAILED) {
        match_linger_until_done();
        send_bye();
        net_close();
    }
}
TGG_ON(quit, on_quit);

/* ---- Through the meeting point */

int netplay_prepare(int port, const char** why)
{
    static char reason[200];
    if (ss.state != NETPLAY_OFF && ss.state != NETPLAY_FAILED) {
        if (why != NULL) {
            *why = "A session is already open.";
        }
        return -1;
    }
    const char* failed = net_prepare(port);
    if (failed != NULL) {
        snprintf(reason, sizeof reason, "%s", failed);
        reason[0] = (char) (reason[0] >= 'a' && reason[0] <= 'z' ? reason[0] - 32 : reason[0]);
        if (why != NULL) {
            *why = reason;
        }
        return -1;
    }
    return 0;
}

/* Whether `a` is in 100.64.0.0/10, alone on its interface, or on one named
 * for Tailscale: a tailnet address */
static int tailscale(const tgg_net_interface* i)
{
    const uint8_t* ip = i->addr.ip;
    const int cgnat = i->addr.family == TGG_NET_IPV4 && ip[0] == 100 && (ip[1] & 0xC0) == 64;
    const int named = strstr(i->name, "tailscale") != NULL || strstr(i->name, "Tailscale") != NULL;
    return cgnat && (named || i->prefix == 32);
}

static int add_candidate(netplay_candidate* out, int max, int n, const tgg_net_addr* a,
                         const char* kind)
{
    for (int i = 0; i < n && i < max; i++) {
        if (strcmp(out[i].kind, kind) == 0) {
            char text[TGG_NET_ADDR_TEXT];
            tgg_net_addr_format(a, text, sizeof text);
            if (strcmp(out[i].addr, text) == 0) {
                return n;
            }
        }
    }
    if (n < max) {
        tgg_net_addr_format(a, out[n].addr, sizeof out[n].addr);
        snprintf(out[n].kind, sizeof out[n].kind, "%s", kind);
    }
    return n + 1;
}

int netplay_candidates(netplay_candidate* out, int max)
{
    const char* no_ts = getenv("TGG_NETPLAY_NO_TAILSCALE");
    const int skip_tailscale = no_ts != NULL && no_ts[0] != '0' && no_ts[0] != '\0';
    if (ss.state == NETPLAY_OFF) {
        /* No session yet: the socket only hears STUN's answer (net_recv takes it) */
        uint8_t buf[WIRE_MAX_BYTES];
        while (net_recv(buf, sizeof buf) != 0) {
        }
    }
    const int port = net_local_port();
    if (port <= 0 || out == NULL || max <= 0) {
        return -1;
    }
    char mapped[TGG_NET_ADDR_TEXT];
    const int stun = stun_mapped(mapped, sizeof mapped);
    if (stun == 0) {
        return -1; /* still asking */
    }
    int n = 0;
    tgg_net_interface list[32];
    const int count = tgg_net_interfaces(list, 32);
    for (int i = 0; i < count && i < 32; i++) {
        tgg_net_addr a = list[i].addr;
        const uint8_t* ip = a.ip;
        if (list[i].flags & TGG_NET_IF_LOOPBACK) {
            continue;
        }
        a.port = (uint16_t) port;
        if (a.family == TGG_NET_IPV4 && tailscale(&list[i]) && skip_tailscale) {
            continue; /* TGG_NETPLAY_NO_TAILSCALE: test the internet path */
        }
        if (a.family == TGG_NET_IPV4) {
            n = add_candidate(out, max, n, &a, tailscale(&list[i]) ? "tailscale" : "local");
        } else if ((ip[0] & 0xE0) == 0x20) {
            /* A global IPv6 address: the internet reaches it without a router's
             * translation, once the firewall lets the other game's hello in */
            n = add_candidate(out, max, n, &a, "stun6");
        }
    }
    if (stun == 1) {
        tgg_net_addr a;
        if (tgg_net_resolve(mapped, 0, &a) == TGG_NET_OK) {
            n = add_candidate(out, max, n, &a, a.family == TGG_NET_IPV6 ? "stun6" : "stun");
        }
    }
    return n < max ? n : max;
}

int netplay_open_meet(int side, const char* const* addrs, const char* const* kinds, int n,
                      const char* token, const char** why)
{
    char* end = NULL;
    const uint64_t t = token != NULL ? strtoull(token, &end, 16) : 0;
    if (t == 0 || end == NULL || *end != '\0' || addrs == NULL || n <= 0) {
        if (why != NULL) {
            *why = "The meeting point sent something this netplay can't use.";
        }
        return -1;
    }
    if (open_session(side, side != 0 ? addrs[0] : NULL, 0, why) != 0) {
        return -1;
    }
    ss.token = t;
    if (net_punch(side != 0, addrs, kinds, n) == 0) {
        session_fail("The other game sent no address this one can reach.");
        if (why != NULL) {
            *why = ss.why;
        }
        return -1;
    }
    tgg_log("meeting through the meeting point: %d of the other game's addresses, side %d", n,
            side);
    return 0;
}

const char* netplay_route(double* ms)
{
    const char* kind = net_punched(ms);
    return kind != NULL ? kind : "";
}
