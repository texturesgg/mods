/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Frozen Stadium: Pokémon Stadium never transforms, and stays the neutral stage
 * for the whole match. Matches Slippi's frozen Stadium toggle
 * (slippi-ssbm-asm Online/Core/Hacks/Stadium/IngameCheckIfFrozen.asm).
 *
 * The game already keeps the stage neutral in Training Mode: when it decides
 * whether to start a transformation (grStadium_801D4548), it asks
 * gm_8018841C whether this is Training Mode. This answers yes to that one
 * question; everywhere else the game gets its own answer.
 */
#include "tgg/game/hooks.h"

#include <melee/gm/gm_1879.h>
#include <melee/gr/grpstadium.h>

static bool deciding;

static void before_decide(Ground_GObj* gobj)
{
    (void) gobj;
    deciding = true;
}
TGG_BEFORE(grStadium_801D4548, before_decide);

static void after_decide(Ground_GObj* gobj)
{
    (void) gobj;
    deciding = false;
}
TGG_AFTER(grStadium_801D4548, after_decide);

static bool training_mode(void)
{
    if (deciding) {
        return true;
    }
    return TGG_ORIGINAL(gm_8018841C)();
}
TGG_REPLACE(gm_8018841C, training_mode);
