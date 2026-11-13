/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tgg.unlocks: every character, stage, unlockable and trophy flag is unlocked.
 *
 * It runs whenever the save is ready, loaded from the memory card or made new
 * (no save on the card, or erasing it), with the game's own unlock-everything
 * calls, the ones its debug build runs. Unlocking is one way, so a save that
 * already has some of it loses nothing, and the log says when a loaded save
 * gained something. It touches only save data. To unlock things by playing,
 * turn it off.
 */
#include "tgg/events.h"
#include "tgg/save.h"

static void on_save_loaded(void)
{
    if (tgg_save_unlock_all()) {
        tgg_log("unlocked what the save still had locked");
    }
}
TGG_ON(save_loaded, on_save_loaded);
