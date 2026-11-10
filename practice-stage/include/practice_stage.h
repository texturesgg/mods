/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The practice stage's API for drills (tgg.practice-stage): start Training
 * Mode made empty for a drill, ask which drill is on, and keep a reset point.
 *
 * A drill that wants it without needing it looks the functions up:
 *
 *     void (*start)(const practice_how*) = (void (*)(const practice_how*))
 *         tgg_mod_export("tgg.practice-stage", "practice_start@1");
 *
 * or lists "tgg.practice-stage": "^0.1" in its manifest's "depends" and
 * includes this header. Every function is presentation code: a menu entry's
 * action, the frame event, hud_draw. */
#ifndef PRACTICE_STAGE_H
#define PRACTICE_STAGE_H

#include "tgg/mod.h"

#define PRACTICE_STAGE TGG_PROVIDER(tgg_practice_stage, "tgg.practice-stage", 1)

/* How a drill wants its Training Mode */
typedef struct practice_how {
    const char* drill; /* the drill's mod id, what practice_drill() answers */
    int keep_cpu;      /* nonzero: leave Training's CPU in */
    int keep_screen;   /* nonzero: leave Training's readouts on screen */
    int stage;         /* a StKind the stage select starts on (still free to
                        * move), or 0 */
} practice_how;

/* From a menu entry's action: opens Training Mode's character select, and
 * the Training match after it is the drill's, as `how` says. */
TGG_API(PRACTICE_STAGE, void, practice_start, (const practice_how* how));

/* The drill this Training Mode was started for (its mod id), or NULL. */
TGG_API(PRACTICE_STAGE, const char*, practice_drill, (void));

/* Keeps this moment as the reset point, replacing the last; "Ready" shows.
 * In a match only. */
TGG_API(PRACTICE_STAGE, void, practice_reset_here, (void));

/* Puts the match back to the reset point, with "Reset" on screen when `say`
 * is nonzero (a reset the player asked for; a drill's own resets after every
 * try are quiet). Nonzero if there was one. */
TGG_API(PRACTICE_STAGE, int, practice_reset, (int say));

/* Nonzero once a reset point is kept in this match. */
TGG_API(PRACTICE_STAGE, int, practice_has_reset, (void));

#endif
