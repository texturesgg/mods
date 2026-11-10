/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Freeze screen KO fall: a fighter KO'd off the top that hits the screen
 * (DeadUpFall and its variants) stays where it was KO'd in the simulation; only
 * its model follows the camera. Matches Slippi's online code
 * (slippi-ssbm-asm Online/Core/FreezeDeadUpFallPhysics), which keeps the
 * camera, which each player's game draws differently, out of the simulation.
 *
 * The game keeps the fighter's flight in screen space (mv.x50, moved by
 * mv.x5C each frame) and, when it draws the fighter, writes the camera's
 * inverse view of it into both the model and the fighter's position. Here:
 * - the hit's velocity goes into mv.x48 (y, unused padding there) and mv.x4C
 *   (z) instead of self_vel, which stays zero;
 * - the physics adds that velocity, with gravity on y, to the screen-space
 *   step instead of self_vel;
 * - drawing puts the fighter's position back after the game writes it, so
 *   only the model moves.
 */
#include "tgg/mod.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_0D31.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftdrawcommon.h>
#include <melee/ft/kinds/ftCommon/types.h>
#include <melee/ft/types.h>

#include <string.h>

/* The flight's y velocity, in the 4 bytes Slippi uses (fp+0x2348) */
static float get_vy(const Fighter* fp)
{
    float v;
    memcpy(&v, fp->mv.co.unk_deadup.pad_x48, sizeof v);
    return v;
}

static void set_vy(Fighter* fp, float v)
{
    memcpy(fp->mv.co.unk_deadup.pad_x48, &v, sizeof v);
}

/* The hit sends the fighter into the screen: keep its velocity out of self_vel */
static int anim_step;

static void before_anim(Fighter_GObj* gobj)
{
    anim_step = GET_FIGHTER(gobj)->mv.co.unk_deadup.x44;
}
TGG_BEFORE(ftCo_DeadUpFall_Anim, before_anim);

static void after_anim(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (anim_step == 2 && fp->mv.co.unk_deadup.x44 == 3) {
        set_vy(fp, fp->self_vel.y);
        fp->mv.co.unk_deadup.x4C = fp->self_vel.z;
        fp->self_vel.x = 0;
        fp->self_vel.y = 0;
        fp->self_vel.z = 0;
    }
}
TGG_AFTER(ftCo_DeadUpFall_Anim, after_anim);

static void phys(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->mv.co.unk_deadup.x44 != 3) {
        TGG_ORIGINAL(ftCo_DeadUpFall_Phys)(gobj);
        return;
    }
    const FallCommon* ca = &p_ftCommonData->fall_common;
    float vy = get_vy(fp) - ca->x554;
    const float terminal = -ca->x558;
    if (!(vy > terminal)) {
        vy = terminal;
    }
    set_vy(fp, vy);
    fp->mv.co.unk_deadup.x5C.y = fp->mv.co.unk_deadup.x5C.y + vy;
    fp->mv.co.unk_deadup.x5C.z = fp->mv.co.unk_deadup.x4C + fp->mv.co.unk_deadup.x5C.z;
    if (fp->x2222_b6 && !ftAnim_80070FD0(fp)) {
        return;
    }
    fp->mv.co.unk_deadup.x50.x += fp->mv.co.unk_deadup.x5C.x;
    fp->mv.co.unk_deadup.x50.y += fp->mv.co.unk_deadup.x5C.y;
    fp->mv.co.unk_deadup.x50.z += fp->mv.co.unk_deadup.x5C.z;
    fp->mv.co.unk_deadup.x5C.z = 0;
    fp->mv.co.unk_deadup.x5C.y = 0;
    fp->mv.co.unk_deadup.x5C.x = 0;
}
TGG_REPLACE(ftCo_DeadUpFall_Phys, phys);

/* Drawing: the model follows the camera, the fighter's position doesn't */
static Vec3 drawn_pos;

static void before_draw(HSD_GObj* gobj, intptr_t arg)
{
    (void) arg;
    drawn_pos = GET_FIGHTER(gobj)->cur_pos;
}
TGG_BEFORE(ftDrawCommon_80080E18, before_draw);

static void after_draw(HSD_GObj* gobj, intptr_t arg)
{
    (void) arg;
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->x2220_b7) {
        fp->cur_pos = drawn_pos;
    }
}
TGG_AFTER(ftDrawCommon_80080E18, after_draw);
