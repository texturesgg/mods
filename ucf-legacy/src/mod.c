/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * UCF (Slippi 3.16 and earlier): the Universal Controller Fix as Slippi's
 * builds up to 3.16 ran it, the three codes of slippi-ssbm-asm's
 * External/UCF 0.8/Logic/: dashback (UCF DB.asm), shield drop (UCF SD.asm) and
 * tumble wiggle (UCF Tumble.asm). It reproduces their logic and constants at
 * the same points in the game's own code.
 *
 * Slippi 3.17 moved to UCF 0.84 (tgg.ucf). Play with one or the other: both
 * change the same checks.
 *
 * The codes read raw stick x from the game's pad queue: the newest entry and
 * the one two entries before it, which are this frame's and the one two frames
 * ago. This keeps those last few raw values of each port, recorded once per
 * logic frame, so it doesn't depend on how the host fills the queue. It's
 * TGG_STATE, so it rolls back.
 */
#include "tgg/events.h"
#include "tgg/fighter.h"
#include "tgg/pad.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* The last four raw stick x values of each controller port, newest at index */
#define HISTORY 4
static struct {
    int8_t x[4][HISTORY];
    uint8_t index;
} s_raw;
TGG_STATE(s_raw);

/* Each logic frame, after the game read its controllers: this frame's raw
 * input. */
static void on_tick(void)
{
    s_raw.index = (s_raw.index + 1) & (HISTORY - 1);
    for (int port = 0; port < 4; port++) {
        s_raw.x[port][s_raw.index] = (int8_t) tgg_pad_stick_x(port);
    }
}
TGG_ON(tick, on_tick);

/* How far a port's raw stick x moved in two frames, squared. */
static int flick_x(int port)
{
    if (port < 0) {
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
 * units in two frames, makes it a smash turn. Popo's Nana dashes back too.
 */
static void on_turn(tgg_fighter fighter, tgg_turn* turn)
{
    if (tgg_fighter_action_frame(fighter) != 2.0f) {
        return;
    }
    if (fabsf(tgg_fighter_stick_x(fighter)) < tgg_pad_dash_threshold() ||
        tgg_fighter_stick_x_age(fighter) >= 2 || tgg_fighter_follower(fighter) ||
        flick_x(tgg_fighter_controller(fighter)) <= 75 * 75)
    {
        return;
    }
    turn->smash = 1;
    turn->follower_too = 1;
}
TGG_ON(fighter_turn, on_turn);

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
static bool blocks_spot_dodge(tgg_fighter fighter)
{
    if (tgg_fighter_cstick_y(fighter) <= tgg_pad_spot_dodge_threshold()) {
        return false;
    }
    const int x = rim_units(tgg_fighter_stick_x(fighter));
    const int y = rim_units(tgg_fighter_stick_y(fighter));
    if (x * x + y * y < 80 * 80) {
        return false;
    }
    if (tgg_fighter_stick_x_age(fighter) <= 3) {
        return false;
    }
    return tgg_fighter_stick_y(fighter) > -0.8f;
}

/*
 * UCF Tumble.asm (ftCo_DamageFall_IASA): a sideways stick ends tumble on the
 * frame it crossed the threshold, or on the frame after if it was inside the
 * threshold the frame before and its raw x moved more than 75 units in two
 * frames.
 *
 * UCF SD.asm: the two checks that start a spot dodge, from a standing state
 * with the shield button down and from the shield, fail while
 * blocks_spot_dodge holds. The code returns from whichever one reached the
 * spot dodge as if its own test had failed.
 */
static void on_check(tgg_fighter fighter, tgg_check* check)
{
    switch (check->kind) {
    case TGG_CHECK_TUMBLE_FALL:
        if (tgg_fighter_stick_x_age(fighter) != 1) {
            check->pass = tgg_fighter_stick_x_age(fighter) < 1;
        } else {
            check->pass = fabsf(tgg_fighter_stick_prev_x(fighter)) < tgg_pad_tumble_threshold() &&
                          flick_x(tgg_fighter_controller(fighter)) > 75 * 75;
        }
        break;
    case TGG_CHECK_SPOT_DODGE:
    case TGG_CHECK_SPOT_DODGE_SHIELD:
        if (check->pass && blocks_spot_dodge(fighter)) {
            check->pass = 0;
        }
        break;
    }
}
TGG_ON(fighter_check, on_check);
