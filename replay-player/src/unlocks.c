/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * While a replay plays, the save's unlocks are the ones it was recorded with,
 * whatever this game's save holds: a match's setup reads the save's unlocks (a
 * stage's alternate music is rolled for only where it's unlocked), so the
 * replay would play differently on another save. The port's recorder writes
 * them (tgg.unlocks in the metadata); a replay that doesn't say plays with
 * everything unlocked, as netplay does.
 *
 * This game's save isn't changed by it: the game's saves to the memory card
 * wait while the replay plays (tgg_save_hold), and afterwards the save gets
 * back exactly the unlock bits the replay set, keeping whatever else the
 * game recorded, which the save that waited then writes.
 */
#include "player.h"

#include "tgg/control.h"

#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_16F1.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/types.h>

#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(GmSaveData) == SLP_SAVE_SIZE, "the save's size");

static struct {
    int on;
    GmSaveData before; /* the save as the replay found it */
    GmSaveData mask;   /* the bits unlocking changed */
} ul;

/* The game's own unlock-everything calls, as tgg.unlocks makes them */
static void unlock_everything(void)
{
    gmMainLib_GetCardData()->save_data.x186C = 0xFF; /* unlocked features */
    gm_80164F18();                                   /* characters */
    gm_8016468C();                                   /* stages */
    gm_8017297C();                                   /* unlockables */
    gm_801741FC();                                   /* trophies */
}

void unlocks_follow(int playing, const Slp* slp)
{
    if (playing == ul.on) {
        return;
    }
    GmSaveData* save = &gmMainLib_GetCardData()->save_data;
    uint8_t* now = (uint8_t*) save;
    const uint8_t* before = (const uint8_t*) &ul.before;
    uint8_t* mask = (uint8_t*) &ul.mask;
    if (playing) {
        tgg_save_hold(1); /* and a save being written finishes with this game's own */
        memcpy(&ul.before, save, sizeof *save);
        unlock_everything();
        const int recorded = slp->has_unlocks && slp->n_unlocks > 0;
        for (int i = 0; slp->has_unlocks && i < slp->n_unlocks; i++) {
            now[slp->unlock_at[i]] = slp->unlock_value[i];
        }
        size_t changed = 0;
        for (size_t i = 0; i < sizeof *save; i++) {
            mask[i] = now[i] ^ before[i];
            changed += mask[i] != 0;
        }
        ul.on = 1;
        tgg_log("%s while the replay plays%s",
                recorded ? "the recording's unlocks" : "everything unlocked",
                changed == 0 ? " (this save has the same)" : "; this game's save keeps its own");
        return;
    }
    for (size_t i = 0; i < sizeof *save; i++) {
        now[i] = (uint8_t) ((now[i] & ~mask[i]) | (before[i] & mask[i]));
    }
    ul.on = 0;
    tgg_log("the replay's unlocks are back to this game's save");
    tgg_save_hold(0);
}
