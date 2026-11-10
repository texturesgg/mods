/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Practice menu: 1-P Mode gets "Practice", after Training, which opens the
 * "practice" submenu. It has no practice of its own: each drill mod adds its
 * own entry there ("practice/<drill>"), so the menu shows whatever drills are
 * installed. An offline mod, like the drills: off in netplay. */
#include "tgg/menu.h"

static const tgg_menu_entry practice = {
    .label = "Practice",
    .description = "Drills on an empty stage, with resets.",
    .after = "1p/training",
    .submenu = "practice",
    .picture = "1p/training",
};
TGG_MENU_ADD("1p/practice", practice);
