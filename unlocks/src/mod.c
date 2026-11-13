/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tgg.unlocks: every character, stage, unlockable and trophy flag is unlocked.
 *
 * It runs after the game sets up a save from scratch (a memory card with no
 * save, or erasing it) and after it loads one, with the game's own
 * unlock-everything calls, the ones its debug build runs. Unlocking is one way,
 * so a save that already has some of it loses nothing, and the log says when a
 * loaded save gained something. It touches only save data. To unlock things
 * by playing, turn it off.
 */
#include "tgg/game/hooks.h"

#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_16F1.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/types.h>

#include <string.h>

static void unlock_everything(void)
{
    gmMainLib_GetCardData()->save_data.x186C = 0xFF; /* unlocked features */
    gm_80164F18();                                   /* characters */
    gm_8016468C();                                   /* stages */
    gm_8017297C();                                   /* unlockables */
    gm_801741FC();                                   /* trophies */
}

/* gmMainLib_8015F600(1, _) resets the main save block, the one holding the
 * unlocks. */
static void after_save_reset(int block, int from_card)
{
    (void) from_card;
    if (block != 1) {
        return;
    }
    unlock_everything();
}
TGG_AFTER(gmMainLib_8015F600, after_save_reset);

/* The game has loaded the save from the card, or reset the blocks it couldn't
 * load (after_save_reset ran for those). */
static void after_save_load(s32 card_state)
{
    (void) card_state;
    GmSaveData* save = &gmMainLib_GetCardData()->save_data;
    unsigned char before[sizeof *save];
    memcpy(before, save, sizeof before);
    unlock_everything();
    if (memcmp(before, save, sizeof before) != 0) {
        tgg_log("unlocked what the loaded save still had locked");
    }
}
TGG_AFTER(gmMainLib_8015FA34, after_save_load);

