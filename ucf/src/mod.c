/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tgg.ucf 0.84: the Universal Controller Fix as Slippi 3.17 and later run it.
 *
 * UCF makes the game read what a player meant from a stick that different
 * controllers report differently. This follows the eight codes of Slippi's
 * build of UCF 0.84 (slippi-ssbm-asm, External/UCF 0.84/UCF/), whose logic and
 * constants it reproduces: the injected PowerPC replaced by C at the same
 * points in the game's own code, through the public API.
 *
 * Everything it reads is the raw stick history of each controller port, which
 * it keeps in TGG_STATE, and fighter fields: it is deterministic and rolls
 * back. It uses the fighter_input, fighter_turn and fighter_check events and
 * two replacements.
 *
 * Not here: the CSS indicator text (Indicator/), which has no counterpart in
 * the port's menus yet.
 */
#include "tgg/events.h"
#include "tgg/mod.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_0DF1.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/ft/kinds/ftCommon/ftCo_Escape.h>
#include <melee/ft/kinds/ftZelda/forward.h>
#include <melee/ft/types.h>
#include <melee/mp/forward.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>

#include <math.h>

/*
 * UCF Pad Buffer + 1.0 Cardinals.asm keeps the last four raw stick values of
 * each controller port, newest at index, and a count of how long a stick has
 * been flicked down and held (the extended shield drop's).
 */
#define UCF_BUFFER 4
static struct {
    s8 x[UCF_BUFFER];
    s8 y[UCF_BUFFER];
    u8 index;
    u8 down;
} s_pad[4];
TGG_STATE(s_pad);

/* The raw stick value `back` entries before the newest. */
static s8 raw_x(int port, int back)
{
    return s_pad[port].x[(s_pad[port].index - back) & (UCF_BUFFER - 1)];
}

static s8 raw_y(int port, int back)
{
    return s_pad[port].y[(s_pad[port].index - back) & (UCF_BUFFER - 1)];
}

/* How far each raw axis moved in two frames, squared: the flick tests. */
static int flick_x(int port)
{
    const int d = raw_x(port, 0) - raw_x(port, 2);
    return d * d;
}

static int flick_y(int port)
{
    const int d = raw_y(port, 0) - raw_y(port, 2);
    return d * d;
}

/* A stick reading as raw units, rounded down the way the codes do it: the
 * square of (|v| * 80 - 0.0001) + 2 of each axis summed, which is above
 * 80 * 80 on the rim. */
static int rim_squared(float x, float y)
{
    const int rx = (int) fmaf(fabsf(x), 80.0f, -0.0001f) + 2;
    const int ry = (int) fmaf(fabsf(y), 80.0f, -0.0001f) + 2;
    return rx * rx + ry * ry;
}

/* A cardinal: one axis at the rim and the other within 6 of center. */
static void snap_cardinal(s8 raw_x, s8 raw_y, float* x, float* y)
{
    const int snap = 6;
    if ((raw_x <= -80 || raw_x >= 80) && raw_y >= -snap && raw_y <= snap) {
        *x = raw_x < 0 ? -1.0f : 1.0f;
        *y = 0.0f;
    } else if ((raw_y <= -80 || raw_y >= 80) && raw_x >= -snap && raw_x <= snap) {
        *x = 0.0f;
        *y = raw_y < 0 ? -1.0f : 1.0f;
    }
}

/*
 * UCF Pad Buffer + 1.0 Cardinals.asm (Fighter_procInput, after the stick
 * timers): records the raw stick the fighter's input was read from, snaps the
 * main stick's and C-stick's cardinals to exactly 1.0 (Zelda's Farore's Wind
 * start excepted), and counts the frames a stick flicked down stays down. A
 * computer-controlled fighter (Nana) has no controller and none of it.
 */
static void on_input(Fighter_GObj* gobj, tgg_input* input)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ftCo_IsCpuControlled(fp) || fp->pad_port >= 4) {
        return;
    }
    const int port = fp->pad_port;
    /* The newest queue entry: the code reads the one before qread, of 5 */
    const PadLibData* lib = &HSD_PadLibData;
    const int newest = lib->qread == 0 ? 4 : lib->qread - 1;
    const PADStatus* raw = &lib->queue[newest].stat[port];

    s_pad[port].index = (s_pad[port].index + 1) & (UCF_BUFFER - 1);
    s_pad[port].x[s_pad[port].index] = raw->stickX;
    s_pad[port].y[s_pad[port].index] = raw->stickY;

    /* Farore's Wind aims with the stick's exact value */
    _Static_assert(ftZd_MS_SpecialHiStart_0 == 349, "UCF's Farore's Wind start state");
    if (!(FT_CODE(fp->kind) == Ft_Kind_Zelda && fp->motion_id == ftZd_MS_SpecialHiStart_0)) {
        snap_cardinal(raw->stickX, raw->stickY, &input->stick_x, &input->stick_y);
        snap_cardinal(raw->substickX, raw->substickY, &input->cstick_x, &input->cstick_y);
    }

    /* The count starts on a flick down (the stick moved more than 44 raw
     * units in two frames, within a frame of crossing the smash threshold),
     * grows while the stick stays down on the rim past -0.609375, and goes
     * back to zero when it doesn't. It wraps with its byte. */
    u8 down = 0;
    if (input->stick_y <= -0.609375f && rim_squared(input->stick_x, input->stick_y) > 80 * 80) {
        down = s_pad[port].down;
        if (down != 0) {
            down++;
        } else if (fp->active_timer.lstick.y <= 1 && flick_y(port) > 44 * 44) {
            down = 1;
        }
    }
    s_pad[port].down = down;
}
TGG_ON(fighter_input, on_input);

/*
 * UCF Dashback.asm (ftCo_Turn_IASA, where a tilt turn flips its facing): on
 * the second frame of a tilt turn of a controlled fighter, a stick that
 * crossed the smash threshold in the last two frames (the raw x moved more
 * than 75 units in two frames) while meeting the game's own smash conditions
 * makes it a smash turn. Nana's pending input is rewritten so she dashes back
 * as well.
 */
static void on_turn(Fighter_GObj* gobj, tgg_turn* turn)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->is_sub_fighter || fp->cur_anim_frame != 2.0f) {
        return;
    }
    /* The game's smash conditions, as its own check reads them */
    if (fp->input.lstick[0].x * fp->facing_dir < p_ftCommonData->dash_smash_stick_threshold ||
        fp->active_timer.lstick.x > 1 || fp->pad_port >= 4 || flick_x(fp->pad_port) <= 75 * 75)
    {
        return;
    }
    turn->smash = 1;

    /* Nana replays Popo's input a few frames late: make her dash back too */
    Fighter_GObj* nana = Player_GetEntityAtIndex(fp->player_idx, 1);
    struct Fighter_x1A88_xFC_t* pending = nana != NULL ? GET_FIGHTER(nana)->cpu.x444 : NULL;
    if (pending != NULL) {
        pending->facing_dir = fp->facing_dir;
        pending->lstick.x = fp->facing_dir < 0 ? -128 : 127;
    }
}
TGG_ON(fighter_turn, on_turn);

/*
 * UCF Shield Drop.asm (ftCo_80099894, the spot dodge): the spot dodge is held
 * off while a stick being rolled down the rim to straight down (the Axe
 * method) passes through the spot dodge zone first: standing on a platform,
 * the stick held sideways for the roll window, no C-stick spot dodge, the
 * stick on the rim and not yet at -0.8.
 */
static bool blocks_spot_dodge(Fighter* fp)
{
    if (fp->input.cstick[0].y <= p_ftCommonData->x314 ||
        fp->active_timer.lstick.x < p_ftCommonData->x320)
    {
        return false;
    }
    if (fp->input.lstick[0].y <= -0.8f) {
        return false;
    }
    if (fp->coll_data.floor.index == -1 || !(fp->coll_data.floor.flags & LINE_FLAG_PLATFORM)) {
        return false;
    }
    return rim_squared(fp->input.lstick[0].x, fp->input.lstick[0].y) > 80 * 80;
}

/* The two checks that start a spot dodge: from a standing state with the
 * shield button down, and from the shield. The code returns from the one
 * function both reach, which makes either check fail; here each check fails
 * instead. */
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

/*
 * The game's input checks the other codes change, each at the point of its
 * own injection:
 *
 * - UCF SDI.asm (ftCo_Damage_OnEveryHitlag) and UCF Shield SDI.asm
 *   (ftCo_80093240): smash DI that the window test refuses (the stick was
 *   already past the threshold) is allowed for a stick that was just flicked.
 * - UCF Tumble.asm (ftCo_DamageFall_IASA): the window test is the frame the
 *   stick crossed or the one after, and on the one after the stick must have
 *   been below the threshold before and flicked.
 * - UCF DBOOC SquatRv Fix.asm (ftCo_SquatRv_CheckInput): the crouch releases
 *   at -0.59 instead when the stick has just crossed the smash threshold and
 *   sits on the rim.
 * - UCF Shield Drop Extended.asm (ftCo_8009A080): the stick counts as down
 *   for dropping through a platform with the shield held when it was flicked
 *   down and held for more than a frame.
 */
static void on_check(Fighter_GObj* gobj, tgg_check* check)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const int port = fp->pad_port;
    const bool have_pad = port < 4;
    const Vec2* prev = &fp->input.lstick[1];

    switch (check->kind) {
    case TGG_CHECK_SDI:
        /* An axis crossed in the last frame, the stick before it inside the
         * threshold, and it moved at least 62 raw units in two frames. */
        if (check->pass) {
            return;
        }
        if (fp->active_sticky.lstick.x > 1 && fp->active_sticky.lstick.y > 1) {
            return;
        }
        {
            const float yy = prev->y * prev->y;
            const float prev_squared = fmaf(prev->x, prev->x, yy);
            const float min = p_ftCommonData->sdi_min_stick_mag;
            if (min * min <= prev_squared || !have_pad) {
                return;
            }
        }
        check->pass = flick_x(port) + flick_y(port) > 62 * 62;
        break;
    case TGG_CHECK_SHIELD_SDI:
        /* The same from the x axis alone, which isn't compared by magnitude */
        if (check->pass || fp->active_sticky.lstick.x > 1 ||
            prev->x >= p_ftCommonData->sdi_min_stick_mag || !have_pad)
        {
            return;
        }
        check->pass = flick_x(port) > 62 * 62;
        break;
    case TGG_CHECK_TUMBLE_FALL:
        if (fp->active_timer.lstick.x != 1) {
            check->pass = fp->active_timer.lstick.x < 1;
        } else {
            check->pass = have_pad && fabsf(prev->x) < p_ftCommonData->x210 && flick_x(port) > 75 * 75;
        }
        break;
    case TGG_CHECK_SQUAT_RELEASE:
        if (fp->active_timer.lstick.x < 1 &&
            rim_squared(fp->input.lstick[0].x, fp->input.lstick[0].y) > 80 * 80)
        {
            check->pass = fp->input.lstick[0].y > -0.59f;
        }
        break;
    case TGG_CHECK_PLATFORM_DROP:
        if (!check->pass && have_pad) {
            check->pass = s_pad[port].down > 1;
        }
        break;
    }
}
TGG_ON(fighter_check, on_check);
