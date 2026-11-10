/* Legal stages: random stage select picks only the competitive legal stages
 * (Battlefield, Final Destination, Dream Land, Yoshi's Story, Fountain of
 * Dreams, Pokémon Stadium).
 *
 * The game already has a list for this: the Random Stage Switch in the
 * options, kept in the save (GamePrefs.stage_mask). Every random pick, the
 * stage select's random button and the Random stage selection rule, asks it
 * (gm_80164330). A new save, or one whose list was never changed, has every
 * stage on; this sets the list to the legal stages there. A list the player
 * changed is theirs, and stays as it is.
 *
 * The list counts only once the Random Stage Switch is unlocked, which the
 * port's built-in tgg.unlocks does.
 */
#include "tgg/events.h"
#include "tgg/mod.h"

#include <melee/gm/gmmain_lib.h>
#include <melee/gm/types.h>

/* Bits of stage_mask, the stage select's order (mnStageSel_803F06D0's xA) */
#define LEGAL_STAGES                                                           \
    ((1u << 0x18) /* Battlefield */ | (1u << 0x19) /* Final Destination */ |  \
     (1u << 0x1A) /* Dream Land */ | (1u << 0x04) /* Yoshi's Story */ |       \
     (1u << 0x05) /* Fountain of Dreams */ | (1u << 0x07) /* Pokémon Stadium */)

/* What a new save starts with: every stage on */
static const struct GamePrefs* defaults;
TGG_GAME_SYMBOL(defaults, "gmMainLib_DefaultGamePrefs");

static void legal_if_untouched(void)
{
    struct GamePrefs* prefs = gmMainLib_GetGamePrefs();
    if (prefs->stage_mask == defaults->stage_mask) {
        prefs->stage_mask = LEGAL_STAGES;
    }
}

/* gmMainLib_8015F600(1, _) resets the save block holding the list: at start
 * before the load, for a new save, and when the player erases their data. */
static void after_save_reset(int block, int from_card)
{
    (void) from_card;
    if (block == 1) {
        legal_if_untouched();
    }
}
TGG_AFTER(gmMainLib_8015F600, after_save_reset);

/* The save is loaded */
static void after_save_load(s32 card_state)
{
    (void) card_state;
    legal_if_untouched();
}
TGG_AFTER(gmMainLib_8015FA34, after_save_load);

/* The stage select shows only the legal stages */
static void on_stage_offered(tgg_stage_offered* stage)
{
    switch (stage->stkind) {
    case 31: /* Battlefield */
    case 32: /* Final Destination */
    case 28: /* Dream Land */
    case 8:  /* Yoshi's Story */
    case 2:  /* Fountain of Dreams */
    case 3:  /* Pokémon Stadium */
        break;
    default:
        stage->offered = 0;
    }
}
TGG_ON(stage_offered, on_stage_offered);
