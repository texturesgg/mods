/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Offscreen damage: a fighter takes offscreen damage whenever it's outside the
 * stage's camera bounds, the way Brawl does it, instead of while the game shows
 * it in a magnifying bubble. The damage, the delay between ticks and the
 * percent where it stops are the game's own. Matches Slippi's online code
 * (slippi-ssbm-asm Online/Core/BrawlOffscreenDamage.asm).
 *
 * Each frame Fighter_procAnim asks ifMagnify_802FC998 whether the fighter is
 * in a bubble. This answers that one question from the camera bounds instead;
 * the bubbles on screen still come from the game's own answer.
 */
#include "tgg/game/hooks.h"

#include <melee/ft/fighter.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/types.h>
#include <melee/gm/forward.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gr/stage.h>
#include <melee/if/ifmagnify.h>

/* The fighter Fighter_procAnim is running for, while it runs */
static const Fighter* animating;

static void before_anim(Fighter_GObj* gobj)
{
    animating = GET_FIGHTER(gobj);
}
TGG_BEFORE(Fighter_procAnim, before_anim);

static void after_anim(Fighter_GObj* gobj)
{
    (void) gobj;
    animating = NULL;
}
TGG_AFTER(Fighter_procAnim, after_anim);

static bool outside_camera(const Fighter* fp)
{
    /* The Home-Run Contest's Sandbag never takes offscreen damage */
    if (gm_GetCurrentGameMode() == GM_HOME_RUN_CONTEST) {
        return false;
    }
    /* Nor does a fighter already KO'd, or flying into the screen or the
     * distance on a star or screen KO */
    if (fp->x221F_b1 || fp->motion_id == ftCo_MS_DeadUpStar ||
        fp->motion_id == ftCo_MS_DeadUpFall)
    {
        return false;
    }
    return fp->cur_pos.x < Stage_GetCamBoundsLeftOffset() ||
           fp->cur_pos.x > Stage_GetCamBoundsRightOffset() ||
           fp->cur_pos.y > Stage_GetCamBoundsTopOffset() ||
           fp->cur_pos.y < Stage_GetCamBoundsBottomOffset();
}

static bool in_bubble(s32 slot)
{
    if (animating != NULL) {
        return outside_camera(animating);
    }
    return TGG_ORIGINAL(ifMagnify_802FC998)(slot);
}
TGG_REPLACE(ifMagnify_802FC998, in_bubble);
