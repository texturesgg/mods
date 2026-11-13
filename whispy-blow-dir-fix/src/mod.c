/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Whispy blow direction fix: ftLib_GetOpponentsDir, which picks the side
 * Whispy Woods blows toward (and the side other callers aim at), skips a
 * fighter in a Dead action (DeadDown through DeadUpFallHitCameraIce), so a
 * KO'd fighter counts for neither side. The game counts it where its camera
 * bone is, and a screen KO's model follows the camera, which each player's
 * game draws differently. Matches Slippi's online code (slippi-ssbm-asm
 * Online/Core/WhispyBlowDirFix), which runs in Slippi 3.15 and later.
 *
 * The rest is the game's own function: the sum of the sides the other awake
 * fighters (not teammates in a team game) are on, and a coin flip on a tie.
 */
#include "tgg/game/hooks.h"

#include <melee/ft/ftlib.h>
#include <melee/ft/types.h>
#include <melee/gm/gmvs.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/random.h>

/* The last Dead action (ftCo_MS_DeadUpFallHitCameraIce) */
#define LAST_DEAD_ACTION 0xB

static float opponents_dir(Vec3* v, HSD_GObj* gobj)
{
    const Fighter* fp = gobj != NULL ? gobj->user_data : NULL;
    int result = 0;
    for (HSD_GObj* cur = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; cur != NULL;
         cur = cur->next)
    {
        if (ftLib_IsSamePlayer(gobj, cur)) {
            continue;
        }
        const Fighter* cur_fp = cur->user_data;
        if (cur_fp->is_sleeping || (gm_8016B168() && fp != NULL && cur_fp->team == fp->team)) {
            continue;
        }
        Vec3 bone;
        ftLib_GetCameraBonePos(cur, &bone);
        if (cur_fp->motion_id <= LAST_DEAD_ACTION) {
            continue;
        }
        result += bone.x - v->x < 0.0f ? -1 : +1;
    }
    if (result == 0) {
        result = HSD_Randi(2) != 0 ? +1 : -1;
    }
    return result < 0 ? -1.0f : +1.0f;
}
TGG_REPLACE(ftLib_GetOpponentsDir, opponents_dir);
