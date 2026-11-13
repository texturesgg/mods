/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Legal stages: random stage select picks only the competitive legal stages
 * (Battlefield, Final Destination, Dream Land, Yoshi's Story, Fountain of
 * Dreams, Pokémon Stadium).
 *
 * The game already has a list for this: the Random Stage Switch in the
 * options, kept in the save. Every random pick, the stage select's random
 * button and the Random stage selection rule, asks it. A new save, or one
 * whose list was never changed, has every stage on; this sets the list to the
 * legal stages there. A list the player changed is theirs, and stays as it is.
 *
 * The list counts only once the Random Stage Switch is unlocked, which the
 * port's built-in tgg.unlocks does.
 */
#include "tgg/content.h"
#include "tgg/events.h"
#include "tgg/save.h"

#include <string.h>

static const char* const legal[] = {
    "melee/battlefield",   "melee/final-destination",    "melee/dream-land",
    "melee/yoshis-story",  "melee/fountain-of-dreams",   "melee/pokemon-stadium",
};

static int is_legal(const char* stage)
{
    for (size_t i = 0; stage != NULL && i < sizeof legal / sizeof *legal; i++) {
        if (strcmp(stage, legal[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* The save is ready, loaded or made new: an untouched list becomes the legal
 * stages */
static void on_save_loaded(void)
{
    if (!tgg_save_random_stages_untouched()) {
        return;
    }
    for (int stkind = 0; stkind < tgg_stage_count(); stkind++) {
        const char* stage = tgg_stage_id(stkind);
        if (stage != NULL) {
            tgg_save_set_random_stage(stage, is_legal(stage));
        }
    }
}
TGG_ON(save_loaded, on_save_loaded);

/* The stage select shows only the legal stages */
static void on_stage_offered(tgg_stage_offered* stage)
{
    if (!is_legal(stage->stage)) {
        stage->state = TGG_STAGE_OFF;
    }
}
TGG_ON(stage_offered, on_stage_offered);
