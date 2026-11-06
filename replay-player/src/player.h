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

/* The controls the player reads, each nonzero on the frame it's pressed. */
typedef struct Keys {
    int pause, step, faster, slower, normal, leave;
} Keys;
/* The controls pressed this displayed frame. */
Keys keys_pressed(void);

#endif
