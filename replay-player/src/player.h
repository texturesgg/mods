/* SPDX-License-Identifier: GPL-3.0-or-later */
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

/* The controls the player reads, each nonzero on the frame it's pressed.
 * `take` is the controller that pressed X, plus one (0: none). */
typedef struct Keys {
    int pause, step, faster, slower, normal, leave, rewind, take;
} Keys;

/* takeover.c: rewinding, skipping ahead, and taking over from a replay */
enum { TAKE_RECORDING = -1, TAKE_STILL = -2 }; /* else a controller, 0 to 3 */
/* A new replay's match */
void takeover_reset(const StartMeleeData* match);
/* Its match ended: snapshots and takeover go */
void takeover_match_end(void);
/* Someone took over */
int takeover_active(void);
/* Who plays `port`: TAKE_* or a controller */
int takeover_source(int port);
/* From advance: keeps a rewind snapshot now and then */
void takeover_advance(int replay_frame);
/* Back a couple of seconds; 0 or -1 */
int takeover_rewind(int replay_frame);
/* Ahead about 10 seconds, fast */
void takeover_skip(void);
/* Opens the takeover panel, `controller` (0 to 3) playing the first player */
void takeover_open(int controller);
int takeover_panel_shown(void);
/* The panel's controls; nonzero while it's shown (it takes the frame's input) */
int takeover_panel_frame(void);
/* Back to the takeover point; 0 or -1 */
int takeover_again(void);
/* From hud_draw */
void takeover_draw(void);
/* The controls pressed this displayed frame. */
Keys keys_pressed(void);

#endif
