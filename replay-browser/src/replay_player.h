/* What the browser reads from a replay (summary.c), the types of
 * tgg.replay-player's include/replay_player.h without its rules report, and
 * the player's replay_play, which the browser finds when the player is
 * loaded (tgg_mod_export) rather than depending on it: the player only loads
 * in a game restarted for a replay. */
#ifndef REPLAY_PLAYER_H_API
#define REPLAY_PLAYER_H_API

#include "tgg/mod.h"

/* One port of a replay. */
typedef struct replay_player_slot {
    int present;    /* someone played in this port */
    int ckind;      /* their CharacterKind (melee/ft/forward.h) */
    int costume;    /* 0 to 5 */
    int cpu;        /* a computer player */
    int team;       /* 0 red, 1 blue, 2 green, in a team match */
    char name[32];  /* the player's name: Slippi's display name, else the
                     * name tag; ASCII, "" when there's none */
    char code[16];  /* the Slippi connect code ("ABCD#123"), or "" */
    int placement;  /* 0 is first, -1 unknown */
} replay_player_slot;

/* What a replay holds, from its first and last few kilobytes. */
typedef struct replay_summary {
    int version[3];    /* the Slippi version that recorded it */
    int stage;         /* its StKind (melee/gr/forward.h) */
    int frames;        /* how long the match ran, in frames; -1 unknown */
    int online;        /* played on Slippi Online */
    int teams;         /* a team match */
    char date[20];     /* when it started, "2026-08-14 22:58", as recorded */
    long long when;    /* the same, as a number that sorts by date */
    int end;           /* how it ended: REPLAY_END_* */
    int quitter;       /* the port that quit with L+R+A+Start, or -1 */
    int winner;        /* the port that won (in a team match, one of the
                        * winning team's), or -1 when unknown or none */
    replay_player_slot players[4];
} replay_summary;

enum {
    REPLAY_END_UNKNOWN = 0, /* the file has no end (a crash, an older Slippi) */
    REPLAY_END_TIME = 1,    /* the timer ran out */
    REPLAY_END_GAME = 2,    /* GAME!: stocks ran out */
    REPLAY_END_QUIT = 7,    /* No Contest: someone quit */
};

/* Reads `path`'s summary into `out`. Returns 0, or -1 when it isn't a
 * Slippi replay. Quick (it reads the start and the end of the file), and
 * safe from any presentation code. */
int replay_read_summary(const char* path, replay_summary* out);

static inline uint16_t slp_be16(const uint8_t* p)
{
    return (uint16_t) (p[0] << 8 | p[1]);
}

static inline uint32_t slp_be32(const uint8_t* p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

/* tgg.replay-player's replay_play, when the player is loaded: from a
 * screen, plays `path`, and the screen comes back when it ends. Returns 0, or
 * -1 with `why`. */
typedef int replay_play_fn(const char* path, const char** why);

#endif
