/* Direct connect: Online on the main menu, the step that connects the games
 * (tgg.netplay). One player hosts and tells the others their address; up to
 * three join it. When everyone's in, the host presses Start: the session is
 * locked, and the host picks what to play from the game's own main menu,
 * which the port cuts down to what can be played online. The others wait
 * here until the host picks, and the port takes them there: the real
 * character select for VS Mode. The game's own menus do the rest. */
#include "dc.h"

#include "tgg/control.h"
#include "tgg/events.h"
#include "tgg/files.h"
#include "tgg/input.h"
#include "tgg/menu.h"
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
    rows[n++] = "Make a room";
    rows[n++] = "Join a room";
    rows[n++] = "Host by address";
    if (setting_peer[0] != '\0' && n < max) {
        snprintf(join_last, sizeof join_last, "Join %s", setting_peer);
        rows[n++] = join_last;
        rows[n++] = "Join another address";
    } else {
        rows[n++] = "Join by address";
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

static void host(void)
{
    const char* why = NULL;
    if (netplay_open(0, NULL, setting_port, &why) != 0) {
        tgg_menu_sound(TGG_SOUND_ERROR);
        say(why != NULL ? why : "Can't host.");
        return;
    }
    tgg_menu_sound(TGG_SOUND_ACCEPT);
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
    dc.view = VIEW_CONNECTING;
}

static void leave_session(void)
{
    if (dc.room) {
        meet_leave();
        dc.room = 0;
    }
    netplay_close();
    if (dc.for_netplay) {
        go_back();
    }
    dc.view = VIEW_MENU;
}

/* ---- Rooms on textures.gg (meet.c): a friend on another network */

static void make_room(void)
{
    memset(&dc.meet, 0, sizeof dc.meet);
    if (meet_make() != 0) {
        tgg_menu_sound(TGG_SOUND_ERROR);
        meet_poll(&dc.meet);
        say(dc.meet.why[0] != '\0' ? dc.meet.why : "Can't make a room here.");
        return;
    }
    tgg_menu_sound(TGG_SOUND_ACCEPT);
    dc.room = 1;
    dc.view = VIEW_ROOM;
}

static void join_room(const char* typed)
{
    char code[sizeof dc.meet.code];
    size_t n = 0;
    for (const char* p = typed; *p != '\0' && n + 1 < sizeof code; p++) {
        if (*p != ' ' && *p != '-') {
            code[n++] = (char) (*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
        }
    }
    code[n] = '\0';
    memset(&dc.meet, 0, sizeof dc.meet);
    if (n == 0 || meet_join(code) != 0) {
        tgg_menu_sound(TGG_SOUND_ERROR);
        meet_poll(&dc.meet);
        say(dc.meet.why[0] != '\0' ? dc.meet.why : "Can't join a room here.");
        return;
    }
    tgg_menu_sound(TGG_SOUND_ACCEPT);
    dc.room = 1;
    dc.view = VIEW_ROOM;
}

/* Each frame of a room: signing in, waiting in it, connecting, then the
 * session as after a direct join */
static void room_frame(uint32_t in)
{
    meet_poll(&dc.meet);
    if (in & TGG_MENU_BACK) {
        tgg_menu_sound(TGG_SOUND_BACK);
        leave_session();
        return;
    }
    if (dc.meet.state == MEET_FAILED) {
        tgg_menu_sound(TGG_SOUND_ERROR);
        tgg_log("room: %s", dc.meet.why);
        meet_leave();
        dc.room = 0;
        netplay_close();
        fail_view(dc.meet.why);
    } else if (dc.meet.state == MEET_CONNECTED) {
        tgg_menu_sound(TGG_SOUND_ACCEPT);
        tgg_log("room %s: connected as side %d", dc.meet.code, dc.meet.side);
        dc.view = dc.meet.side == 0 ? VIEW_HOSTING : VIEW_WAITING;
    }
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
    /* TGG_DIRECT_CONNECT_YES=1 takes the offer at once, for scripted runs */
    const char* yes = getenv("TGG_DIRECT_CONNECT_YES");
    const int take = (in & TGG_MENU_ACCEPT) || (yes != NULL && strcmp(yes, "1") == 0);
    if (!take || !dc.have_diff || dc.for_netplay) {
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

/* ---- Each view */

static void menu_frame(uint32_t in)
{
    const char* rows[6];
    const int n = menu_rows(rows, 6);
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
            make_room();
        } else if (dc.menu_sel == 1) {
            tgg_menu_sound(TGG_SOUND_ACCEPT);
            dc.address[0] = '\0';
            dc.typing_code = 1;
            dc.key = 0;
            dc.view = VIEW_ADDRESS;
        } else if (dc.menu_sel == 2) {
            host();
        } else if (dc.menu_sel == 3 && setting_peer[0] != '\0') {
            join(setting_peer);
        } else {
            tgg_menu_sound(TGG_SOUND_ACCEPT);
            dc.typing_code = 0;
            snprintf(dc.address, sizeof dc.address, "%s", setting_peer);
            dc.key = 0;
            dc.view = VIEW_ADDRESS;
        }
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
            } else if (dc.typing_code) {
                join_room(dc.address);
            } else {
                join(dc.address);
            }
        } else if (r < 0) {
            dc.view = VIEW_MENU;
        }
        break;
    }
    case VIEW_HOSTING:
        if (dc.room) {
            meet_poll(&dc.meet); /* the room lets more games in */
        }
        if (in & TGG_MENU_BACK) {
            tgg_menu_sound(TGG_SOUND_BACK);
            leave_session();
        } else if ((in & TGG_MENU_START) && info.players >= 2 && netplay_lock() == 0) {
            /* Everyone's in: the host picks what to play on the main menu */
            tgg_menu_sound(TGG_SOUND_ACCEPT);
            tgg_log("playing with %d games, mod set %s", info.players, tgg_mod_set_id());
            tgg_screen_close();
        }
        break;
    case VIEW_CONNECTING:
    case VIEW_WAITING:
        if (in & TGG_MENU_BACK) {
            tgg_menu_sound(TGG_SOUND_BACK);
            leave_session();
        } else if (dc.view == VIEW_CONNECTING && info.state == NETPLAY_CONNECTED) {
            tgg_menu_sound(TGG_SOUND_ACCEPT);
            tgg_log("joined %s as side %d, mod set %s", info.peer, info.side, tgg_mod_set_id());
            dc.view = VIEW_WAITING;
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
    case VIEW_ROOM:
        room_frame(in);
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
    (void) from_match;
    netplay_info info;
    netplay_status(&info);
    if (info.state == NETPLAY_FAILED) {
        fail_view(info.why);
        netplay_close();
        return;
    }
    if (info.state == NETPLAY_OFF) {
        dc.view = VIEW_MENU;
        return;
    }
    /* Back during a session: hosting, or waiting for the host */
    dc.view = info.side == 0 ? VIEW_HOSTING : VIEW_WAITING;
    if (info.desync[0] != '\0') {
        say(info.desync);
        dc.message_frames = 10 * 60; /* long enough to read */
    }
}

static void leave(void)
{
    /* Once the host has locked the session it goes on through the game's
     * menus; before that, leaving the screen ends it */
    netplay_info info;
    netplay_status(&info);
    if (info.state != NETPLAY_PLAYING && !info.locked) {
        netplay_close();
        dc.view = VIEW_MENU;
    }
}

static const tgg_screen screen = {
    .enter = enter,
    .frame = frame,
    .draw = dc_draw,
    .leave = leave,
};
TGG_SCREEN("connect", screen);

/* Online, on the main menu after VS Mode */
static const tgg_menu_entry online = {
    .label = "Online",
    .description = "Play people over the internet.",
    .after = "main/vs",
    .screen = &screen,
    .online = 1,
};
TGG_MENU_ADD("main/online", online);

/* What the game starts on: TGG_DIRECT_CONNECT=host, join:<address>, room
 * (make a room) or room:<code> (join one) starts
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
    if (tgg_screen_shown() == NULL && !tgg_in_match()) {
        /* Back at the main menu during a session: a game that doesn't host
         * waits for the host here, and a session that ended says why. This
         * opens once the game is at the main menu. */
        netplay_info info;
        netplay_status(&info);
        if ((info.locked && info.side != 0 && info.state == NETPLAY_CONNECTED) ||
            info.state == NETPLAY_FAILED)
        {
            tgg_screen_open(&screen);
            return;
        }
    }
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
    } else if (strcmp(what, "room") == 0) {
        make_room();
    } else if (strncmp(what, "room:", 5) == 0) {
        join_room(what + 5);
    }
}
TGG_ON(frame, on_frame);
