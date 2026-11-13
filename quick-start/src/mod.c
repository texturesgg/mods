/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Quick start: the game skips the opening movie and the title screen and
 * starts at the main menu, or at VS mode's or Training mode's character
 * select, as the Slippi and UnclePunch setups do.
 *
 * tgg_boot_mode does the work: the game goes to the mode the way Start on
 * the title would take it to the main menu, so the unlock checks still run
 * and a waiting challenger still shows up. The memory card scene still
 * runs: a save loads as usual, and a new one is made without a button
 * press. From there the menus are the game's own, so B on a character
 * select goes back to the main menu.
 */
#include "tgg/control.h"
#include "tgg/settings.h"

#include "tgg/mode.h"

/* Where the game starts, in the order of the choices below */
static const int modes[] = { TGG_MODE_MENU, TGG_MODE_VS, TGG_MODE_TRAINING };

static int start;
TGG_SETTING_CHOICE_LOCAL(start, "start", "Start at", 0, "Main menu", "VS mode", "Training mode");

static void init(void)
{
    tgg_boot_mode(modes[start]);
}
TGG_INIT(init);
