/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Neutral spawns: on the legal stages, singles players start at the stage's
 * neutral spawn points instead of the game's, and 2v2 teams start side by
 * side. Other stages use the stage's own points in player order. Matches
 * Slippi's NeutralSpawn code (slippi-ssbm-asm External/NeutralSpawn).
 *
 * As a match is set up, each player's starting point comes through the
 * player_spawn event: this moves it, and turns the player to face the middle
 * of the stage.
 */
#include "tgg/events.h"
#include "tgg/mode.h"
#include "tgg/player.h"
#include "tgg/stage.h"

#include <string.h>

typedef struct Spawn {
    float x, y;
} Spawn;

typedef struct StageSpawns {
    const char* stage;
    Spawn singles[4];
    Spawn teams[4]; /* team 1's two players, then team 2's */
} StageSpawns;

static const StageSpawns stages[] = {
    { "melee/final-destination",
      { { -60, 10 }, { 60, 10 }, { -20, 10 }, { 20, 10 } },
      { { -60, 10 }, { -20, 10 }, { 60, 10 }, { 20, 10 } } },
    { "melee/battlefield",
      { { -38.8f, 35.2f }, { 38.8f, 35.2f }, { 0, 8 }, { 0, 62.4f } },
      { { -38.8f, 35.2f }, { -38.8f, 5 }, { 38.8f, 35.2f }, { 38.8f, 5 } } },
    { "melee/yoshis-story",
      { { -42, 26.6f }, { 42, 28 }, { 0, 46.9f }, { 0, 4.9f } },
      { { -42, 26.6f }, { -42, 5 }, { 42, 28 }, { 42, 5 } } },
    { "melee/dream-land",
      { { -46.6f, 37.2f }, { 47.4f, 37.3f }, { 0, 7 }, { 0, 58.5f } },
      { { -46.6f, 37.2f }, { -46.6f, 5 }, { 47.4f, 37.3f }, { 47.4f, 5 } } },
    { "melee/fountain-of-dreams",
      { { -41.25f, 21 }, { 41.25f, 27 }, { 0, 5.25f }, { 0, 48 } },
      { { -41.25f, 21 }, { -41.25f, 5 }, { 41.25f, 27 }, { 41.25f, 5 } } },
    { "melee/pokemon-stadium",
      { { -40, 32 }, { 40, 32 }, { 70, 7 }, { -70, 7 } },
      { { -40, 32 }, { -40, 5 }, { 40, 32 }, { 40, 5 } } },
};

/* On another stage, which of the stage's own points each 2v2 player takes */
static const int other_stage_teams[4] = { 0, 3, 1, 2 };

static int present(int slot)
{
    return tgg_player_kind(slot) != TGG_PLAYER_NONE;
}

/* Where this player goes among the players, or -1 to leave the game's
 * spawn alone. */
static int spawn_order(int slot, int teams)
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
            members += present(i) && tgg_player_team(i) == team;
        }
        if (members == 1 || members > 2) {
            return -1;
        }
    }
    int order = 0;
    for (int team = 0; team < 3; team++) {
        for (int i = 0; i < 4; i++) {
            if (present(i) && tgg_player_team(i) == team) {
                if (i == slot) {
                    return order;
                }
                order++;
            }
        }
    }
    return -1;
}

static void on_player_spawn(tgg_spawn* spawn)
{
    if (spawn->port >= 5 || tgg_mode_single_player()) {
        return;
    }
    const int mode = tgg_mode();
    if (mode == TGG_MODE_TRAINING || mode == TGG_MODE_TARGET_TEST) {
        return;
    }
    const int order = spawn_order(spawn->port, spawn->teams);
    if (order < 0 || order > 3) {
        return;
    }

    const char* id = tgg_stage();
    const StageSpawns* stage = NULL;
    for (size_t i = 0; id != NULL && i < sizeof stages / sizeof *stages; i++) {
        if (strcmp(stages[i].stage, id) == 0) {
            stage = &stages[i];
        }
    }
    if (stage != NULL) {
        const Spawn* at = spawn->teams ? &stage->teams[order] : &stage->singles[order];
        spawn->x = at->x;
        spawn->y = at->y;
    } else {
        const int index = spawn->teams ? other_stage_teams[order] : order;
        spawn->x = tgg_stage_spawn_x(index);
        spawn->y = tgg_stage_spawn_y(index);
    }
    spawn->facing = spawn->x > 0 ? -1.0f : 1.0f;
}
TGG_ON(player_spawn, on_player_spawn);
