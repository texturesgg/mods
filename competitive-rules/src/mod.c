/* Competitive rules: VS mode starts with the competitive standard (4 stocks,
 * 8:00, items off, team attack on) instead of the game's 2-minute time match
 * with items, and the rules the player picks in the menus stay picked from
 * one start to the next.
 *
 * The game doesn't save VS rules: it resets them to gmMainLib_DefaultGameRules
 * every start (gmMainLib_8015FBA4). Only the item switch is in the save. So
 * the mod keeps every value of the rules menus in its settings, whose defaults
 * are the competitive ones, and puts them in place after the game resets its
 * rules and after it loads the save.
 *
 * Only the rules menus are the player's choice: Custom Rules and the character
 * select's rules banner (mnmainrule.c), Additional Rules (mnruleplus.c) and
 * the item switch (mnitemsw.c). After each of their frames the mod saves what
 * they changed. Each displayed frame outside a match it compares the rules
 * with the settings: a change on the Mods page goes to the game, and any other
 * change in the game is put back. Tournament mode changes the handicap itself,
 * so its changes stay until it ends.
 */
#include "tgg/events.h"
#include "tgg/settings.h"

#include <melee/gm/forward.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/types.h>
#include <melee/mn/mnitemsw.h>
#include <melee/mn/mnmainrule.h>
#include <melee/mn/mnruleplus.h>

#include <string.h>

enum {
    MODE,       /* GameRules.mode: Time, Stock, Coin, Bonus */
    STOCKS,     /* stock_count, 1 to 99 */
    STOCK_TIME, /* stock_time_limit: minutes in a stock match, 0 for none */
    TIME,       /* time_limit: minutes in a time match, 0 for none */
    ITEMS,      /* GamePrefs.item_freq + 1: 0 is None */
    HANDICAP,   /* handicap: Off, Auto, On */
    DAMAGE,     /* damage_ratio - 5: 0.5 to 2.0 */
    STAGE_SEL,  /* stage_sel */
    TEAM_ATK,   /* friendly_fire */
    PAUSE,      /* pause */
    SCORE,      /* score_display */
    SD_PENALTY, /* unk_xc: the game's order is -1, 0, -2 */
    N_RULES,
};

static int rule[N_RULES];
TGG_SETTING_CHOICE(rule[MODE], "mode", "Mode", 1, "Time", "Stock", "Coin", "Bonus");
TGG_SETTING_INT(rule[STOCKS], "stocks", "Stocks", 4, 1, 99);
TGG_SETTING_INT(rule[STOCK_TIME], "stock-time", "Stock time limit (min, 0 = none)", 8, 0, 99);
TGG_SETTING_INT(rule[TIME], "time", "Time match (min, 0 = none)", 2, 0, 99);
TGG_SETTING_CHOICE(rule[ITEMS], "items", "Items", 0,
                   "None", "Very low", "Low", "Medium", "High", "Very high");
TGG_SETTING_CHOICE(rule[HANDICAP], "handicap", "Handicap", 0, "Off", "Auto", "On");
TGG_SETTING_CHOICE(rule[DAMAGE], "damage", "Damage ratio", 5,
                   "0.5", "0.6", "0.7", "0.8", "0.9", "1.0", "1.1", "1.2",
                   "1.3", "1.4", "1.5", "1.6", "1.7", "1.8", "1.9", "2.0");
TGG_SETTING_CHOICE(rule[STAGE_SEL], "stage-select", "Stage selection", 0,
                   "On", "Random", "Ordered", "Turns", "Loser");
TGG_SETTING_BOOL(rule[TEAM_ATK], "team-attack", "Team attack", 1);
TGG_SETTING_BOOL(rule[PAUSE], "pause", "Pause", 1);
TGG_SETTING_BOOL(rule[SCORE], "score", "Score display", 0);
TGG_SETTING_CHOICE(rule[SD_PENALTY], "sd-penalty", "SD penalty", 0, "-1", "0", "-2");

/* What the game had when the settings and the game last agreed */
static int synced[N_RULES];
/* The save is loaded: until then the item switch holds the game's default */
static bool ready;

static void read_game(int out[N_RULES])
{
    const GameRules* r = gmMainLib_GetGameRules();
    out[MODE] = r->mode;
    out[STOCKS] = r->stock_count;
    out[STOCK_TIME] = r->stock_time_limit;
    out[TIME] = r->time_limit;
    out[ITEMS] = (u8) (gmMainLib_GetGamePrefs()->item_freq + 1);
    out[HANDICAP] = r->handicap;
    out[DAMAGE] = r->damage_ratio - 5;
    out[STAGE_SEL] = r->stage_sel;
    out[TEAM_ATK] = r->friendly_fire;
    out[PAUSE] = r->pause;
    out[SCORE] = r->score_display;
    out[SD_PENALTY] = r->unk_xc;
}

static void write_game(const int in[N_RULES])
{
    GameRules* r = gmMainLib_GetGameRules();
    r->mode = in[MODE];
    r->stock_count = in[STOCKS];
    r->stock_time_limit = in[STOCK_TIME];
    r->time_limit = in[TIME];
    gmMainLib_GetGamePrefs()->item_freq = in[ITEMS] - 1; /* None is 0xFF */
    r->handicap = in[HANDICAP];
    r->damage_ratio = in[DAMAGE] + 5;
    r->stage_sel = in[STAGE_SEL];
    r->friendly_fire = in[TEAM_ATK];
    r->pause = in[PAUSE];
    r->score_display = in[SCORE];
    r->unk_xc = in[SD_PENALTY];
}

static void apply(void)
{
    write_game(rule);
    memcpy(synced, rule, sizeof synced);
}

/* At start, and when the player erases their data */
static void after_rules_reset(void)
{
    apply();
}
TGG_AFTER(gmMainLib_8015FBA4, after_rules_reset);

/* The save is loaded, or made: its item switch replaced ours */
static void after_save_load(s32 card_state)
{
    (void) card_state;
    apply();
    ready = true;
}
TGG_AFTER(gmMainLib_8015FA34, after_save_load);

/* A rules menu ran its frame: what it changed is the player's choice */
static void after_rules_menu(HSD_GObj* gobj)
{
    (void) gobj;
    if (!ready) {
        return;
    }
    int game[N_RULES];
    read_game(game);
    for (int i = 0; i < N_RULES; i++) {
        if (game[i] != synced[i]) {
            rule[i] = game[i];
            tgg_setting_store(&rule[i]);
            /* a value the setting can't hold comes back to the game on the
             * next frame, as one changed on the Mods page */
            synced[i] = game[i];
        }
    }
}
TGG_AFTER(fn_8022F538, after_rules_menu); /* Custom Rules' input */
TGG_AFTER(fn_802309F0, after_rules_menu); /* Custom Rules' values */
TGG_AFTER(fn_8023201C, after_rules_menu); /* Additional Rules' input */
TGG_AFTER(fn_80232F44, after_rules_menu); /* Additional Rules' values */
TGG_AFTER(fn_80233E10, after_rules_menu); /* item switch's input */
TGG_AFTER(fn_80234C24, after_rules_menu); /* item switch's values */

/* Tournament mode changed the rules: they go back when it ends */
static bool in_tournament;

static void on_frame(void)
{
    if (!ready || tgg_in_match()) {
        return;
    }
    int game[N_RULES];
    read_game(game);
    if (gm_GetCurrentGameMode() == GM_TOURNAMENT) {
        if (memcmp(game, synced, sizeof game) != 0) {
            memcpy(synced, game, sizeof synced);
            in_tournament = true;
        }
        return;
    }
    if (in_tournament) {
        in_tournament = false;
        apply();
        return;
    }
    if (memcmp(rule, synced, sizeof rule) != 0) {
        /* changed on the Mods page */
        apply();
        return;
    }
    if (memcmp(game, synced, sizeof game) != 0) {
        /* changed outside the rules menus: not the player's choice */
        for (int i = 0; i < N_RULES; i++) {
            if (game[i] != synced[i]) {
                tgg_log("rule %d changed outside the rules menus (%d, was %d): put back", i,
                        game[i], synced[i]);
            }
        }
        apply();
    }
}
TGG_ON(frame, on_frame);
