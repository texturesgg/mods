/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Competitive rules: VS mode starts with the competitive standard (4 stocks,
 * 8:00, items off, team attack on) instead of the game's 2-minute time match
 * with items. The game's own rules screens are where they change.
 *
 * The game sets the VS rules to its defaults every start (rules_defaults);
 * the mod sets the competitive ones then. The item switch is in the save, so
 * a change there stays: the mod only turns items off on a save whose item
 * switch the player never touched.
 */
#include "tgg/events.h"
#include "tgg/save.h"

/* At start, and when the player erases their data */
static void on_rules_defaults(void)
{
    tgg_save_set_rule(TGG_RULE_MODE, TGG_RULE_MODE_STOCK);
    tgg_save_set_rule(TGG_RULE_STOCKS, 4);
    tgg_save_set_rule(TGG_RULE_STOCK_TIME_LIMIT, 8);
    tgg_save_set_rule(TGG_RULE_HANDICAP, 0);
    tgg_save_set_rule(TGG_RULE_DAMAGE_RATIO, 10); /* 1.0 */
    tgg_save_set_rule(TGG_RULE_RANDOM_STAGE, 0);  /* on */
    tgg_save_set_rule(TGG_RULE_FRIENDLY_FIRE, 1); /* doubles is played with it on */
    tgg_save_set_rule(TGG_RULE_PAUSE, 1);
    tgg_save_set_rule(TGG_RULE_SCORE_DISPLAY, 0);
    tgg_save_set_rule(TGG_RULE_SELF_DESTRUCTS, 0); /* -1 */
    tgg_save_set_rule(TGG_RULE_ITEM_FREQUENCY, -1);
}
TGG_ON(rules_defaults, on_rules_defaults);

/* The save loads, or a new one is made: its item switch replaces ours unless
 * it's the game's default, which nobody chose */
static void on_save_loaded(void)
{
    if (tgg_save_rule(TGG_RULE_ITEM_FREQUENCY) ==
        tgg_save_rule_default(TGG_RULE_ITEM_FREQUENCY)) {
        tgg_save_set_rule(TGG_RULE_ITEM_FREQUENCY, -1);
    }
}
TGG_ON(save_loaded, on_save_loaded);
