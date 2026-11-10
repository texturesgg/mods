/* A copy of tgg.netplay's include/netplay.h.
 *
 * tgg.netplay's API for other mods: a session of two to four games over the
 * network, messages between them, and the matches they play together with
 * rollback.
 *
 * A mod that connects players (a direct-connect screen, a lobby, a match
 * maker) opens a session: one game hosts, and up to three join it. It talks to
 * the other games with messages, locks the session once everyone is in, and
 * starts a match or a shared screen (tgg/session.h); netplay does the rest:
 * the inputs, guessing, rolling back, keeping the games in time, and checking
 * they agree.
 *
 *     netplay_open(1, "192.168.1.20:51000", 0, &why);   // join the host's game
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
 *     "depends": { "tgg.netplay": "^0.5" }
 *
 * and includes this header (copy it, or add netplay's include/ to the
 * include path). */
#ifndef NETPLAY_H_API
#define NETPLAY_H_API

#include "tgg/mod.h"

#define NETPLAY TGG_PROVIDER(tgg_netplay, "tgg.netplay", 1)

/* As the game declares it (melee/mn/forward.h). */
typedef struct StartMeleeData StartMeleeData;

/* The most games in a session: the host and three joiners. */
#define NETPLAY_MAX_PLAYERS 4

/* Where a session is. */
enum {
    NETPLAY_OFF = 0,        /* no session */
    NETPLAY_WAITING = 1,    /* the host, before any game joined */
    NETPLAY_CONNECTING = 2, /* a joiner saying hello to the host, no answer yet */
    NETPLAY_CONNECTED = 3,  /* the games met: same build, same mods */
    NETPLAY_STARTING = 4,   /* netplay_play was called; waiting for the other games */
    NETPLAY_PLAYING = 5,    /* a match is on */
    NETPLAY_FAILED = 6,     /* it ended: `why` says how; netplay_close to start over */
    NETPLAY_MISMATCH = 7,   /* a joiner: the host runs another game version or other mods;
                               `why` says which, and netplay_host_profile gives the
                               host's profile once it has arrived */
};

typedef struct netplay_info {
    int state;      /* NETPLAY_* */
    int side;       /* this game's side: 0 hosts, joiners 1 to 3 in the order they
                       joined; a joiner's is 0 until the host answers */
    int players;    /* games in the session, this one included */
    int locked;     /* the host locked the session: no more games join */
    char peer[64];  /* a joiner: the host's address, "192.168.1.20:51000"; the host:
                       the first joiner's, once there is one */
    float rtt_ms;   /* the longest round trip to another game, 0 before it's known */
    int matches;    /* matches played in this session */
    char why[160];  /* NETPLAY_FAILED: what happened, for the player */
    char desync[256]; /* the last match stopped matching: when, and the display mods
                       * only one game has (the likely cause), for the player; else "" */
} netplay_info;

/* Opens a session: side 0 hosts, any other side joins. With `peer` NULL,
 * the host waits on UDP `port` and takes each game whose hello arrives with
 * the same build and mods, up to three, until netplay_lock; a game that
 * differs is told so, may ask for this game's profile, and the host keeps
 * waiting. A joiner says hello to `peer` ("host:port", a name or an IPv4
 * address) from `port`, or from a port of the system's choosing when `port`
 * is 0; when the host differs, the session goes to NETPLAY_MISMATCH. Turns
 * rollback on from the next match. Returns 0, or -1 with `why` (may be NULL)
 * saying why, for the player to see. An open session is closed first. */
TGG_API(NETPLAY, int, netplay_open, (int side, const char* peer, int port, const char** why));

/* The host: no more games join, and the session (tgg/session.h) starts on
 * every game with the games in it now: tgg_session_players() is their
 * count, and shared screens run on all of them. The first netplay_play
 * locks too. Returns 0, or -1 when this game isn't a connected host. */
TGG_API(NETPLAY, int, netplay_lock, (void));

/* Ends the session: tells the other games, closes the socket, and turns
 * rollback off from the next match. Does nothing without one. When any game
 * leaves, the session ends for all. */
TGG_API(NETPLAY, void, netplay_close, (void));

/* Where the session is. */
TGG_API(NETPLAY, void, netplay_status, (netplay_info* out));

/* NETPLAY_MISMATCH: the host's mod profile, the JSON its tgg_mod_set_profile
 * gives (the game's version and layout, and each counted mod's version and
 * settings), once it has arrived whole; else NULL. Write it to a file and
 * pass it to tgg_restart_with to restart into the host's mods, then join
 * again from there. Valid until the session closes. */
TGG_API(NETPLAY, const char*, netplay_host_profile, (void));

/* Sends a message of up to 400 bytes to every other game, once connected.
 * Messages arrive whole, once, and in the order each game sent them. Returns
 * 0, or -1 when it's too long, there's no connected session, or too many are
 * waiting to go. */
TGG_API(NETPLAY, int, netplay_send, (const void* data, int len));

/* The next message from another game: copies it to `buf`, sets `*from` (may
 * be NULL) to the side that sent it, and returns its length, or 0 when none is
 * waiting. A message longer than `cap` is cut. */
TGG_API(NETPLAY, int, netplay_receive, (void* buf, int cap, int* from));

/* Plays a match with the other games, from a screen (tgg/screen.h): the host
 * passes the match (`data`: the rules, the stage and the players), and the
 * joiners pass NULL to take the host's. `data` has one human player for each
 * game: side 0 plays the first, side 1 the second, and so on. Once every game
 * has called it, each leaves its screen for the match at the same time, and
 * its screen comes back when the match ends; the session then goes back to
 * NETPLAY_CONNECTED for the next one. A mod with shared screens calls
 * tgg_play_match instead, which comes here. Returns 0, or -1 when the session
 * isn't connected or `data` doesn't have a human player for each game. */
TGG_API(NETPLAY, int, netplay_play, (const StartMeleeData* data));

#endif
