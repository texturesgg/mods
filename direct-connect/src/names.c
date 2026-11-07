/* The character grid in the character select's order, and the stages the
 * host picks from (the competitive ones first). */
#include "dc.h"

#include "tgg/draw.h"

#include <melee/ft/forward.h>

static const int grid[GRID_CELLS] = {
    /* Top row */
    CKind_DrMario, CKind_Mario, CKind_Luigi, CKind_Koopa, CKind_Peach, CKind_Yoshi, CKind_Donkey,
    CKind_Captain, CKind_Ganon,
    /* Middle row */
    CKind_Falco, CKind_Fox, CKind_Ness, CKind_PopoNana, CKind_Kirby, CKind_Samus, CKind_Zelda,
    CKind_Link, CKind_CLink,
    /* Bottom row, with Sheik, whom the game's select reaches through Zelda */
    CKind_Pichu, CKind_Pikachu, CKind_Purin, CKind_Mewtwo, CKind_GameWatch, CKind_Mars,
    CKind_Emblem, CKind_Seak,
};

int grid_ckind(int cell)
{
    return cell >= 0 && cell < GRID_CELLS ? grid[cell] : CKind_Fox;
}

int grid_cell(int ckind)
{
    for (int i = 0; i < GRID_CELLS; i++) {
        if (grid[i] == ckind) {
            return i;
        }
    }
    return 10;
}

#define NAME(kind, name) name,
#define KIND(kind, name) kind,
static const char* const fighters[] = { FIGHTERS(NAME) };
static const char* const stage_names[] = { STAGES(NAME) };
static const int stage_kinds[] = { STAGES(KIND) };

const char* fighter_name(int ckind)
{
    return ckind >= 0 && ckind < (int) (sizeof fighters / sizeof *fighters) ? fighters[ckind]
                                                                              : "?";
}

int stage_count(void)
{
    return (int) (sizeof stage_kinds / sizeof *stage_kinds);
}

int stage_kind(int index)
{
    return index >= 0 && index < stage_count() ? stage_kinds[index] : stage_kinds[0];
}

const char* stage_name(int index)
{
    return index >= 0 && index < stage_count() ? stage_names[index] : stage_names[0];
}

int next_costume(int ckind, int costume, int dir)
{
    int count = 0;
    while (count < 6 && tgg_image_stock_icon(ckind, count) != NULL) {
        count++;
    }
    if (count == 0) {
        return 0;
    }
    return ((costume + dir) % count + count) % count;
}
