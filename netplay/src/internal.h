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

/* ---- net.c: one UDP socket and one peer. The only file with socket calls,
 * so another platform's sockets replace it alone. */

/* Binds `port` (0: any) on every address. With `peer` ("host:port") it sends
 * there and takes datagrams only from there; with NULL, the peer is whoever
 * net_adopt names. Returns NULL, or what went wrong. */
const char* net_open(int port, const char* peer);
void net_close(void);
/* Sends a datagram to the peer now; drops it if there's no peer or the
 * socket can't take it. */
void net_send_now(const void* data, size_t len);
/* A datagram, without waiting: its length, or 0 when none is waiting.
 * Datagrams from anyone but the peer are dropped, unless there's no peer
 * yet: then net_adopt makes the last one's sender the peer. */
size_t net_recv(void* buf, size_t cap);
void net_adopt(void);
int net_has_peer(void);
/* The peer's address, "1.2.3.4:51000", or "" */
const char* net_peer_name(void);
/* Milliseconds on a clock that only goes forward. */
double net_now_ms(void);
void net_sleep_ms(int ms);

/* ---- fakenet.c: a network with latency, jitter and loss, for tests:
 * TGG_NETPLAY_FAKE="latency=40,jitter=10,loss=5,seed=1" (one way, in ms and
 * percent). Without it, sends go out at once. */

void fakenet_init(const char* spec);
/* Sends through the fake network, or at once without one. */
void net_send(const void* data, size_t len);
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

#define WIRE_MAGIC 0x4e474754u /* "TGGN" */
#define WIRE_VERSION 3
enum { WIRE_HELLO = 1, WIRE_INPUT = 2, WIRE_BYE = 3, WIRE_LOBBY = 4 };
#define WIRE_MAX_INPUTS 40
#define WIRE_MSG_MAX 400
#define WIRE_MAX_MSGS 4

typedef struct WireHello {
    char build[96];
    char mod_set[17];
    char versions[17]; /* tgg_mod_set_versions */
    char target[32];   /* the platform, TGG_GAME_TARGET */
    uint8_t side;    /* 0 hosts: its seed starts a match the boot sets up */
    uint8_t delay;
    uint8_t heard;   /* the sender has the receiver's hello */
    uint32_t seed;
    uint32_t replay; /* the replay test: a hash of the replay played, else 0 */
} WireHello;

typedef struct WireInputs {
    uint32_t number;     /* the match: 1 for the session's first */
    int32_t frame;       /* the sender's next frame to run */
    int32_t ack;         /* the newest frame of the receiver's input the sender has */
    uint32_t match;      /* a hash of the sender's match setup, 0 before it */
    int32_t sum_frame;   /* a confirmed frame and its checksum, or -1 */
    uint64_t sum;
    uint32_t ping;       /* the sender's clock, ms */
    uint32_t pong;       /* the last ping it had from the receiver */
    uint16_t pong_age;   /* how long ago it had it, ms */
    int32_t first;       /* the frame of inputs[0] */
    uint8_t count;
    WireInput inputs[WIRE_MAX_INPUTS];
} WireInputs;

/* A message: what netplay_send sends, and netplay's own about matches */
typedef struct WireMsg {
    uint32_t seq; /* from 1, one more for each message sent */
    uint8_t kind; /* MSG_* (session.c) */
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
    int32_t input_ack;  /* the newest frame of the receiver's input it has in it */
    uint8_t count;
    WireMsg msgs[WIRE_MAX_MSGS];
} WireLobby;

typedef struct WirePacket {
    uint8_t type;
    union {
        WireHello hello;
        WireInputs inputs;
        WireLobby lobby;
    } u;
} WirePacket;

/* Writes a packet into buf (at least WIRE_MAX_BYTES); returns its length */
#define WIRE_MAX_BYTES 2048
size_t wire_encode(const WirePacket* p, uint8_t* buf);
/* Reads one; 0 if it isn't a packet of this version */
int wire_decode(const uint8_t* buf, size_t len, WirePacket* p);

/* ---- session.c: the session (the handshake, keeping in touch, messages)
 * and the API */

enum {
    MSG_USER = 1,  /* netplay_send's */
    MSG_MATCH = 2, /* side 0's match: its number, seed and setup */
    MSG_READY = 3, /* side 1 called netplay_play */
};

/* The session, shared with match.c */
typedef struct Session {
    int state; /* NETPLAY_* */
    int side;
    int delay;
    int max_rollback;
    int env;       /* opened from the environment, for a match the boot sets up */
    int replay;    /* the replay test */
    uint32_t seed; /* side 0's: the boot's match starts from it */
    int greeted;   /* the other side's hello arrived and matched */
    int heard;     /* it has ours */
    double opened_ms, last_hello_ms, last_lobby_ms, last_heard_ms;
    uint32_t peer_ping;
    double peer_ping_ms;
    double rtt_ms;
    int matches;
    char why[160];
} Session;

extern Session ss;

/* The settings */
extern int setting_delay;
extern int setting_controller; /* 0 any, else the port + 1 */

/* Reads the network: every packet waiting goes where it belongs */
void session_receive(void);
/* Sends the between-matches packet when it's due (now with `force`) */
void session_send_lobby(int force);
void session_send_packet(const WirePacket* p);
/* Ends the session with `why` for the player */
void session_fail(const char* why);
/* A match ended: back to CONNECTED */
void session_match_over(void);
/* The clock in packets */
uint32_t session_clock(void);
/* The round trip: a ping came back, `age` ms after the other side had it */
void session_got_pong(uint32_t pong, uint16_t age);
/* A ping from the other side, and how long ago the newest came, to send back */
void session_got_ping(uint32_t ping);
uint16_t session_pong_age(void);

/* ---- match.c: the match being played and its rollback */

/* Starts match `number` from `data` (already as both sides have it) with
 * `seed`; from a screen, the screen leaves for it. Returns 0 or -1. */
int match_begin(uint32_t number, uint32_t seed, const StartMeleeData* data);
/* The env session's match, the one the boot sets up: number 1, the hello's
 * seed */
void match_expect_boot(void);
void match_got_inputs(const WireInputs* in);
/* The other side's ack of this side's inputs in match `number` */
void match_got_ack(uint32_t number, int32_t ack);
/* The newest frame of the other side's input in match `number`, for the
 * between-matches packet; -1 when it isn't the current one */
uint32_t match_number(void);
int32_t match_input_ack(void);
/* Between matches: sends the last match's inputs until the other side has
 * them all */
void match_linger(void);
/* Before the game exits: the same, for up to three seconds */
void match_linger_until_done(void);
/* The session ended during a match: stop holding, leave it */
void match_abandon(void);
/* Fill in a match's ports from its setup */
int match_ports(const StartMeleeData* data, int ports[2]);

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
