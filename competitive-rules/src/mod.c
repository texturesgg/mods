/* Competitive rules: VS mode starts with the competitive standard (4 stocks,
 * 8:00, items off, team attack on) instead of the game's 2-minute time match
 * with items. The game's own rules screens are where they change.
 *
 * The game resets VS rules to gmMainLib_DefaultGameRules every start
 * (gmMainLib_8015FBA4); the mod writes the competitive ones after it. The
 * item switch is in the save, so a change there stays: the mod only turns
 * items off on a save whose item switch the player never touched.
 */
#include "tgg/mod.h"

#include <melee/gm/gmmain_lib.h>
#include <melee/gm/types.h>

/* The game's defaults for a new save */
static const struct GamePrefs* defaults;
TGG_GAME_SYMBOL(defaults, "gmMainLib_DefaultGamePrefs");

/* At start, and when the player erases their data */
static void after_rules_reset(void)
{
    GameRules* r = gmMainLib_GetGameRules();
    r->mode = 1; /* stock */
    r->stock_count = 4;
    r->stock_time_limit = 8;
    r->handicap = 0;
    r->damage_ratio = 10; /* 1.0 */
    r->stage_sel = 0;     /* on */
    r->friendly_fire = 1; /* doubles is played with it on */
    r->pause = 1;
    r->score_display = 0;
    r->unk_xc = 0; /* self-destructs -1 */
    gmMainLib_GetGamePrefs()->item_freq = -1;
}
TGG_AFTER(gmMainLib_8015FBA4, after_rules_reset);

/* The save loads, or a new one is made: its item switch replaces ours unless
 * it's the game's default, which nobody chose */
static void after_save_load(s32 card_state)
{
    (void) card_state;
    struct GamePrefs* p = gmMainLib_GetGamePrefs();
    if (p->item_freq == defaults->item_freq) {
        p->item_freq = -1;
    }
}
TGG_AFTER(gmMainLib_8015FA34, after_save_load);
