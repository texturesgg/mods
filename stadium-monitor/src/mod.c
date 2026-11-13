/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Camera-free Stadium monitor: Pokémon Stadium's big screen leaves its close-up
 * of a fighter when the fighter's position leaves fixed bounds (x -120 to 120,
 * y -20 to 80), instead of when the close-up would leave the camera's view.
 * Leaving the close-up draws from the RNG, so with the game's own rule the
 * camera, which each player's game draws differently, can change the
 * simulation. Matches Slippi's code (slippi-ssbm-asm
 * Common/PSCameraIndependentMonitor), which runs in every Slippi game.
 *
 * The close-up still goes where the game puts it; whether it stays comes from
 * the bounds (the fighter_check event's TGG_CHECK_STADIUM_CLOSE_UP).
 */
#include "tgg/events.h"
#include "tgg/fighter.h"

static void on_check(tgg_fighter fighter, tgg_check* check)
{
    if (check->kind != TGG_CHECK_STADIUM_CLOSE_UP) {
        return;
    }
    const float x = tgg_fighter_x(fighter);
    const float y = tgg_fighter_y(fighter);
    check->pass = !(x < -120.0f || x > 120.0f || y > 80.0f || y < -20.0f);
}
TGG_ON(fighter_check, on_check);
