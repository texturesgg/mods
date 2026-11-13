/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Neutral spawns: on the legal stages, singles players start at the stage's
 * neutral spawn points instead of the game's, and 2v2 teams start side by
 * side. Other stages use the stage's own points in player order. Matches
 * Slippi's NeutralSpawn code (slippi-ssbm-asm External/NeutralSpawn).
 *
 * The game stores each player's starting position with Player_80032768 while
 * it sets up the match's players (fn_8016E2BC). This changes that position,
 * and turns the player to face the middle of the stage.
 */
#include "tgg/game/hooks.h"

#include <melee/gm/forward.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmvs.h>
#include <melee/gr/stage.h>
#include <melee/mn/types.h>
#include <melee/pl/player.h>

typedef struct Spawn {
    float x, y;
} Spawn;

typedef struct StageSpawns {
    int stage; /* StKind */
    Spawn singles[4];
    Spawn teams[4]; /* team 1's two players, then team 2's */
} StageSpawns;

static const StageSpawns stages[] = {
    { 32 /* Final Destination */,
      { { -60, 10 }, { 60, 10 }, { -20, 10 }, { 20, 10 } },
      { { -60, 10 }, { -20, 10 }, { 60, 10 }, { 20, 10 } } },
    { 31 /* Battlefield */,
      { { -38.8f, 35.2f }, { 38.8f, 35.2f }, { 0, 8 }, { 0, 62.4f } },
      { { -38.8f, 35.2f }, { -38.8f, 5 }, { 38.8f, 35.2f }, { 38.8f, 5 } } },
    { 8 /* Yoshi's Story */,
      { { -42, 26.6f }, { 42, 28 }, { 0, 46.9f }, { 0, 4.9f } },
      { { -42, 26.6f }, { -42, 5 }, { 42, 28 }, { 42, 5 } } },
    { 28 /* Dream Land */,
      { { -46.6f, 37.2f }, { 47.4f, 37.3f }, { 0, 7 }, { 0, 58.5f } },
      { { -46.6f, 37.2f }, { -46.6f, 5 }, { 47.4f, 37.3f }, { 47.4f, 5 } } },
    { 2 /* Fountain of Dreams */,
      { { -41.25f, 21 }, { 41.25f, 27 }, { 0, 5.25f }, { 0, 48 } },
      { { -41.25f, 21 }, { -41.25f, 5 }, { 41.25f, 27 }, { 41.25f, 5 } } },
    { 3 /* Pokemon Stadium */,
      { { -40, 32 }, { 40, 32 }, { 70, 7 }, { -70, 7 } },
      { { -40, 32 }, { -40, 5 }, { 40, 32 }, { 40, 5 } } },
};

/* On another stage, which of the stage's own points each 2v2 player takes */
static const int other_stage_teams[4] = { 0, 3, 1, 2 };

static bool in_setup;

static void before_setup(void)
{
    in_setup = true;
}
TGG_BEFORE(fn_8016E2BC, before_setup);

static void after_setup(void)
{
    in_setup = false;
}
TGG_AFTER(fn_8016E2BC, after_setup);

static bool present(int slot)
{
    return Player_GetPlayerSlotType(slot) != Gm_PKind_NA;
}

/* Where this player goes among the players, or -1 to leave the game's
 * spawn alone. */
static int spawn_order(int slot, bool teams)
{
    if (!teams) {
        int order = 0;
        for (int i = 0; i < slot; i++) {
            order += present(i);
        }
        return order;
    }
    /* Only 2v2: every team has none or two players */
    for (int team = 0; team < 3; team++) {
        int members = 0;
        for (int i = 0; i < 4; i++) {
            members += present(i) && Player_GetTeam(i) == team;
        }
        if (members == 1 || members > 2) {
            return -1;
        }
    }
    int order = 0;
    for (int team = 0; team < 3; team++) {
        for (int i = 0; i < 4; i++) {
            if (present(i) && Player_GetTeam(i) == team) {
                if (i == slot) {
                    return order;
                }
                order++;
            }
        }
    }
    return -1;
}

static void before_store_spawn(s32 slot, Vec3* pos)
{
    if (!in_setup || slot >= 5 || gm_IsCurrently1PMode_inline()) {
        return;
    }
    const u8 mode = gm_GetCurrentGameMode();
    if (mode == GM_TRAINING || mode == GM_TARGET_TEST) {
        return;
    }
    const StartMeleeRules* rules = gm_GetStartMeleeRules();
    const bool teams = rules->is_teams == 1;
    const int order = spawn_order(slot, teams);
    if (order < 0 || order > 3) {
        return;
    }

    const StageSpawns* stage = NULL;
    for (size_t i = 0; i < sizeof stages / sizeof *stages; i++) {
        if (stages[i].stage == rules->stkind) {
            stage = &stages[i];
        }
    }
    if (stage != NULL) {
        const Spawn* spawn = teams ? &stage->teams[order] : &stage->singles[order];
        pos->x = spawn->x;
        pos->y = spawn->y;
        pos->z = 0;
    } else {
        Stage_80224E64(teams ? other_stage_teams[order] : order, pos);
    }
    Player_SetFacingDirection(slot, pos->x > 0 ? -1.0f : 1.0f);
}
TGG_BEFORE(Player_80032768, before_store_spawn);
