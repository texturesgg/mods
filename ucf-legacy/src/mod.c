/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * UCF (Slippi 3.16 and earlier): the Universal Controller Fix as Slippi's
 * builds up to 3.16 ran it, the three codes of slippi-ssbm-asm's
 * External/UCF 0.8/Logic/: dashback (UCF DB.asm), shield drop (UCF SD.asm) and
 * tumble wiggle (UCF Tumble.asm). It reproduces their logic and constants at
 * the same points in the game's own code.
 *
 * Slippi 3.17 moved to UCF 0.84, which the port has built in (tgg.ucf). Turn
 * that off (TGG_UCF=0) to play with this one instead: the two replace the
 * same spot dodge checks, so they can't both be on.
 *
 * The codes read raw stick x from the game's pad queue: the newest entry and
 * the one two entries before it, which are this frame's and the one two frames
 * ago. This keeps those last few raw values of each port, recorded once per
 * logic frame, so it doesn't depend on how the host fills the queue. It's
 * TGG_STATE, so it rolls back.
 */
#include "tgg/events.h"
#include "tgg/game/bridge.h"
#include "tgg/game/hooks.h"

#include <melee/ft/fighter.h>
#include <melee/ft/forward.h>
#include <melee/ft/kinds/ftCommon/ftCo_Escape.h>
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>

#include <math.h>

/* The last four raw stick x values of each controller port, newest at index */
#define HISTORY 4
static struct {
    s8 x[4][HISTORY];
    u8 index;
} s_raw;
TGG_STATE(s_raw);

/* Each logic frame, after the game read its controllers: the queue entry it
 * just read is this frame's raw input. */
static void on_tick(void)
{
    const PadLibData* lib = &HSD_PadLibData;
    const int newest = lib->qread == 0 ? lib->qnum - 1 : lib->qread - 1;
    s_raw.index = (s_raw.index + 1) & (HISTORY - 1);
    for (int port = 0; port < 4; port++) {
        s_raw.x[port][s_raw.index] = lib->queue[newest].stat[port].stickX;
    }
}
TGG_ON(tick, on_tick);

/* How far a port's raw stick x moved in two frames, squared. */
static int flick_x(int port)
{
    if (port >= 4) {
        return 0;
    }
    const int now = s_raw.x[port][s_raw.index];
    const int before = s_raw.x[port][(s_raw.index - 2) & (HISTORY - 1)];
    return (now - before) * (now - before);
}

/*
 * UCF DB.asm (ftCo_Turn_IASA, where a tilt turn flips its facing): on the
 * second frame of the turn, a stick past the dash threshold either way that
 * hasn't been held there for two frames, and whose raw x moved more than 75
 * units in two frames, makes it a smash turn. Popo's Nana is given a stick
 * all the way the new way, so she dashes back too.
 */
static void on_turn(tgg_fighter fighter, tgg_turn* turn)
{
    Fighter_GObj* gobj = tgg_fighter_gobj(fighter);
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->cur_anim_frame != 2.0f) {
        return;
    }
    if (fabsf(fp->input.lstick[0].x) < p_ftCommonData->dash_smash_stick_threshold ||
        fp->active_timer.lstick.x >= 2 || fp->is_sub_fighter ||
        flick_x(fp->pad_port) <= 75 * 75)
    {
        return;
    }
    turn->smash = 1;

    if (FT_CODE(fp->kind) != Ft_Kind_Popo) {
        return;
    }
    Fighter_GObj* nana = Player_GetEntityAtIndex(fp->player_idx, 1);
    struct Fighter_x1A88_xFC_t* pending = nana != NULL ? GET_FIGHTER(nana)->cpu.x444 : NULL;
    if (pending != NULL) {
        pending->facing_dir = fp->facing_dir;
        pending->lstick.x = fp->facing_dir > 0.0f ? 127 : -128;
    }
}
TGG_ON(fighter_turn, on_turn);

/*
 * UCF Tumble.asm (ftCo_DamageFall_IASA): a sideways stick ends tumble on the
 * frame it crossed the threshold, or on the frame after if it was inside the
 * threshold the frame before and its raw x moved more than 75 units in two
 * frames.
 */
static void on_check(tgg_fighter fighter, tgg_check* check)
{
    Fighter_GObj* gobj = tgg_fighter_gobj(fighter);
    if (check->kind != TGG_CHECK_TUMBLE_FALL) {
        return;
    }
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->active_timer.lstick.x != 1) {
        check->pass = fp->active_timer.lstick.x < 1;
    } else {
        check->pass = fabsf(fp->input.lstick[1].x) < p_ftCommonData->x210 &&
                      flick_x(fp->pad_port) > 75 * 75;
    }
}
TGG_ON(fighter_check, on_check);

/* A stick axis in raw units the way UCF SD.asm rounds it: |v| * 80 less
 * 0.00001, truncated, plus 2. */
static int rim_units(float v)
{
    const float scaled = fabsf(v) * 80.0f;
    return (int) (scaled - 9.953975677490234e-06f) + 2;
}

/*
 * UCF SD.asm (ftCo_80099894, the spot dodge): the spot dodge is held off while
 * the stick, on the rim, has been sideways for more than 3 frames and isn't
 * yet down past -0.8 (rolling it down to shield drop), unless the C-stick asks
 * for the spot dodge. The code compares (units / 80) squared, summed, with 1;
 * for every unit count that's the same as comparing the units squared with 80
 * squared.
 */
static bool blocks_spot_dodge(Fighter* fp)
{
    if (fp->input.cstick[0].y <= p_ftCommonData->x314) {
        return false;
    }
    const int x = rim_units(fp->input.lstick[0].x);
    const int y = rim_units(fp->input.lstick[0].y);
    if (x * x + y * y < 80 * 80) {
        return false;
    }
    if (fp->active_timer.lstick.x <= 3) {
        return false;
    }
    return fp->input.lstick[0].y > -0.8f;
}

/* The two checks that start a spot dodge, from a standing state with the
 * shield button down and from the shield. The code returns from whichever one
 * reached the spot dodge as if its own test had failed; both tests are free of
 * side effects, so each check fails here instead. */
static bool spot_dodge_from_state(Fighter_GObj* gobj)
{
    return !blocks_spot_dodge(GET_FIGHTER(gobj)) && TGG_ORIGINAL(ftCo_80099794)(gobj);
}
TGG_REPLACE(ftCo_80099794, spot_dodge_from_state);

static bool spot_dodge_from_shield(Fighter_GObj* gobj)
{
    return !blocks_spot_dodge(GET_FIGHTER(gobj)) && TGG_ORIGINAL(ftCo_8009980C)(gobj);
}
TGG_REPLACE(ftCo_8009980C, spot_dodge_from_shield);
