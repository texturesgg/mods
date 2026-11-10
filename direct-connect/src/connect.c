/* Direct connect: a screen on the Mods page to play someone over the network
 * with tgg.netplay. One player hosts and tells the other their address; the
 * other joins it. Then both pick a character, the host picks the stage, and
 * when both press Start the match begins on both games. After it, both are
 * back here for the next one.
 *
 * The two screens talk in short text messages through netplay_send:
 *   "pick <ckind> <costume> <ready>"     each side's character
 *   "stage <stkind index> <stocks> <minutes>"   the host's stage and rules
 * Nothing here is part of a match: the match is the StartMeleeData the host
 * builds, which netplay hands to both games. */
#include "dc.h"

#include "tgg/control.h"
#include "tgg/events.h"
#include "tgg/files.h"
#include "tgg/input.h"
#include "tgg/screen.h"
#include "tgg/settings.h"

#include <melee/gm/forward.h>
#include <melee/gm/gm_1601.h>
#include <melee/mn/types.h>
#include <melee/pl/forward.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Dc dc;

char setting_peer[ADDRESS_MAX];
TGG_SETTING_TEXT_LOCAL(setting_peer, "peer", "Last address joined", "");
int setting_port;
TGG_SETTING_INT_LOCAL(setting_port, "port", "Port to host on", 51000, 1024, 65535);
int setting_stocks;
TGG_SETTING_INT_LOCAL(setting_stocks, "stocks", "Stocks (host)", 4, 1, 99);
int setting_minutes;
TGG_SETTING_INT_LOCAL(setting_minutes, "minutes", "Time limit, minutes (host)", 8, 0, 99);
#define NAME(kind, name) name,
int setting_stage;
TGG_SETTING_CHOICE_LOCAL(setting_stage, "stage", "Stage (host)", 0, STAGES(NAME));
int setting_character;
TGG_SETTING_CHOICE_LOCAL(setting_character, "character", "Character", 2, FIGHTERS(NAME));
#undef NAME
int setting_costume;
TGG_SETTING_INT_LOCAL(setting_costume, "costume", "Color", 0, 0, 5);

static const tgg_screen screen;

static void say(const char* text)
{
    snprintf(dc.message, sizeof dc.message, "%s", text);
    dc.message_frames = 240;
}

void dc_full_address(const char* typed, char* out, int cap)
{
    if (strchr(typed, ':') != NULL) {
        snprintf(out, (size_t) cap, "%s", typed);
    } else {
        snprintf(out, (size_t) cap, "%s:%d", typed, setting_port);
    }
}

int menu_rows(const char** rows, int max)
{
    static char join_last[ADDRESS_MAX + 8];
    int n = 0;
    rows[n++] = "Host";
    if (setting_peer[0] != '\0' && n < max) {
        snprintf(join_last, sizeof join_last, "Join %s", setting_peer);
        rows[n++] = join_last;
        rows[n++] = "Join another address";
    } else {
        rows[n++] = "Join";
    }
    return n;
}

/* ---- The session */

static void fail_view(const char* why)
{
    snprintf(dc.why, sizeof dc.why, "%s", why);
    dc.failed_frames = 0;
    dc.view = VIEW_FAILED;
}

/* In a game restarted with the host's mods: the player's own game again */
static void go_back(void)
{
    tgg_log("leaving netplay: back to this player's own game");
    if (tgg_restart_back() != 0) {
        dc.for_netplay = 0;
        say("Can't go back to your own game.");
    }
}

static void send_pick(void)
{
    char msg[64];
    snprintf(msg, sizeof msg, "pick %d %d %d", dc.lobby.me.ckind, dc.lobby.me.costume,
             dc.lobby.me.ready);
    netplay_send(msg, (int) strlen(msg));
}

static void send_stage(void)
{
    char msg[64];
    snprintf(msg, sizeof msg, "stage %d %d %d", dc.lobby.stage, dc.lobby.stocks,
             dc.lobby.minutes);
    netplay_send(msg, (int) strlen(msg));
}

static void enter_lobby(void)
{
    Lobby* l = &dc.lobby;
    dc.view = VIEW_LOBBY;
    if (!l->me.known) {
        l->me.known = 1;
        l->me.ckind = setting_character;
        l->me.costume = setting_costume;
    }
    l->me.ready = 0;
    l->them.ready = 0;
    l->started = 0;
    l->cursor = grid_cell(l->me.ckind);
    netplay_info info;
    netplay_status(&info);
    if (info.side == 0) {
        l->stage = setting_stage;
        l->stocks = setting_stocks;
        l->minutes = setting_minutes;
        send_stage();
    }
    send_pick();
}

static void host(void)
{
    const char* why = NULL;
    if (netplay_open(0, NULL, setting_port, &why) != 0) {
        tgg_menu_sound(TGG_SOUND_ERROR);
        say(why != NULL ? why : "Can't host.");
        return;
    }
    tgg_menu_sound(TGG_SOUND_ACCEPT);
    memset(&dc.lobby, 0, sizeof dc.lobby);
    dc.view = VIEW_HOSTING;
}

static void join(const char* typed)
{
    char full[ADDRESS_MAX + 8];
    dc_full_address(typed, full, sizeof full);
    const char* why = NULL;
    if (netplay_open(1, full, 0, &why) != 0) {
        tgg_menu_sound(TGG_SOUND_ERROR);
        say(why != NULL ? why : "Can't connect.");
        return;
    }
    tgg_menu_sound(TGG_SOUND_ACCEPT);
    snprintf(setting_peer, sizeof setting_peer, "%s", typed);
    tgg_setting_store(setting_peer);
    snprintf(dc.joined, sizeof dc.joined, "%s", full);
    memset(&dc.lobby, 0, sizeof dc.lobby);
    dc.view = VIEW_CONNECTING;
}

static void leave_session(void)
{
    netplay_close();
    if (dc.for_netplay) {
        go_back();
    }
    dc.view = VIEW_MENU;
}

/* ---- The host has other mods */

/* What to say when this game can't get them itself */
static void say_compare(void)
{
    char why[160];
    if (dc.have_diff && strcmp(dc.diff.game_there, dc.diff.game_here) != 0) {
        snprintf(why, sizeof why, "The host runs tgg-melee %s and this game %s.",
                 dc.diff.game_there, dc.diff.game_here);
    } else {
        snprintf(why, sizeof why,
                 "The host has other mods. Compare the two mods folders; the log lists both.");
    }
    fail_view(why);
}

static void different_frame(uint32_t in)
{
    if (!dc.have_diff && !dc.bad_profile) {
        const char* host_profile = netplay_host_profile();
        if (host_profile != NULL) {
            static char own[16 * 1024];
            snprintf(own, sizeof own, "%s", tgg_mod_set_profile());
            if (profile_diff(host_profile, own, &dc.diff) == 0) {
                dc.have_diff = 1;
            } else {
                dc.bad_profile = 1;
                tgg_log("can't read the host's profile");
            }
        }
    }
    if (in & TGG_MENU_BACK) {
        tgg_menu_sound(TGG_SOUND_BACK);
        leave_session();
        return;
    }
    if (!(in & TGG_MENU_ACCEPT) || !dc.have_diff || dc.for_netplay) {
        return;
    }
    /* Get them: tgg reads the host's profile from a file in the data folder,
     * and the game restarts with those mods and joins the host again */
    char path[1024];
    snprintf(path, sizeof path, "%s/netplay-host-profile.json", tgg_data_dir());
    FILE* f = fopen(path, "wb");
    int saved = f != NULL && fputs(netplay_host_profile(), f) >= 0;
    if (f != NULL && fclose(f) != 0) {
        saved = 0;
    }
    netplay_close();
    if (!saved) {
        tgg_menu_sound(TGG_SOUND_ERROR);
        tgg_log("can't write %s", path);
        fail_view("Can't save the host's list of mods.");
        return;
    }
    if (tgg_restart_with("tgg.direct-connect", path, dc.joined) != 0) {
        tgg_menu_sound(TGG_SOUND_ERROR);
        tgg_log("tgg can't get the host's mods: it isn't beside the game");
        say_compare();
        return;
    }
    tgg_menu_sound(TGG_SOUND_ACCEPT);
    tgg_log("getting the host's mods (%s), then joining %s again", path, dc.joined);
    memset(&dc.restart, 0, sizeof dc.restart);
    dc.view = VIEW_RESTARTING;
}

/* While tgg gets the host's mods: B stops it; asked about publishers, A says
 * yes (and remembers it) and B no */
static void restarting_frame(uint32_t in)
{
    tgg_restart_poll(&dc.restart);
    switch (dc.restart.state) {
    case TGG_RESTART_WORKING:
        if (in & TGG_MENU_BACK) {
            tgg_restart_cancel();
            tgg_menu_sound(TGG_SOUND_BACK);
            dc.view = VIEW_MENU;
        }
        break;
    case TGG_RESTART_NEEDS_TRUST:
        if (in & (TGG_MENU_ACCEPT | TGG_MENU_START)) {
            tgg_menu_sound(TGG_SOUND_ACCEPT);
            tgg_log("getting mods by %s: yes, and from now on", dc.restart.trust);
            tgg_restart_trust(1, 1);
        } else if (in & TGG_MENU_BACK) {
            tgg_menu_sound(TGG_SOUND_BACK);
            tgg_log("getting mods by %s: no", dc.restart.trust);
            tgg_restart_trust(0, 0);
            dc.view = VIEW_MENU;
        }
        break;
    case TGG_RESTART_NOT_NEEDED:
        /* tgg says this game already has the host's mods, yet the two
         * differ: nothing a restart fixes */
        tgg_restart_cancel();
        tgg_menu_sound(TGG_SOUND_ERROR);
        tgg_log("tgg says this game already has the host's mods, but the mod sets differ");
        fail_view("This game has the host's mods, but they still differ. The log lists both.");
        break;
    case TGG_RESTART_FAILED: {
        char why[160];
        snprintf(why, sizeof why, "Can't get the host's mods: %.120s.", dc.restart.message);
        tgg_log("%s", why);
        tgg_restart_cancel();
        tgg_menu_sound(TGG_SOUND_ERROR);
        fail_view(why);
        break;
    }
    default:
        dc.view = VIEW_MENU;
        break;
    }
}

/* Messages from the other screen */
static void read_messages(void)
{
    char buf[128];
    int n;
    while ((n = netplay_receive(buf, (int) sizeof buf - 1)) > 0) {
        buf[n] = '\0';
        int a, b, c;
        if (sscanf(buf, "pick %d %d %d", &a, &b, &c) == 3) {
            dc.lobby.them.known = 1;
            dc.lobby.them.ckind = a;
            dc.lobby.them.costume = b;
            dc.lobby.them.ready = c;
        } else if (sscanf(buf, "stage %d %d %d", &a, &b, &c) == 3) {
            dc.lobby.stage = a;
            dc.lobby.stocks = b;
            dc.lobby.minutes = c;
        }
    }
}

/* The host's match: both picks, its stage and rules */
static void build_match(StartMeleeData* m)
{
    const Lobby* l = &dc.lobby;
    memset(m, 0, sizeof *m);
    gm_SetupRulesDefaults(&m->rules);
    gm_SetupAllPlayerDefaults(m->players);
    m->rules.match_kind = MatchKind_Stock;
    m->rules.timer_enabled = l->minutes != 0;
    m->rules.time_limit = (u32) l->minutes * 60;
    m->rules.item_freq = -1;
    m->rules.sd_penalty = -1;
    /* As on Slippi: nobody pauses a match over the network */
    m->rules.disable_pausing = 1;
    m->rules.stkind = (u16) stage_kind(l->stage);
    const Pick* side[2] = { &l->me, &l->them };
    for (int i = 0; i < 2; i++) {
        PlayerInitData* p = &m->players[i];
        p->ckind = (s8) side[i]->ckind;
        p->slot_type = Gm_PKind_Human;
        p->color = (u8) side[i]->costume;
        p->stocks = (s8) l->stocks;
    }
}

static void maybe_play(void)
{
    Lobby* l = &dc.lobby;
    netplay_info info;
    netplay_status(&info);
    if (l->started || !l->me.ready || info.state != NETPLAY_CONNECTED) {
        return;
    }
    if (info.side == 1) {
        /* The host decides the match */
        if (netplay_play(NULL) == 0) {
            l->started = 1;
        }
        return;
    }
    if (!l->them.ready) {
        return;
    }
    static StartMeleeData match;
    build_match(&match);
    if (netplay_play(&match) == 0) {
        l->started = 1;
    }
}

/* ---- Each view */

static void menu_frame(uint32_t in)
{
    const char* rows[4];
    const int n = menu_rows(rows, 4);
    if (in & TGG_MENU_BACK) {
        tgg_menu_sound(TGG_SOUND_BACK);
        if (dc.for_netplay) {
            go_back();
        }
        tgg_screen_close();
        return;
    }
    if (in & TGG_MENU_UP) {
        dc.menu_sel = (dc.menu_sel + n - 1) % n;
        tgg_menu_sound(TGG_SOUND_MOVE);
    }
    if (in & TGG_MENU_DOWN) {
        dc.menu_sel = (dc.menu_sel + 1) % n;
        tgg_menu_sound(TGG_SOUND_MOVE);
    }
    if (dc.menu_sel >= n) {
        dc.menu_sel = 0;
    }
    if (in & (TGG_MENU_ACCEPT | TGG_MENU_START)) {
        if (dc.menu_sel == 0) {
            host();
        } else if (dc.menu_sel == 1 && setting_peer[0] != '\0') {
            join(setting_peer);
        } else {
            tgg_menu_sound(TGG_SOUND_ACCEPT);
            snprintf(dc.address, sizeof dc.address, "%s", setting_peer);
            dc.key = 0;
            dc.view = VIEW_ADDRESS;
        }
    }
}

static void lobby_frame(uint32_t in)
{
    Lobby* l = &dc.lobby;
    netplay_info info;
    netplay_status(&info);
    if (in & TGG_MENU_BACK) {
        tgg_menu_sound(TGG_SOUND_BACK);
        leave_session();
        return;
    }
    if (l->me.ready) {
        return;
    }
    int moved = 0;
    const int rows = (GRID_CELLS + GRID_COLS - 1) / GRID_COLS;
    int row = l->cursor / GRID_COLS, col = l->cursor % GRID_COLS;
    if (in & TGG_MENU_LEFT) {
        col = (col + GRID_COLS - 1) % GRID_COLS;
        moved = 1;
    }
    if (in & TGG_MENU_RIGHT) {
        col = (col + 1) % GRID_COLS;
        moved = 1;
    }
    if (in & TGG_MENU_UP) {
        row = (row + rows - 1) % rows;
        moved = 1;
    }
    if (in & TGG_MENU_DOWN) {
        row = (row + 1) % rows;
        moved = 1;
    }
    if (moved) {
        int cell = row * GRID_COLS + col;
        if (cell >= GRID_CELLS) {
            cell = GRID_CELLS - 1;
        }
        l->cursor = cell;
        tgg_menu_sound(TGG_SOUND_MOVE);
    }
    int changed = 0;
    if (in & TGG_MENU_ACCEPT) {
        if (l->me.ckind != grid_ckind(l->cursor)) {
            l->me.ckind = grid_ckind(l->cursor);
            l->me.costume = 0;
        }
        tgg_menu_sound(TGG_SOUND_ACCEPT);
        changed = 1;
    }
    if (in & (TGG_MENU_X | TGG_MENU_Y)) {
        l->me.costume = next_costume(l->me.ckind, l->me.costume, (in & TGG_MENU_Y) ? 1 : -1);
        tgg_menu_sound(TGG_SOUND_MOVE);
        changed = 1;
    }
    if (info.side == 0 && (in & (TGG_MENU_L | TGG_MENU_R))) {
        const int n = stage_count();
        l->stage = (l->stage + ((in & TGG_MENU_R) ? 1 : n - 1)) % n;
        setting_stage = l->stage;
        tgg_setting_store(&setting_stage);
        tgg_menu_sound(TGG_SOUND_MOVE);
        send_stage();
    }
    if (in & TGG_MENU_START) {
        l->me.ready = 1;
        tgg_menu_sound(TGG_SOUND_ACCEPT);
        changed = 1;
    }
    if (changed) {
        setting_character = l->me.ckind;
        setting_costume = l->me.costume;
        tgg_setting_store(&setting_character);
        tgg_setting_store(&setting_costume);
        send_pick();
    }
}

static void frame(void)
{
    if (dc.message_frames > 0) {
        dc.message_frames--;
    }
    const uint32_t in = tgg_menu_input();
    netplay_info info;
    netplay_status(&info);
    if (dc.view != VIEW_MENU && dc.view != VIEW_ADDRESS && dc.view != VIEW_FAILED &&
        info.state == NETPLAY_FAILED)
    {
        tgg_menu_sound(TGG_SOUND_ERROR);
        fail_view(info.why);
        netplay_close();
    }
    switch (dc.view) {
    case VIEW_MENU:
        menu_frame(in);
        break;
    case VIEW_ADDRESS: {
        const int r = keyboard_frame();
        if (r > 0) {
            if (dc.address[0] == '\0') {
                tgg_menu_sound(TGG_SOUND_ERROR);
            } else {
                join(dc.address);
            }
        } else if (r < 0) {
            dc.view = VIEW_MENU;
        }
        break;
    }
    case VIEW_HOSTING:
    case VIEW_CONNECTING:
        if (in & TGG_MENU_BACK) {
            tgg_menu_sound(TGG_SOUND_BACK);
            leave_session();
        } else if (info.state == NETPLAY_CONNECTED) {
            tgg_menu_sound(TGG_SOUND_ACCEPT);
            tgg_log("in the lobby with %s, mod set %s", info.peer, tgg_mod_set_id());
            enter_lobby();
        } else if (info.state == NETPLAY_MISMATCH) {
            tgg_menu_sound(TGG_SOUND_ERROR);
            dc.have_diff = 0;
            dc.bad_profile = 0;
            dc.view = VIEW_DIFFERENT;
        }
        break;
    case VIEW_DIFFERENT:
        different_frame(in);
        break;
    case VIEW_RESTARTING:
        restarting_frame(in);
        break;
    case VIEW_LOBBY:
        read_messages();
        lobby_frame(in);
        if (dc.view == VIEW_LOBBY) {
            maybe_play();
        }
        break;
    case VIEW_FAILED:
        dc.failed_frames++;
        if (in & (TGG_MENU_ACCEPT | TGG_MENU_BACK | TGG_MENU_START)) {
            tgg_menu_sound(TGG_SOUND_BACK);
            if (dc.for_netplay) {
                go_back();
            }
            dc.view = VIEW_MENU;
        } else if (dc.for_netplay && dc.failed_frames > 5 * 60) {
            /* The session ended in a game restarted for it: back on its own */
            go_back();
            dc.view = VIEW_MENU;
        }
        break;
    }
}

static void enter(int from_match)
{
    if (!from_match) {
        dc.view = VIEW_MENU;
        return;
    }
    /* Back from a match: the lobby again, for the next one */
    netplay_info info;
    netplay_status(&info);
    if (info.state == NETPLAY_FAILED) {
        fail_view(info.why);
        netplay_close();
        return;
    }
    enter_lobby();
}

static void leave(void)
{
    /* Leaving for a match keeps the session; leaving the screen ends it */
    netplay_info info;
    netplay_status(&info);
    if (info.state != NETPLAY_PLAYING) {
        netplay_close();
        dc.view = VIEW_MENU;
    }
}

static const tgg_screen screen = {
    .title = "Direct connect",
    .enter = enter,
    .frame = frame,
    .draw = dc_draw,
    .leave = leave,
};
TGG_SCREEN("connect", screen);

/* What the game starts on: TGG_DIRECT_CONNECT=host or join:<address> starts
 * it on this screen, hosting or joining, for scripted runs and a quick
 * start. A game restarted with the host's mods (tgg_restart_with above;
 * TGG_RETURN_BY names this mod, TGG_RETURN_THEN the host) joins the host at
 * once. Back in the player's own game after that, the screen opens again. */
static const char* autostart;
static int reopen;

static void init(void)
{
    static char rejoin[ADDRESS_MAX + 16];
    const char* by = getenv("TGG_RETURN_BY");
    const char* then = getenv("TGG_RETURN_THEN");
    if (by != NULL && strcmp(by, "tgg.direct-connect") == 0 && then != NULL && then[0] != '\0') {
        dc.for_netplay = 1;
        snprintf(rejoin, sizeof rejoin, "join:%s", then);
        autostart = rejoin;
        tgg_log("restarted with the host's mods: joining %s", then);
        tgg_boot_mode(GM_MENU);
        return;
    }
    if (tgg_restarted("tgg.direct-connect") != NULL) {
        reopen = 1;
        tgg_log("back in this player's own game after netplay");
        tgg_boot_mode(GM_MENU);
        return;
    }
    autostart = getenv("TGG_DIRECT_CONNECT");
    if (autostart != NULL && autostart[0] != '\0') {
        tgg_boot_mode(GM_MENU);
    } else {
        autostart = NULL;
    }
}
TGG_INIT(init);

static void on_frame(void)
{
    if (autostart == NULL && !reopen) {
        return;
    }
    if (tgg_screen_shown() != &screen) {
        tgg_screen_open(&screen); /* opens once the game is at a menu */
        return;
    }
    reopen = 0;
    const char* what = autostart;
    autostart = NULL;
    if (what == NULL) {
        return;
    }
    if (strcmp(what, "host") == 0) {
        host();
    } else if (strncmp(what, "join:", 5) == 0) {
        join(what + 5);
    }
}
TGG_ON(frame, on_frame);
