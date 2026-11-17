/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Freeze glitch fix: stops the Ice Climbers freeze glitch. Matches Slippi's code
 * (slippi-ssbm-asm External/FreezeGlitchFix, by tauKhan), in its General Codes since Slippi
 * 3.7, which removes one store from ftNn_Init_80123954.
 *
 * When Popo starts a special, ftNn_Init_80123954 decides whether Nana can join in. When she's in
 * one of the action kinds 1 to 13 (hurt, grabbed, dead and the like) she can't, and the game also
 * clears her link to the fighter she's tied to (x1A5C). That cleared link is what leaves the
 * opponent frozen. This keeps the link; the answer (she can't join in) is unchanged.
 */
#include "tgg/game/hooks.h"

#include <melee/ft/fighter.h>
#include <melee/ft/kinds/ftNana/ftnana.h>
#include <melee/ft/types.h>

static Fighter_GObj* kept_for;
static Fighter_GObj* kept;

static void before_join(Fighter_GObj* nana_gobj, GroundOrAir ga)
{
    (void) ga;
    kept_for = NULL;
    if (nana_gobj == NULL) {
        return;
    }
    Fighter* nana = GET_FIGHTER(nana_gobj);
    u8 kind = nana->x2070.x0.x2071_b0_3;
    if (kind >= 1 && kind <= 13) {
        kept_for = nana_gobj;
        kept = nana->x1A5C;
    }
}
TGG_BEFORE(ftNn_Init_80123954, before_join);

static void after_join(Fighter_GObj* nana_gobj, GroundOrAir ga)
{
    (void) ga;
    if (nana_gobj != NULL && nana_gobj == kept_for) {
        GET_FIGHTER(nana_gobj)->x1A5C = kept;
    }
    kept_for = NULL;
}
TGG_AFTER(ftNn_Init_80123954, after_join);
