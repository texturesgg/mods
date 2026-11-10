/* Direct connect: Online on the main menu, a submenu of the game's own look
 * that connects the games (tgg.netplay):
 *
 *   Make a room       a code friends anywhere join with (textures.gg, meet.c)
 *   Join a room       type a friend's code
 *   Host by address   others on the network or a VPN join with this address
 *   Join by address   type the host's address
 *
 * Hosting runs from the menu: the entry's line under the menu says the code
 * or the address and who's in, and A again, once someone's in, locks the
 * session and goes back to the main menu, which the port cuts down to what
 * can be played online. Joining opens this mod's screen to type and connect,
 * then comes back to the submenu, where the line says to wait for the host;
 * the port takes every game where the host picks: the real character select
 * for VS Mode. B leaves the submenu, and a session that hasn't started with
 * it. The screen also shows when the host has other mods (and gets them),
 * and why a session ended. */
#include "dc.h"

#include "tgg/control.h"
#include "tgg/draw.h"
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

/* A line for the submenu: what went wrong, or where things are */
static void note(const char* text)
{
    snprintf(dc.note, sizeof dc.note, "%s", text);
}

void dc_full_address(const char* typed, char* out, int cap)
{
    if (strchr(typed, ':') != NULL) {
        snprintf(out, (size_t) cap, "%s", typed);
    } else {
        snprintf(out, (size_t) cap, "%s:%d", typed, setting_port);
    }
}

/* ---- The session */

/* The screen's work is over: back where it was opened from */
static void done(void)
{
    if (tgg_screen_shown() == &screen) {
        tgg_screen_close();
    }
}

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
    dc.note[0] = '\0';
    if (netplay_open(0, NULL, setting_port, &why) != 0) {
        tgg_menu_sound(TGG_SOUND_ERROR);
        note(why != NULL ? why : "Can't host.");
        return;
    }
    tgg_menu_sound(TGG_SOUND_ACCEPT);
    dc.mode = MODE_HOST;
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
    dc.mode = MODE_JOIN;
    dc.view = VIEW_CONNECTING;
}

static void leave_session(void)
{
    if (dc.room) {
        meet_leave();
        dc.room = 0;
    }
    netplay_close();
    dc.mode = MODE_NONE;
    if (dc.for_netplay) {
        go_back();
    }
}

/* ---- Rooms on textures.gg (meet.c): friends on other networks */

static void make_room(void)
{
    memset(&dc.meet, 0, sizeof dc.meet);
    dc.note[0] = '\0';
    if (meet_make() != 0) {
        tgg_menu_sound(TGG_SOUND_ERROR);
        meet_poll(&dc.meet);
        note(dc.meet.why[0] != '\0' ? dc.meet.why : "Can't make a room here.");
        return;
    }
    tgg_menu_sound(TGG_SOUND_ACCEPT);
    dc.room = 1;
    dc.mode = MODE_ROOM;
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
        note(dc.meet.why[0] != '\0' ? dc.meet.why : "Can't join a room here.");
        return;
    }
    tgg_menu_sound(TGG_SOUND_ACCEPT);
    dc.room = 1;
    dc.mode = MODE_JOIN_ROOM;
}

/* Each frame of joining a room: signing in, waiting in it, connecting */
static void room_frame(uint32_t in)
{
    if (in & TGG_MENU_BACK) {
        tgg_menu_sound(TGG_SOUND_BACK);
        leave_session();
        done();
        return;
    }
    if (dc.meet.state == MEET_FAILED) {
        tgg_menu_sound(TGG_SOUND_ERROR);
        tgg_log("room: %s", dc.meet.why);
        meet_leave();
        dc.room = 0;
        netplay_close();
        dc.mode = MODE_NONE;
        fail_view(dc.meet.why);
    } else if (dc.meet.state == MEET_CONNECTED) {
        tgg_menu_sound(TGG_SOUND_ACCEPT);
        tgg_log("room %s: connected as side %d", dc.meet.code, dc.meet.side);
        done();
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
            done();
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
            done();
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
        done();
        break;
    }
}

/* ---- The screen: typing, connecting, the host's mods, why it ended */

static void frame(void)
{
    if (dc.message_frames > 0) {
        dc.message_frames--;
    }
    const uint32_t in = tgg_menu_input();
    netplay_info info;
    netplay_status(&info);
    if (dc.view != VIEW_ADDRESS && dc.view != VIEW_FAILED && info.state == NETPLAY_FAILED) {
        tgg_menu_sound(TGG_SOUND_ERROR);
        fail_view(info.why);
        leave_session();
    }
    if ((dc.view == VIEW_CONNECTING || dc.view == VIEW_ROOM) &&
        info.state == NETPLAY_MISMATCH)
    {
        tgg_menu_sound(TGG_SOUND_ERROR);
        dc.have_diff = 0;
        dc.bad_profile = 0;
        dc.view = VIEW_DIFFERENT;
    }
    switch (dc.view) {
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
            done();
        }
        break;
    }
    case VIEW_CONNECTING:
        if (in & TGG_MENU_BACK) {
            tgg_menu_sound(TGG_SOUND_BACK);
            leave_session();
            done();
        } else if (info.state == NETPLAY_CONNECTED) {
            tgg_menu_sound(TGG_SOUND_ACCEPT);
            tgg_log("joined %s as side %d, mod set %s", info.peer, info.side, tgg_mod_set_id());
            done();
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
            done();
        } else if (dc.for_netplay && dc.failed_frames > 5 * 60) {
            /* The session ended in a game restarted for it: back on its own */
            go_back();
            done();
        }
        break;
    }
}

/* The screen opens to type a code or an address, or to show why a session
 * ended */
static void enter(int from_match)
{
    (void) from_match;
    netplay_info info;
    netplay_status(&info);
    if (info.state == NETPLAY_FAILED) {
        fail_view(info.why);
        leave_session();
        return;
    }
    if (dc.view != VIEW_FAILED && dc.view != VIEW_CONNECTING && dc.view != VIEW_ROOM) {
        dc.view = VIEW_ADDRESS;
    }
}

static const tgg_screen screen = {
    .enter = enter,
    .frame = frame,
    .draw = dc_draw,
};
TGG_SCREEN("connect", screen);

/* ---- The Online submenu */

/* Joining: this mod's screen types the code or the address */
static void start_typing(int code)
{
    if (dc.mode != MODE_NONE) {
        tgg_menu_sound(TGG_SOUND_ERROR);
        return;
    }
    dc.typing_code = code;
    dc.key = 0;
    dc.view = VIEW_ADDRESS;
    snprintf(dc.address, sizeof dc.address, "%s", code ? "" : setting_peer);
    tgg_menu_sound(TGG_SOUND_ACCEPT);
    tgg_screen_open(&screen);
}

/* The host, once someone's in: the session is locked, and the main menu
 * offers what to play */
static void play(void)
{
    netplay_info info;
    netplay_status(&info);
    if (info.players < 2 || netplay_lock() != 0) {
        tgg_menu_sound(TGG_SOUND_ERROR);
        return;
    }
    tgg_menu_sound(TGG_SOUND_ACCEPT);
    tgg_log("playing with %d games, mod set %s", info.players, tgg_mod_set_id());
    tgg_menu_back();
}

static void make_room_entry(void)
{
    if (dc.mode == MODE_ROOM) {
        play();
    } else if (dc.mode == MODE_NONE) {
        make_room();
    } else {
        tgg_menu_sound(TGG_SOUND_ERROR);
    }
}

/* A room's code, typed on the game's own Name Entry keyboard: four letters,
 * no I, L or O */
static void code_typed(const char* text)
{
    if (text == NULL) {
        return;
    }
    char code[8];
    int n = 0;
    for (const char* p = text; *p != '\0'; p++) {
        const char c = (char) (*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
        if (n == 4 || c < 'A' || c > 'Z' || c == 'I' || c == 'L' || c == 'O') {
            n = -1;
            break;
        }
        code[n++] = c;
    }
    if (n != 4) {
        tgg_menu_sound(TGG_SOUND_ERROR);
        note("Room codes are 4 letters.");
        return;
    }
    code[4] = '\0';
    join_room(code);
}

static void join_room_entry(void)
{
    if (dc.mode != MODE_NONE || tgg_menu_keyboard(code_typed) != 0) {
        tgg_menu_sound(TGG_SOUND_ERROR);
        return;
    }
    dc.note[0] = '\0';
}

static void host_entry(void)
{
    if (dc.mode == MODE_HOST) {
        play();
    } else if (dc.mode == MODE_NONE) {
        host();
    } else {
        tgg_menu_sound(TGG_SOUND_ERROR);
    }
}

static void join_entry(void)
{
    start_typing(0);
}

/* Who's in, for the host: "waiting for a friend", "1 friend in, A to start" */
static const char* in_count(void)
{
    static char line[48];
    netplay_info info;
    netplay_status(&info);
    if (info.locked) {
        return "playing";
    }
    if (info.players < 2) {
        return "waiting for a friend";
    }
    snprintf(line, sizeof line, "%d friend%s in, A to start", info.players - 1,
             info.players == 2 ? "" : "s");
    return line;
}

/* A joiner's: connected, then waiting on the host */
static const char* joined_count(void)
{
    netplay_info info;
    netplay_status(&info);
    return info.state == NETPLAY_CONNECTED || info.state == NETPLAY_STARTING ||
                   info.state == NETPLAY_PLAYING
               ? "waiting for the host to pick"
               : "connecting";
}

/* In a room, every Online entry's line leads with its code (the one the log
 * prints), so it's there wherever the cursor is */
static const char* room_line(void)
{
    static char line[320];
    if (dc.mode != MODE_ROOM && dc.mode != MODE_JOIN_ROOM) {
        return NULL;
    }
    if (dc.meet.state == MEET_SIGN_IN) {
        snprintf(line, sizeof line, "Sign in at %s with the code %s.", dc.meet.signin_url,
                 dc.meet.signin_code);
        return line;
    }
    if (dc.meet.code[0] == '\0') {
        return dc.mode == MODE_ROOM ? "Getting a room on textures.gg." : "Joining the room.";
    }
    snprintf(line, sizeof line, "Room %s - %s", dc.meet.code,
             dc.mode == MODE_ROOM ? in_count() : joined_count());
    return line;
}

/* Hosting or joined by address, likewise */
static const char* address_line(void)
{
    static char line[160];
    if (dc.mode == MODE_JOIN) {
        snprintf(line, sizeof line, "Joined %s - %s", dc.joined, joined_count());
        return line;
    }
    if (dc.mode != MODE_HOST) {
        return NULL;
    }
    char addrs[1][48];
    if (local_addresses(addrs, 1) > 0) {
        snprintf(line, sizeof line, "Your address: %s - %s", addrs[0], in_count());
    } else {
        snprintf(line, sizeof line, "Port %d - %s", setting_port, in_count());
    }
    return line;
}

/* The session's line if there is one, else the entry's own */
static const char* session_line(const char* own)
{
    const char* line = room_line();
    if (line == NULL) {
        line = address_line();
    }
    if (line != NULL) {
        return line;
    }
    return dc.note[0] != '\0' ? dc.note : own;
}

static const char* make_room_line(void)
{
    return session_line("Get a code your friends can join with.");
}

static const char* host_line(void)
{
    return session_line("Others on your network join with your address.");
}

static const char* join_room_line(void)
{
    return session_line("Join a friend's room with its code.");
}

static const char* join_line(void)
{
    static char line[160];
    const char* busy = session_line(NULL);
    if (busy != NULL) {
        return busy;
    }
    if (setting_peer[0] != '\0') {
        snprintf(line, sizeof line, "Join a game on your network. Last: %s.", setting_peer);
        return line;
    }
    return "Join a game on your network by its address.";
}

/* B leaves the submenu: a session that hasn't started ends with it */
static void online_leave(void)
{
    netplay_info info;
    netplay_status(&info);
    if (!info.locked && dc.mode != MODE_NONE) {
        tgg_log("leaving the session");
        leave_session();
    }
    dc.note[0] = '\0';
}

static const tgg_menu_entry online = {
    .label = "Online",
    .description = "Play people over the internet.",
    .after = "main/vs",
    .submenu = "online",
    .leave = online_leave,
    .online = 1,
};
TGG_MENU_ADD("main/online", online);

static const tgg_menu_entry make_room_item = {
    .label = "Make a room", .action = make_room_entry, .describe = make_room_line,
};
TGG_MENU_ADD("online/make-room", make_room_item);
static const tgg_menu_entry join_room_item = {
    .label = "Join a room", .action = join_room_entry, .describe = join_room_line,
};
TGG_MENU_ADD("online/join-room", join_room_item);
static const tgg_menu_entry host_item = {
    .label = "Host by address", .action = host_entry, .describe = host_line,
};
TGG_MENU_ADD("online/host", host_item);
static const tgg_menu_entry join_item = {
    .label = "Join by address", .action = join_entry, .describe = join_line,
};
TGG_MENU_ADD("online/join", join_item);

/* What the game starts on: TGG_DIRECT_CONNECT=host, join:<address>, room
 * (make a room) or room:<code> (join one), for scripted runs and a quick
 * start; TGG_DIRECT_CONNECT_GAMES=<n> has a host start once n games are in.
 * A game restarted with the host's mods (tgg_restart_with above;
 * TGG_RETURN_BY names this mod, TGG_RETURN_THEN the host) joins the host at
 * once. */
static const char* autostart;
static int autostart_games;

static void init(void)
{
    static char rejoin[ADDRESS_MAX + 16];
    const char* games = getenv("TGG_DIRECT_CONNECT_GAMES");
    autostart_games = games != NULL ? atoi(games) : 0;
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
    if (dc.room) {
        meet_poll(&dc.meet); /* a room lets more games in until the host starts */
        if ((dc.mode == MODE_ROOM || (dc.mode == MODE_JOIN_ROOM && tgg_screen_shown() != &screen)) &&
            dc.meet.state == MEET_FAILED)
        {
            tgg_log("room: %s", dc.meet.why);
            note(dc.meet.why);
            meet_leave();
            dc.room = 0;
            netplay_close();
            dc.mode = MODE_NONE;
        }
    }
    netplay_info info;
    netplay_status(&info);
    if (info.state == NETPLAY_MISMATCH && tgg_screen_shown() != &screen && !tgg_in_match()) {
        /* The host has other mods: the screen shows what differs, and gets them */
        dc.have_diff = 0;
        dc.bad_profile = 0;
        dc.view = VIEW_DIFFERENT;
        tgg_screen_open(&screen);
        return;
    }
    if (autostart_games >= 2 && (dc.mode == MODE_HOST || dc.mode == MODE_ROOM) && !info.locked &&
        info.players >= autostart_games && netplay_lock() == 0)
    {
        tgg_log("playing with %d games, mod set %s", info.players, tgg_mod_set_id());
    }
    if (tgg_screen_shown() == NULL && !tgg_in_match() && info.state == NETPLAY_FAILED) {
        /* A session that ended says why, once the game is at the main menu */
        tgg_screen_open(&screen);
        return;
    }
    if (autostart == NULL) {
        return;
    }
    const char* what = autostart;
    if (strcmp(what, "host") == 0 || strcmp(what, "room") == 0) {
        autostart = NULL;
        if (what[0] == 'h') {
            host();
        } else {
            make_room();
        }
        return;
    }
    if (tgg_screen_shown() != &screen) {
        dc.view = VIEW_CONNECTING;
        tgg_screen_open(&screen); /* opens once the game is at a menu */
        return;
    }
    autostart = NULL;
    if (strncmp(what, "join:", 5) == 0) {
        join(what + 5);
    } else if (strncmp(what, "room:", 5) == 0) {
        join_room(what + 5);
        dc.view = VIEW_ROOM;
    }
}
TGG_ON(frame, on_frame);

/* ---- The character select: each player's name and ping, and the room ------ */

/* A player's panel: their name where the room says it (the host first, then
 * in the order they joined, as the sides go), and their ping where this game
 * has a link to them. Online, side s plays port s. */
static void on_panel(int port, float x, float y, float w, float h)
{
    netplay_info info;
    netplay_status(&info);
    if (info.players < 2 || port >= info.players ||
        (info.state != NETPLAY_CONNECTED && info.state != NETPLAY_STARTING))
    {
        return;
    }
    const float size = h * 0.8f;
    const float top = y + (h - size) / 2;
    const tgg_color white = TGG_RGBA(255, 255, 255, 230);
    char ping[16] = "";
    const float rtt = netplay_rtt(port);
    if (port != info.side && rtt > 0) {
        snprintf(ping, sizeof ping, "%d ms", (int) (rtt + 0.5f));
        tgg_draw_text(x + w - 2, top, size, TGG_RGBA(200, 200, 200, 230), TGG_ALIGN_RIGHT, ping);
    }
    if (dc.room && port < dc.meet.n_members && dc.meet.members[port][0] != '\0') {
        char name[64];
        const float room = w - 4 - (ping[0] != '\0' ? tgg_text_width(size, ping) + 6 : 0);
        tgg_text_fit(name, sizeof name, size, room, dc.meet.members[port]);
        tgg_draw_text(x + 2, top, size, white, TGG_ALIGN_LEFT, name);
    }
    if (port == 0 && dc.room && dc.meet.code[0] != '\0') {
        /* The room, below the panels, so more friends can join */
        char line[32];
        snprintf(line, sizeof line, "Room %s", dc.meet.code);
        tgg_draw_text(320, 456, 18, white, TGG_ALIGN_CENTER, line);
    }
}
TGG_ON(css_panel_draw, on_panel);
