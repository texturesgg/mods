/* The replay player's parts: the match setup and rules report (setup.c), the
 * keyboard (keys.c), and the playback itself (mod.c). */
#ifndef REPLAY_PLAYER_H
#define REPLAY_PLAYER_H

#include "tgg/mod.h"

#include "slp.h"

typedef struct StartMeleeData StartMeleeData;

/* The match `slp` recorded, as the game sets one up. */
void player_match(const Slp* slp, StartMeleeData* start);
/* Logs the replay and the Slippi rules its console ran. */
void player_log_rules(const Slp* slp);

/* The keys the player reads, each nonzero on the frame it goes down. */
typedef struct Keys {
    int pause, step, faster, slower, normal;
} Keys;
/* Whether the keyboard can be read; logs how, or why not. Call once. */
int keys_open(void);
/* The keys pressed since the last call. */
Keys keys_pressed(void);

#endif
