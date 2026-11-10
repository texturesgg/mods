/* The replay player's parts: the match setup and rules report (setup.c), the
 * controls (keys.c), the summary other mods read (summary.c), and the
 * playback itself (mod.c). */
#ifndef REPLAY_PLAYER_H
#define REPLAY_PLAYER_H

#include "tgg/mod.h"

#include "slp.h"

typedef struct StartMeleeData StartMeleeData;

/* The match `slp` recorded, as the game sets one up. */
void player_match(const Slp* slp, StartMeleeData* start);
/* Logs the replay and the Slippi rules its console ran. */
void player_log_rules(const Slp* slp);

/* unlocks.c: everything unlocked while `playing`, this game's save kept */
void unlocks_follow(int playing);
/* The Slippi rule mods a console's gecko list (and its Stadium
 * transformations, -1 when unknown) call for, ", "-separated into `out`;
 * with `missing_only`, only those that aren't loaded. Returns how many. */
int player_rule_mods(const uint8_t* gecko, size_t len, int stage, int transformations,
                     int missing_only, char* out, size_t cap);

/* The controls the player reads, each nonzero on the frame it's pressed. */
typedef struct Keys {
    int pause, step, faster, slower, normal, leave;
} Keys;
/* The controls pressed this displayed frame. */
Keys keys_pressed(void);

#endif
