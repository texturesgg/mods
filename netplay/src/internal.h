/* The netplay mod's parts: the socket (net.c), the fake network for tests
 * (fakenet.c), the packets (wire.c), the session and its messages
 * (session.c), the matches and their rollback (match.c) and the replay test
 * (replaytest.c). */
#ifndef NETPLAY_INTERNAL_H
#define NETPLAY_INTERNAL_H

#include "tgg/mod.h"

#include <stddef.h>
#include <stdint.h>

typedef struct HSD_PadStatus HSD_PadStatus;
typedef struct PADStatus PADStatus;
typedef struct StartMeleeData StartMeleeData;
typedef struct tgg_socket tgg_socket;

/* ---- net.c: one UDP socket and its peers, over the port's tgg/net.h: a
 * joiner has one, the host; the host one per joiner */

#define NET_PEERS 3

/* Binds `port` (0: any) on every address. With `peer` ("host:port") that's
 * peer 0. Returns NULL, or what went wrong. */
const char* net_open(int port, const char* peer);
void net_close(void);
/* Sends a datagram to peer `peer` now; drops it if there's no such peer or
 * the socket can't take it. */
void net_send_now(int peer, const void* data, size_t len);
/* A datagram, without waiting: its length, or 0 when none is waiting. Its
 * sender is net_last_peer(), or -1 for a game that isn't a peer (yet). */
size_t net_recv(void* buf, size_t cap);
int net_last_peer(void);
/* Makes the last datagram's sender a peer: its index, or -1 when full. */
int net_adopt(void);
/* Sends a datagram to the last one's sender, peer or not, and that sender's
 * address ("" before any) */
void net_reply(const void* data, size_t len);
const char* net_last_name(void);
int net_peers(void);
/* Peer `peer`'s address, "1.2.3.4:51000", or "" */
const char* net_peer_name(int peer);

/* ---- The meeting point: the socket opened early, and punching */

/* The most candidate addresses of the other games */
#define NET_CANDIDATES 16
/* How long to keep saying hello to candidates that haven't answered */
#define PUNCH_MS 10000.0

/* Opens the socket on `port` (0: any) now and starts STUN; NULL, or what went
 * wrong. A session opened after it (net_open) uses the same socket. */
const char* net_prepare(int port);
/* The port the socket is on, or -1 */
int net_local_port(void);
/* The other games' candidates ("host:port", and each one's kind: local,
 * stun, ...). A joiner (`joining`) treats whichever answers as the host; the
 * host sends net_punch_send to each one that isn't a peer yet. How many were
 * usable. */
int net_punch(int joining, const char* const* addrs, const char* const* kinds, int n);
/* The host: sends a datagram to every candidate that isn't a peer, for
 * PUNCH_MS after net_punch */
void net_punch_send(const void* data, size_t len);
/* Milliseconds since net_punch, or -1 without candidates */
double net_punch_age_ms(void);
/* The kind of candidate that answered first and how long it took, or NULL */
const char* net_punched(double* ms);
/* A joiner still punching: the last datagram's sender is the host (its hello
 * carried the session's token). 0, or -1 when not punching */
int net_take_host(void);

/* ---- stun.c: this socket's address as the internet sees it */

void stun_start(tgg_socket* sock);
void stun_stop(void);
/* Resends while there's no answer */
void stun_pump(void);
/* Takes a datagram if it's a STUN answer: 1 when it was */
int stun_got(const void* data, size_t len);
/* 1 with the address, 0 while asking, -1 when no answer came */
int stun_mapped(char* text, size_t cap);
/* Milliseconds on a clock that only goes forward. Presentation code only. */
double net_now_ms(void);
/* Waits up to `ms` for a datagram to arrive: for loops that run while the
 * game doesn't, as it exits */
void net_wait_ms(int ms);

/* ---- fakenet.c: a network with latency, jitter and loss, for tests:
 * TGG_NETPLAY_FAKE="latency=40,jitter=10,loss=5,seed=1" (one way, in ms and
 * percent). Without it, sends go out at once. */

void fakenet_init(const char* spec);
/* Sends to peer `peer` through the fake network, or at once without one. */
void net_send(int peer, const void* data, size_t len);
/* Sends what the fake network holds and is due. */
void fakenet_flush(void);

/* ---- wire.c: what goes over the network */

/* One port's input for one frame, as the sender's game made it: the report
 * the controller gave and the state the game made of it. A receiver sets
 * both as they are, so both machines run the frame on the same values. */
typedef struct WireInput {
    uint32_t buttons;  /* HSD_PadStatus::button */
    uint16_t physical; /* PADStatus::button */
    int8_t raw_x, raw_y, raw_cx, raw_cy;
    uint8_t raw_l, raw_r;
    int8_t x, y, cx, cy;
    uint8_t l, r;
    int8_t err;
    uint8_t unused;
    float nx, ny, ncx, ncy, nl, nr;
} WireInput;

/* A port's input as the game holds it after its read */
WireInput wire_from_pad(const HSD_PadStatus* status, const PADStatus* raw);
/* Sets a port's state and report to `in` */
void wire_to_pad(const WireInput* in, HSD_PadStatus* status, PADStatus* raw);
/* No buttons, sticks centered */
WireInput wire_neutral(void);
int wire_same(const WireInput* a, const WireInput* b);

/* The most games in a session: the host and NET_PEERS joiners */
#define SIDES (NET_PEERS + 1)

#define WIRE_MAGIC 0x4e474754u /* "TGGN" */
#define WIRE_VERSION 7
enum {
    WIRE_HELLO = 1,
    WIRE_INPUT = 2,
    WIRE_BYE = 3,
    WIRE_LOBBY = 4,
    WIRE_PROFILE_ASK = 5, /* a joiner whose game differs asks for the host's profile */
    WIRE_PROFILE = 6,     /* a piece of it */
};
#define WIRE_MAX_INPUTS 40
#define WIRE_MAX_RUNS NET_PEERS
#define WIRE_MSG_MAX 400
#define WIRE_MAX_MSGS 32
/* The messages in one lobby packet stay under this many bytes */
#define WIRE_MSGS_BYTES 1700
/* The host's profile goes in pieces of up to WIRE_PROFILE_CHUNK bytes, and is
 * at most WIRE_PROFILE_MAX */
#define WIRE_PROFILE_CHUNK 1024
#define WIRE_PROFILE_MAX (16 * 1024)

/* A hello's `assign` from a host that takes no more games */
#define WIRE_NO_ROOM 0xff

typedef struct WireHello {
    char build[96];
    char mod_set[17];
    char versions[17]; /* tgg_mod_set_versions */
    char target[32];   /* the platform, TGG_GAME_TARGET */
    uint8_t host;      /* the sender hosts */
    uint8_t assign;    /* the host's: the receiver's side, from 1; WIRE_NO_ROOM */
    uint8_t delay;
    uint8_t heard;   /* the sender has the receiver's hello */
    uint32_t seed;
    uint32_t replay; /* the replay test: a hash of the replay played, else 0 */
    char all[17];     /* tgg_mod_set_all: every mod that loaded, display mods too */
    char display[256]; /* tgg_mod_set_display: the display mods, "id version, ..." */
    uint8_t strict;   /* the host wants every mod to match, display mods too */
    uint64_t token;   /* a session the meeting point set up: both games have it; 0 for none */
    uint32_t game;    /* a random number this game picked when the session opened */
} WireHello;

/* A run of one side's inputs, from frame `first` */
typedef struct WireRun {
    uint8_t side;
    int32_t first;
    uint8_t count;
} WireRun;

typedef struct WireInputs {
    uint32_t number;     /* the match: 1 for the session's first */
    int32_t frame;       /* the sender's next frame to run */
    uint32_t match;      /* a hash of the sender's match setup, 0 before it */
    int32_t sum_frame;   /* a confirmed frame and its checksum, or -1 */
    uint64_t sum;
    uint32_t ping;       /* the sender's clock, ms */
    uint32_t pong;       /* the last ping it had from the receiver */
    uint16_t pong_age;   /* how long ago it had it, ms */
    int32_t ack[SIDES];  /* the newest frame of each side's input the sender has */
    uint8_t runs;
    WireRun run[WIRE_MAX_RUNS];
    WireInput inputs[WIRE_MAX_INPUTS]; /* the runs' inputs, one after another */
} WireInputs;

/* A message: what netplay_send sends, and netplay's own about the session */
typedef struct WireMsg {
    uint32_t seq; /* from 1, one more for each message sent on the link */
    uint8_t kind; /* MSG_* */
    uint8_t from; /* the side that sent it first: the host passes joiners' on */
    uint16_t len;
    uint8_t data[WIRE_MSG_MAX];
} WireMsg;

/* Between matches: the round trip, messages, and the last match's inputs
 * acknowledged */
typedef struct WireLobby {
    uint32_t ping, pong;
    uint16_t pong_age;
    uint32_t msg_ack;   /* the newest message the sender has, in order */
    uint32_t number;    /* the sender's last match, 0 before one */
    int32_t input_ack[SIDES]; /* the newest frame of each side's input it has in it */
    uint8_t count;
    WireMsg msgs[WIRE_MAX_MSGS];
} WireLobby;

/* The host's profile (tgg_mod_set_profile's JSON): `len` bytes of it from
 * `offset`, of `total`. A total over WIRE_PROFILE_MAX says it's too long to
 * send, and the piece is empty. */
typedef struct WireProfile {
    uint32_t total;
    uint32_t offset;
    uint16_t len;
    uint8_t data[WIRE_PROFILE_CHUNK];
} WireProfile;

typedef struct WirePacket {
    uint8_t type;
    union {
        WireHello hello;
        WireInputs inputs;
        WireLobby lobby;
        uint32_t profile_ask; /* WIRE_PROFILE_ASK: the offset wanted */
        WireProfile profile;
    } u;
} WirePacket;

/* Writes a packet into buf (at least WIRE_MAX_BYTES); returns its length */
#define WIRE_MAX_BYTES 2048
size_t wire_encode(const WirePacket* p, uint8_t* buf);
/* Reads one: 1, or 0 if it isn't netplay's packet, or -1 if it's netplay's
 * of another wire version (the version in `*version`) */
int wire_decode(const uint8_t* buf, size_t len, WirePacket* p, int* version);
/* The bytes a message takes in a lobby packet */
size_t wire_msg_bytes(const WireMsg* m);

/* ---- session.c: the session (the handshake, keeping in touch, messages)
 * and the API
 *
 * The games meet in a star: each joiner talks to the host only, and the host
 * passes on what one joiner sends the others (inputs and messages). A link is
 * a peer of net.c: a joiner's one link is the host; the host's link L is the
 * joiner playing side L + 1. */

enum {
    MSG_USER = 1,   /* netplay_send's */
    MSG_MATCH = 2,  /* the host's match: its number, seed and setup */
    MSG_READY = 3,  /* a joiner called netplay_play */
    MSG_INPUT = 4,  /* a shared screen's menu input for a tick (tgg/session.h) */
    MSG_ROSTER = 5, /* the host: how many games are in, and whether that's all */
    MSG_POST = 6,   /* the port's own (tgg/session.h post), passed on by the host */
    MSG_OFFER = 7,  /* the host: the match its game's menus set up, as MSG_MATCH */
};

/* A link to one other game */
typedef struct Link {
    int greeted; /* its hello arrived and matched */
    int heard;   /* it has ours */
    double last_hello_ms, last_lobby_ms, last_heard_ms;
    uint32_t peer_ping;
    double peer_ping_ms;
    double rtt_ms;
    uint32_t game;     /* its hello's `game` */
    char display[256]; /* its display mods, from its hello */
} Link;

/* The session, shared with match.c */
typedef struct Session {
    int state; /* NETPLAY_* */
    int side;  /* 0 hosts; a joiner's comes from the host's hello */
    int players; /* games in the session, this one included */
    int locked;  /* the host takes no more games: the session (tgg/session.h) is on */
    int want;    /* the environment's TGG_NETPLAY_GAMES: the host locks at that many */
    int delay;
    int max_rollback;
    int env;       /* opened from the environment, for a match the boot sets up */
    int replay;    /* the replay test */
    uint32_t seed; /* the host's: the boot's match starts from it */
    uint32_t game;  /* this game's hello `game`: a game that says hello from several
                     * addresses is one game */
    uint64_t token; /* set by the meeting point: hellos without it are ignored; 0 for none */
    double last_punch_ms; /* the host: when it last said hello to the candidates */
    double opened_ms;
    int matches;
    Link link[NET_PEERS];
    char why[160];
    char desync[256]; /* netplay_info.desync */
} Session;

extern Session ss;

/* The side link `link` plays */
int link_side(int link);
/* The link a side's packets come from: the host's for a joiner */
int side_link(int side);

/* The settings */
extern int setting_delay;
extern int setting_controller; /* 0 any, else the port + 1 */
extern int setting_strict; /* 1: every mod must match, display mods too (the host's) */
/* The display mods only this game or link `link`'s has, "a (yours), b
 * (theirs)", or "" */
void session_display_difference(int link, char* out, size_t cap);

/* Reads the network: every packet waiting goes where it belongs */
void session_receive(void);
/* Sends the between-matches packet to every link when it's due (now with
 * `force`) */
void session_send_lobby(int force);
void session_send_packet(int link, const WirePacket* p);
/* Ends the session with `why` for the player */
void session_fail(const char* why);
/* A match ended: back to CONNECTED */
void session_match_over(void);
/* The clock in packets */
uint32_t session_clock(void);
/* The round trip on `link`: a ping came back, `age` ms after it was had */
void session_got_pong(int link, uint32_t pong, uint16_t age);
/* A ping from `link`, and how long ago the newest came, to send back */
void session_got_ping(int link, uint32_t ping);
uint16_t session_pong_age(int link);
/* How long since the link heard from least recently said anything, ms */
double session_silence_ms(void);

/* ---- match.c: the match being played and its rollback */

/* Starts match `number` from `data` (already as every game has it) with
 * `seed`; from a screen, the screen leaves for it. Returns 0 or -1. */
int match_begin(uint32_t number, uint32_t seed, const StartMeleeData* data);
/* The env session's match, the one the boot sets up: number 1, the hello's
 * seed */
void match_expect_boot(void);
/* A match the game's own menus start (VS mode), `data` as every game has it:
 * its number and seed */
void match_expect(uint32_t number, uint32_t seed, const StartMeleeData* data);
void match_got_inputs(int link, const WireInputs* in);
/* Link `link`'s acks of each side's inputs in match `number` */
void match_got_ack(int link, uint32_t number, const int32_t ack[SIDES]);
/* The match's number, and the newest frame of each side's input this game
 * has in it, for the between-matches packet */
uint32_t match_number(void);
/* The match's seed */
uint32_t match_seed(void);
void match_input_acks(int32_t out[SIDES]);
/* Between matches: sends the last match's inputs until every link has them */
void match_linger(void);
/* Before the game exits: the same, for up to three seconds */
void match_linger_until_done(void);
/* The session ended during a match: stop holding, leave it */
void match_abandon(void);
/* A match's human ports, in port order: how many, or -1 for none */
int match_ports(const StartMeleeData* data, int ports[SIDES]);

/* ---- replaytest.c: TGG_NETPLAY_REPLAY=<file.slp>, two games play a Slippi
 * replay against each other: each boots its match, plays one side's recorded
 * inputs and sets each frame's RNG seed as the replay recorded it */

/* Reads the replay and boots its match; returns 0 if it can't */
int replaytest_open(const char* path);
/* The replay's two human ports, in port order */
int replaytest_port(int side);
uint32_t replaytest_start_seed(void);
uint32_t replaytest_hash(void);
/* The replay's input for `port` at match frame `frame`, as the game would
 * hold it; neutral past the end */
WireInput replaytest_input(int port, int frame);
/* Sets the replay's seed for match frame `frame`, if it has one */
void replaytest_seed(int frame);
/* Whether match frame `frame` is past the replay's end */
int replaytest_ended(int frame);

#endif
