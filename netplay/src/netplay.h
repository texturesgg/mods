/* The netplay mod's parts: the socket (net.c), the fake network for tests
 * (fakenet.c), the packets (wire.c), the replay test (replaytest.c) and the
 * rollback itself (mod.c). */
#ifndef NETPLAY_H
#define NETPLAY_H

#include "tgg/mod.h"

#include <stddef.h>
#include <stdint.h>

typedef struct HSD_PadStatus HSD_PadStatus;
typedef struct PADStatus PADStatus;
typedef struct StartMeleeData StartMeleeData;

/* ---- net.c: one UDP socket to one peer. The only file with socket calls,
 * so another platform's sockets replace it alone. */

/* Binds `port` on every address and sends to `peer` ("host:port"). Returns
 * NULL, or what went wrong. */
const char* net_open(int port, const char* peer);
/* Sends a datagram to the peer now; drops it if the socket can't take it. */
void net_send_now(const void* data, size_t len);
/* A datagram from the peer, without waiting: its length, or 0 when none is
 * waiting. Datagrams from anyone else are dropped. */
size_t net_recv(void* buf, size_t cap);
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
#define WIRE_VERSION 1
enum { WIRE_HELLO = 1, WIRE_INPUT = 2, WIRE_BYE = 3 };
#define WIRE_MAX_INPUTS 40

typedef struct WireHello {
    char build[96];
    char mod_set[17];
    uint8_t side;    /* 0 hosts: its seed is the match's */
    uint8_t delay;
    uint8_t unused;
    uint32_t seed;
    uint32_t replay; /* the replay test: a hash of the replay played, else 0 */
} WireHello;

typedef struct WireInputs {
    int32_t frame;       /* the sender's next frame to run */
    int32_t ack;         /* the newest frame of the receiver's input the sender has */
    uint32_t match;      /* a hash of the sender's match setup, 0 before it */
    int32_t sum_frame;   /* a confirmed frame and its checksum, or -1 */
    uint64_t sum;
    uint32_t ping;       /* the sender's clock, ms */
    uint32_t pong;       /* the last ping it had from the receiver */
    int32_t first;       /* the frame of inputs[0] */
    uint8_t count;
    WireInput inputs[WIRE_MAX_INPUTS];
} WireInputs;

typedef struct WirePacket {
    uint8_t type;
    union {
        WireHello hello;
        WireInputs inputs;
    } u;
} WirePacket;

/* Writes a packet into buf (at least WIRE_MAX_BYTES); returns its length */
#define WIRE_MAX_BYTES 2048
size_t wire_encode(const WirePacket* p, uint8_t* buf);
/* Reads one; 0 if it isn't a packet of this version */
int wire_decode(const uint8_t* buf, size_t len, WirePacket* p);

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
