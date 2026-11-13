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
 * it keeps in TGG_STATE, and the curated layer's fighter and pad reads: it is
 * deterministic and rolls back. It uses the fighter_input, fighter_turn and
 * fighter_check events, on the stable tier.
 *
 * Not here: the CSS indicator text (Indicator/), which has no counterpart in
 * the port's menus yet.
 */
#include "tgg/action.h"
#include "tgg/events.h"
#include "tgg/fighter.h"
#include "tgg/pad.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Zelda's Farore's Wind start, the eighth of her own action states */
#define ZELDA_SPECIAL_HI_START (TGG_ACTION_FIGHTER_FIRST + 8)

/*
 * UCF Pad Buffer + 1.0 Cardinals.asm keeps the last four raw stick values of
 * each controller port, newest at index, and a count of how long a stick has
 * been flicked down and held (the extended shield drop's).
 */
#define UCF_BUFFER 4
static struct {
    int8_t x[UCF_BUFFER];
    int8_t y[UCF_BUFFER];
    uint8_t index;
    uint8_t down;
} s_pad[4];
TGG_STATE(s_pad);

/* The raw stick value `back` entries before the newest. */
static int8_t raw_x(int port, int back)
{
    return s_pad[port].x[(s_pad[port].index - back) & (UCF_BUFFER - 1)];
}

static int8_t raw_y(int port, int back)
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
static void snap_cardinal(int raw_x, int raw_y, float* x, float* y)
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
static void on_input(tgg_fighter fighter, tgg_input* input)
{
    const int port = tgg_fighter_controller(fighter);
    if (tgg_fighter_cpu(fighter) || port < 0) {
        return;
    }
    s_pad[port].index = (s_pad[port].index + 1) & (UCF_BUFFER - 1);
    s_pad[port].x[s_pad[port].index] = (int8_t) tgg_pad_stick_x(port);
    s_pad[port].y[s_pad[port].index] = (int8_t) tgg_pad_stick_y(port);

    /* Farore's Wind aims with the stick's exact value */
    const char* as = tgg_fighter_behaves_as(fighter);
    if (!(as != NULL && strcmp(as, "melee/zelda") == 0 &&
          tgg_fighter_action(fighter) == ZELDA_SPECIAL_HI_START))
    {
        snap_cardinal(tgg_pad_stick_x(port), tgg_pad_stick_y(port), &input->stick_x,
                      &input->stick_y);
        snap_cardinal(tgg_pad_cstick_x(port), tgg_pad_cstick_y(port), &input->cstick_x,
                      &input->cstick_y);
    }

    /* The count starts on a flick down (the stick moved more than 44 raw
     * units in two frames, within a frame of crossing the smash threshold),
     * grows while the stick stays down on the rim past -0.609375, and goes
     * back to zero when it doesn't. It wraps with its byte. */
    uint8_t down = 0;
    if (input->stick_y <= -0.609375f && rim_squared(input->stick_x, input->stick_y) > 80 * 80) {
        down = s_pad[port].down;
        if (down != 0) {
            down++;
        } else if (tgg_fighter_stick_y_age(fighter) <= 1 && flick_y(port) > 44 * 44) {
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
 * makes it a smash turn, and Nana dashes back as well.
 */
static void on_turn(tgg_fighter fighter, tgg_turn* turn)
{
    if (tgg_fighter_follower(fighter) || tgg_fighter_action_frame(fighter) != 2.0f) {
        return;
    }
    /* The game's smash conditions, as its own check reads them */
    const int port = tgg_fighter_controller(fighter);
    if (tgg_fighter_stick_x(fighter) * tgg_fighter_facing(fighter) < tgg_pad_dash_threshold() ||
        tgg_fighter_stick_x_age(fighter) > 1 || port < 0 || flick_x(port) <= 75 * 75)
    {
        return;
    }
    turn->smash = 1;
    turn->follower_too = 1;
}
TGG_ON(fighter_turn, on_turn);

/*
 * UCF Shield Drop.asm (ftCo_80099894, the spot dodge): the spot dodge is held
 * off while a stick being rolled down the rim to straight down (the Axe
 * method) passes through the spot dodge zone first: standing on a platform,
 * the stick held sideways for the roll window, no C-stick spot dodge, the
 * stick on the rim and not yet at -0.8.
 */
static bool blocks_spot_dodge(tgg_fighter fighter)
{
    if (tgg_fighter_cstick_y(fighter) <= tgg_pad_spot_dodge_threshold() ||
        tgg_fighter_stick_x_age(fighter) < tgg_pad_roll_frames())
    {
        return false;
    }
    if (tgg_fighter_stick_y(fighter) <= -0.8f) {
        return false;
    }
    if (!tgg_fighter_on_platform(fighter)) {
        return false;
    }
    return rim_squared(tgg_fighter_stick_x(fighter), tgg_fighter_stick_y(fighter)) > 80 * 80;
}

/*
 * The game's input checks the codes change, each at the point of its own
 * injection:
 *
 * - UCF Shield Drop.asm: the two checks that start a spot dodge (from a
 *   standing state with the shield button down, and from the shield) fail
 *   while blocks_spot_dodge holds. The code returns from the one function
 *   both reach, which makes either check fail.
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
static void on_check(tgg_fighter fighter, tgg_check* check)
{
    const int port = tgg_fighter_controller(fighter);
    const bool have_pad = port >= 0;
    const float prev_x = tgg_fighter_stick_prev_x(fighter);
    const float prev_y = tgg_fighter_stick_prev_y(fighter);

    switch (check->kind) {
    case TGG_CHECK_SPOT_DODGE:
    case TGG_CHECK_SPOT_DODGE_SHIELD:
        if (check->pass && blocks_spot_dodge(fighter)) {
            check->pass = 0;
        }
        break;
    case TGG_CHECK_SDI:
        /* An axis crossed in the last frame, the stick before it inside the
         * threshold, and it moved at least 62 raw units in two frames. */
        if (check->pass) {
            return;
        }
        if (tgg_fighter_stick_x_age_kept(fighter) > 1 && tgg_fighter_stick_y_age_kept(fighter) > 1) {
            return;
        }
        {
            const float yy = prev_y * prev_y;
            const float prev_squared = fmaf(prev_x, prev_x, yy);
            const float min = tgg_pad_sdi_threshold();
            if (min * min <= prev_squared || !have_pad) {
                return;
            }
        }
        check->pass = flick_x(port) + flick_y(port) > 62 * 62;
        break;
    case TGG_CHECK_SHIELD_SDI:
        /* The same from the x axis alone, which isn't compared by magnitude */
        if (check->pass || tgg_fighter_stick_x_age_kept(fighter) > 1 ||
            prev_x >= tgg_pad_sdi_threshold() || !have_pad)
        {
            return;
        }
        check->pass = flick_x(port) > 62 * 62;
        break;
    case TGG_CHECK_TUMBLE_FALL:
        if (tgg_fighter_stick_x_age(fighter) != 1) {
            check->pass = tgg_fighter_stick_x_age(fighter) < 1;
        } else {
            check->pass =
                have_pad && fabsf(prev_x) < tgg_pad_tumble_threshold() && flick_x(port) > 75 * 75;
        }
        break;
    case TGG_CHECK_SQUAT_RELEASE:
        if (tgg_fighter_stick_x_age(fighter) < 1 &&
            rim_squared(tgg_fighter_stick_x(fighter), tgg_fighter_stick_y(fighter)) > 80 * 80)
        {
            check->pass = tgg_fighter_stick_y(fighter) > -0.59f;
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
