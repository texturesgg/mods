/* A copy of tgg.netplay's include/netplay.h.
 *
 * tgg.netplay's API for other mods: a session with another game over the
 * network, messages between the two, and the matches they play together
 * with rollback.
 *
 * A mod that connects players (a direct-connect screen, a lobby, a match
 * maker) opens a session, talks to the other game with messages, and starts
 * a match from its screen (tgg/screen.h); netplay does the rest: the inputs,
 * guessing, rolling back, keeping the two games in time, and checking they
 * agree.
 *
 *     netplay_open(1, "100.64.0.7:51000", 0, &why);   // join side 0's game
 *     ...
 *     netplay_status(&info);
 *     if (info.state == NETPLAY_CONNECTED) {
 *         netplay_send("ready", 5);
 *     }
 *     ...
 *     netplay_play(NULL);                              // the host decides the match
 *
 * Everything here is presentation code: call it from a screen's functions or
 * a PRESENT event, never from simulation code.
 *
 * A mod that uses it lists netplay in its manifest's "depends":
 *
 *     "depends": { "tgg.netplay": "^0.2" }
 *
 * and includes this header (copy it, or add netplay's include/ to the
 * include path). */
#ifndef NETPLAY_H_API
#define NETPLAY_H_API

#include "tgg/mod.h"

#define NETPLAY TGG_PROVIDER(tgg_netplay, "tgg.netplay", 1)

/* As the game declares it (melee/mn/forward.h). */
typedef struct StartMeleeData StartMeleeData;

/* Where a session is. */
enum {
    NETPLAY_OFF = 0,        /* no session */
    NETPLAY_WAITING = 1,    /* side 0 with no peer yet: waiting for a game to say hello */
    NETPLAY_CONNECTING = 2, /* saying hello to the peer, no answer yet */
    NETPLAY_CONNECTED = 3,  /* the two games met: same build, same mods */
    NETPLAY_STARTING = 4,   /* netplay_play was called; waiting for the other side */
    NETPLAY_PLAYING = 5,    /* a match is on */
    NETPLAY_FAILED = 6,     /* it ended: `why` says how; netplay_close to start over */
};

typedef struct netplay_info {
    int state;      /* NETPLAY_* */
    int side;       /* this game's side: 0 or 1 */
    char peer[64];  /* the other game's address, "100.64.0.7:51000", once known */
    float rtt_ms;   /* the round trip to it, 0 before it's known */
    int matches;    /* matches played in this session */
    char why[160];  /* NETPLAY_FAILED: what happened, for the player */
} netplay_info;

/* Opens a session as side 0 or 1, one of each. With `peer` NULL, side 0
 * waits for any game to say hello on UDP `port` and plays the first that
 * does (its build and mods must match). With `peer` ("host:port", a name or
 * an IPv4 address), the game says hello to it from `port`, or from a port of
 * the system's choosing when `port` is 0. Turns rollback on from the next
 * match. Returns 0, or -1 with `why` (may be NULL) saying why, for the
 * player to see. An open session is closed first. */
TGG_API(NETPLAY, int, netplay_open, (int side, const char* peer, int port, const char** why));

/* Ends the session: tells the other game, closes the socket, and turns
 * rollback off from the next match. Does nothing without one. */
TGG_API(NETPLAY, void, netplay_close, (void));

/* Where the session is. */
TGG_API(NETPLAY, void, netplay_status, (netplay_info* out));

/* Sends a message of up to 400 bytes to the other game, once connected.
 * Messages arrive whole, once, and in the order sent. Returns 0, or -1 when
 * it's too long, there's no connected session, or 32 are waiting to go. */
TGG_API(NETPLAY, int, netplay_send, (const void* data, int len));

/* The next message from the other game: copies it to `buf` and returns its
 * length, or 0 when none is waiting. A message longer than `cap` is cut. */
TGG_API(NETPLAY, int, netplay_receive, (void* buf, int cap));

/* Plays a match with the other game, from a screen (tgg/screen.h): side 0
 * passes the match (`data`: the rules, the stage and the players), and side
 * 1 passes NULL to take side 0's. Side 0 plays the first human player in
 * `data`, side 1 the second. Once both sides have called it, each leaves its
 * screen for the match at the same time, and its screen comes back when the
 * match ends; the session then goes back to NETPLAY_CONNECTED for the next
 * one. Returns 0, or -1 when the session isn't connected or `data` doesn't
 * have two human players. */
TGG_API(NETPLAY, int, netplay_play, (const StartMeleeData* data));

#endif
