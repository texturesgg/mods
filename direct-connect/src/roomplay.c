/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Scripted room matches, for tests: with TGG_DIRECT_CONNECT_YES=1 and
 * TGG_DIRECT_CONNECT_TIME=<seconds>, a room's games skip the menus once the
 * host starts. Each game sends its pick ("rpick <character> <costume>",
 * TGG_DIRECT_CONNECT_PICK=<character>[:<costume>], Fox in its side's costume
 * by default), the host makes a time match of that many seconds with every
 * game's pick, and they play TGG_DIRECT_CONNECT_MATCHES of them (1 by
 * default), then leave the room. Players see the real menus instead. */
#include "dc.h"

#include "tgg/match.h"
#include "tgg/net.h"
#include "tgg/screen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* const STAGES[] = {
    "melee/battlefield",     "melee/final-destination", "melee/dream-land",
    "melee/yoshis-story",    "melee/fountain-of-dreams", "melee/pokemon-stadium",
};
#define N_STAGES ((int) (sizeof STAGES / sizeof STAGES[0]))

static struct {
    int sent;     /* this game's pick went out */
    int started;  /* netplay_play went through for this match */
    int played;   /* matches played */
    char picks[NETPLAY_MAX_PLAYERS][64];
    int costumes[NETPLAY_MAX_PLAYERS];
    int have[NETPLAY_MAX_PLAYERS];
} rp;

static int env_int(const char* name, int fallback)
{
    const char* v = getenv(name);
    return v != NULL && v[0] != '\0' ? atoi(v) : fallback;
}

int roomplay_wanted(void)
{
    const char* yes = getenv("TGG_DIRECT_CONNECT_YES");
    return dc.room && yes != NULL && strcmp(yes, "1") == 0 &&
           env_int("TGG_DIRECT_CONNECT_TIME", 0) > 0;
}

int roomplay_done(void)
{
    return rp.played >= env_int("TGG_DIRECT_CONNECT_MATCHES", 1);
}

void roomplay_reset(void)
{
    memset(&rp, 0, sizeof rp);
}

int roomplay_got(const char* text, int from)
{
    char word[16], character[64];
    int costume = 0;
    if (sscanf(text, "%15s %63s %d", word, character, &costume) < 2 ||
        strcmp(word, "rpick") != 0)
    {
        return 0;
    }
    if (from >= 0 && from < NETPLAY_MAX_PLAYERS) {
        snprintf(rp.picks[from], sizeof rp.picks[0], "%s", character);
        rp.costumes[from] = costume;
        rp.have[from] = 1;
    }
    return 1;
}

static void own_pick(int side)
{
    const char* pick = getenv("TGG_DIRECT_CONNECT_PICK");
    char character[64] = "melee/fox";
    int costume = side;
    if (pick != NULL && pick[0] != '\0') {
        snprintf(character, sizeof character, "%s", pick);
        char* colon = strrchr(character, ':');
        if (colon != NULL && strchr(colon, '/') == NULL) {
            *colon = '\0';
            costume = atoi(colon + 1);
        }
    }
    snprintf(rp.picks[side], sizeof rp.picks[0], "%s", character);
    rp.costumes[side] = costume;
    rp.have[side] = 1;
}

static void host_play(const netplay_info* info)
{
    for (int side = 0; side < info->players; side++) {
        if (!rp.have[side]) {
            return;
        }
    }
    static tgg_match match;
    tgg_match_defaults(&match);
    const char* stage = STAGES[rp.played % N_STAGES];
    tgg_match_set_stage(&match, stage);
    tgg_match_set_stocks(&match, 0);
    tgg_match_set_time(&match, env_int("TGG_DIRECT_CONNECT_TIME", 60));
    tgg_match_set_items(&match, -1);
    tgg_match_set_pause(&match, 0);
    for (int side = 0; side < info->players; side++) {
        tgg_match_set_player(&match, side, TGG_PLAYER_HUMAN);
        if (tgg_match_set_character(&match, side, rp.picks[side]) != 0) {
            tgg_log("room play: no character %s", rp.picks[side]);
            return;
        }
        tgg_match_set_costume(&match, side, rp.costumes[side]);
    }
    if (netplay_play(&match) == 0) {
        rp.started = 1;
        tgg_log("room play: match %d with %d games on %s", rp.played + 1, info->players, stage);
    }
}

void roomplay_frame(void)
{
    netplay_info info;
    netplay_status(&info);
    if (info.state != NETPLAY_CONNECTED || rp.started || roomplay_done()) {
        return;
    }
    if (!rp.sent) {
        own_pick(info.side);
        char msg[96];
        snprintf(msg, sizeof msg, "rpick %s %d", rp.picks[info.side], rp.costumes[info.side]);
        if (netplay_send(msg, (int) strlen(msg)) != 0) {
            return;
        }
        rp.sent = 1;
    }
    if (info.side == 0) {
        host_play(&info);
    } else if (netplay_play(NULL) == 0) {
        rp.started = 1;
    }
}

void roomplay_enter(int from_match)
{
    if (from_match && rp.started) {
        rp.started = 0;
        rp.played++;
        tgg_log("room play: match %d over", rp.played);
    }
}
