/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Prevent wobbling: in a singles match, an Ice Climber holding someone lets go after the fourth
 * pummel that lands while the one held never got back to waiting in the grab, which is what
 * Nana's hits do during a wobble. Matches Slippi's online code (slippi-ssbm-asm
 * External/PreventWobbling, by UnclePunch), in every online game from Slippi 3.14 on.
 *
 * - When the one held starts waiting in the grab again (CaptureWaitHi or CaptureWaitLw), the
 *   count starts over.
 * - When a pummel lands (the hit with no knockback while the grabber takes none), it counts if it
 *   came from the grabber, or from an item the grabber owns, and is a new attack (not the same
 *   attack instance as the last counted one). Only a grabber with a follower (Popo with Nana)
 *   counts at all.
 * - On the fourth, in singles, the grabber lets go (CatchCut) and the game skips the held
 *   fighter's damage reaction. Nana, unless she's dead, frozen or in a star or screen KO, lets go
 *   too: CatchCut on the ground, CaptureJump in the air.
 *
 * The count and the last attack live where Slippi keeps them, in the held fighter's action
 * variables (fp+0x2384 and fp+0x2386 on the console), so they roll back with the match and share
 * those bytes with whatever the current action keeps there, as on the console.
 */
#include "tgg/game/hooks.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_0C8C.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/kinds/ftCommon/ftCo_Attack100.h>
#include <melee/ft/kinds/ftCommon/ftCo_Damage.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/types.h>
#include <melee/gm/gmvs.h>
#include <melee/it/forward.h>
#include <melee/it/inlines.h>
#include <melee/it/types.h>
#include <melee/pl/player.h>

#include <string.h>

#define MAX_WOBBLES 3
#define COUNT_AT 0x44 /* fp+0x2384 */
#define LAST_AT 0x46  /* fp+0x2386 */

static u8 get_count(const Fighter* fp)
{
    return fp->mv._[COUNT_AT];
}

static void set_count(Fighter* fp, u8 count)
{
    fp->mv._[COUNT_AT] = count;
}

static u16 get_last(const Fighter* fp)
{
    u16 v;
    memcpy(&v, &fp->mv._[LAST_AT], sizeof v);
    return v;
}

static void set_last(Fighter* fp, u16 v)
{
    memcpy(&fp->mv._[LAST_AT], &v, sizeof v);
}

/* The count starts over when the held fighter waits in the grab again */
static void start_count(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    set_count(fp, 0);
    set_last(fp, 0xFFFF);
}
TGG_AFTER(fn_800DB790, start_count);
TGG_AFTER(fn_800DBAE4, start_count);

/* The game reacts to a hit in ftCo_8008EC90. Slippi's check sits after the call to
 * ftCo_800C8D00 on its pummel path: the one hit has knockback, the grabber has none. */
static Fighter_GObj* damaged;
static Fighter_GObj* pummeled;
static Fighter_GObj* broke;

static void before_damage(Fighter_GObj* gobj)
{
    damaged = gobj;
}
TGG_BEFORE(ftCo_8008EC90, before_damage);

static void after_damage(Fighter_GObj* gobj)
{
    (void) gobj;
    damaged = NULL;
    pummeled = NULL;
    broke = NULL;
}
TGG_AFTER(ftCo_8008EC90, after_damage);

static void before_reaction(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    pummeled = NULL;
    if (gobj != damaged || fp->x2220_b3 || fp->x2220_b4 || !fp->dmg.kb_applied ||
        fp->victim_gobj == NULL)
    {
        return;
    }
    if (!GET_FIGHTER(fp->victim_gobj)->dmg.kb_applied) {
        pummeled = gobj;
    }
}
TGG_BEFORE(ftCo_800C8D00, before_reaction);

/* Nana lets go too, on the ground or in the air */
static void release_nana(Fighter* grabber)
{
    Fighter_GObj* nana_gobj = Player_GetEntityAtIndex(grabber->player_idx, 1);
    if (nana_gobj == NULL) {
        return;
    }
    Fighter* nana = GET_FIGHTER(nana_gobj);
    if (nana->x221F_b1 || nana->x2219_b5 || nana->x2070.x0.x2071_b0_3 == 13) {
        return;
    }
    if (nana->ground_or_air == GA_Ground) {
        ftCo_800DA698(nana_gobj, false);
        return;
    }
    ftCommon_8007D5D4(nana);
    nana->self_vel.x = p_ftCommonData->x374 * -nana->facing_dir;
    nana->self_vel.y = p_ftCommonData->x378;
    nana->mv.co.buryjump.x0 = 0;
    Fighter_ChangeMotionState(nana_gobj, ftCo_MS_CaptureJump, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
}

static void after_reaction(Fighter_GObj* gobj)
{
    if (gobj != pummeled) {
        return;
    }
    pummeled = NULL;
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->motion_id < ftCo_MS_CapturePulledHi || fp->motion_id > ftCo_MS_CaptureDamageLw) {
        return;
    }
    Fighter_GObj* grabber_gobj = fp->victim_gobj;
    if (grabber_gobj == NULL) {
        return;
    }
    Fighter* grabber = GET_FIGHTER(grabber_gobj);
    if (!grabber->x2222_b5) {
        return;
    }
    HSD_GObj* source = fp->dmg.x1868_source;
    u16 attack;
    if (source == (HSD_GObj*) grabber_gobj) {
        attack = grabber->x2074.x2088;
    } else if (source != NULL && source->classifier == HSD_GOBJ_CLASS_ITEM &&
               GET_ITEM(source)->owner == (HSD_GObj*) grabber_gobj)
    {
        attack = GET_ITEM(source)->xDA8_short;
    } else {
        return;
    }
    u16 last = get_last(fp);
    if (attack == last) {
        return;
    }
    set_last(fp, attack);
    set_count(fp, get_count(fp) + 1);
    if (gm_8016B168() || get_count(fp) <= MAX_WOBBLES) {
        return;
    }
    /* Slippi's call leaves the previous attack in the "the held fighter lets go too" argument */
    ftCo_800DA698(grabber_gobj, last != 0);
    release_nana(grabber);
    broke = gobj;
}
TGG_AFTER(ftCo_800C8D00, after_reaction);

/* After a break the game skips the held fighter's damage reaction */
static void capture_damage_hi(Fighter_GObj* gobj)
{
    if (gobj != broke) {
        TGG_ORIGINAL(ftCo_800DC284)(gobj);
    }
}
TGG_REPLACE(ftCo_800DC284, capture_damage_hi);

static void capture_damage_lw(Fighter_GObj* gobj)
{
    if (gobj != broke) {
        TGG_ORIGINAL(ftCo_800DC3A4)(gobj);
    }
}
TGG_REPLACE(ftCo_800DC3A4, capture_damage_lw);
