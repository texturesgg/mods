/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Ledge dash drill: Practice > Ledge dash. Training Mode with nobody else and
 * a clean screen (tgg.practice-stage); the first time you hang on a ledge
 * that's the reset point, and every try goes back to it, so a rep is just the
 * ledge dash itself.
 *
 * A try starts when the fighter leaves the ledge and is over when they're
 * standing on the stage and can act, when they're KO'd, or after TRY_MAX
 * displayed frames. A beat later (BEAT frames, to read the result) it resets.
 * D-pad left while hanging moves the reset point there; D-pad right resets
 * now.
 *
 * It tells for itself when a try is over, so it works without
 * tgg.ledgedash-trainer; with it installed, the trainer's breakdown shows for
 * each try. Everything here is presentation code: it reads the fighter's
 * state and asks practice-stage for snapshots.
 *
 * An offline mod: the port switches it off for netplay.
 */
#include "practice_stage.h"

#include "tgg/action.h"
#include "tgg/events.h"
#include "tgg/fighter.h"
#include "tgg/input.h"
#include "tgg/menu.h"

#include <stddef.h>
#include <string.h>

#define ID "tgg.ledgedash-drill"
/* Displayed frames a try may take, and the beat before the reset */
#define TRY_MAX 150
#define BEAT 40

static void start(void)
{
    static const practice_how how = { .drill = ID, .stage = "melee/final-destination" };
    practice_start(&how);
}

static const tgg_menu_entry entry = {
    .label = "Ledge dash",
    .description = "Hang on the ledge once, and every try puts you back there.",
    .action = start,
};
TGG_MENU_ADD("practice/ledgedash", entry);

enum { WAITING, HANGING, TRYING, DONE };

static struct {
    int phase;
    int frames; /* displayed frames in this phase */
} view;

static int on_ledge(int msid)
{
    return msid == TGG_ACTION_CLIFF_CATCH || msid == TGG_ACTION_CLIFF_WAIT;
}

static int ko(int msid)
{
    return msid >= TGG_ACTION_DEAD_DOWN && msid <= TGG_ACTION_REBIRTH;
}

/* Standing on the stage, past any landing lag */
static int standing(tgg_fighter fighter)
{
    const int action = tgg_fighter_action(fighter);
    return tgg_fighter_grounded(fighter) && action != TGG_ACTION_LANDING_FALL_SPECIAL &&
           action != TGG_ACTION_LANDING && !on_ledge(action);
}

static uint32_t pressed(void)
{
    uint32_t any = 0;
    for (int port = 0; port < 4; port++) {
        tgg_viewer_pad pad;
        if (tgg_viewer_pad_read(port, &pad)) {
            any |= pad.pressed;
        }
    }
    return any;
}

static void on_frame(void)
{
    const char* drill = practice_drill();
    const tgg_fighter fighter = tgg_in_match() ? tgg_fighter_at(0, 0) : TGG_NO_FIGHTER;
    if (drill == NULL || strcmp(drill, ID) != 0 || fighter == TGG_NO_FIGHTER) {
        memset(&view, 0, sizeof view);
        return;
    }
    const int msid = tgg_fighter_action(fighter);
    const uint32_t in = pressed();
    view.frames++;
    switch (view.phase) {
    case WAITING:
        /* the first hang: from the catch, which lasts a few frames */
        if (on_ledge(msid)) {
            practice_reset_here();
            view.phase = HANGING;
            view.frames = 0;
        }
        break;
    case HANGING:
        if (on_ledge(msid)) {
            if ((in & TGG_BUTTON_LEFT) && msid == TGG_ACTION_CLIFF_WAIT) {
                practice_reset_here();
            }
        } else {
            view.phase = TRYING;
            view.frames = 0;
        }
        break;
    case TRYING:
        if (standing(fighter) || ko(msid) || view.frames >= TRY_MAX) {
            view.phase = DONE;
            view.frames = 0;
        }
        break;
    case DONE:
        if (view.frames >= BEAT && practice_reset(0)) {
            view.phase = HANGING;
            view.frames = 0;
        }
        break;
    }
    if ((in & TGG_BUTTON_RIGHT) && view.phase != WAITING && practice_reset(1)) {
        view.phase = HANGING;
        view.frames = 0;
    }
}
TGG_ON(frame, on_frame);
