/* The replay player's API for other mods (tgg.replay-player): read what a
 * Slippi replay holds without playing it, and play one.
 *
 * A mod that uses it lists the player in its manifest's "depends":
 *
 *     "depends": { "tgg.replay-player": "^0.2" }
 *
 * and includes this header (copy it, or add the player's include/ to the
 * include path). */
#ifndef REPLAY_PLAYER_H_API
#define REPLAY_PLAYER_H_API

#include "tgg/mod.h"

#define REPLAY_PLAYER TGG_PROVIDER(tgg_replay_player, "tgg.replay-player", 1)

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
    char missing[160]; /* the Slippi rule mods it needs (tgg.neutral-spawns, ...)
                        * that aren't loaded, ", "-separated, or "": without
                        * them the match plays out differently */
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
TGG_API(REPLAY_PLAYER, int, replay_read_summary, (const char* path, replay_summary* out));

/* From a screen (tgg/screen.h): plays `path`, and the screen comes back
 * when it ends, or when the viewer leaves it (B or Escape). Returns 0, or
 * -1 when it can't be read or no screen is shown; `why` (may be NULL) then
 * says why, for the player to see. */
TGG_API(REPLAY_PLAYER, int, replay_play, (const char* path, const char** why));

#endif
