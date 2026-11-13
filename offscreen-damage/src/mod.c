/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Offscreen damage: a fighter takes offscreen damage whenever it's outside the
 * stage's camera bounds, the way Brawl does it, instead of while the game shows
 * it in a magnifying bubble. The damage, the delay between ticks and the
 * percent where it stops are the game's own. Matches Slippi's online code
 * (slippi-ssbm-asm Online/Core/BrawlOffscreenDamage.asm).
 *
 * The game asks each frame whether the fighter counts as off screen (the
 * fighter_check event's TGG_CHECK_OFFSCREEN_DAMAGE); this answers from the
 * camera bounds. The bubbles on screen still come from the game's own answer.
 */
#include "tgg/action.h"
#include "tgg/events.h"
#include "tgg/fighter.h"
#include "tgg/mode.h"
#include "tgg/stage.h"

static int outside_camera(tgg_fighter fighter)
{
    /* The Home-Run Contest's Sandbag never takes offscreen damage */
    if (tgg_mode() == TGG_MODE_HOME_RUN_CONTEST) {
        return 0;
    }
    /* Nor does a fighter already KO'd, or flying into the screen or the
     * distance on a star or screen KO */
    const int action = tgg_fighter_action(fighter);
    if (tgg_fighter_out_of_play(fighter) || action == TGG_ACTION_DEAD_UP_STAR ||
        action == TGG_ACTION_DEAD_UP_FALL)
    {
        return 0;
    }
    const float x = tgg_fighter_x(fighter);
    const float y = tgg_fighter_y(fighter);
    return x < tgg_stage_camera_left() || x > tgg_stage_camera_right() ||
           y > tgg_stage_camera_top() || y < tgg_stage_camera_bottom();
}

static void on_check(tgg_fighter fighter, tgg_check* check)
{
    if (check->kind == TGG_CHECK_OFFSCREEN_DAMAGE) {
        check->pass = outside_camera(fighter);
    }
}
TGG_ON(fighter_check, on_check);
