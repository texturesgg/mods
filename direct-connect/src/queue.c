/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Find a match: Online's entry that pairs this player with someone searching
 * in the same mode with the same gameplay mods (the meeting point's queues,
 * meet.c), on this mod's screen:
 *
 *   connecting   signing in to textures.gg and reaching the meeting point
 *   modes        VS (1 on 1) and each mode mods offer online, with how many
 *                are searching in each
 *   notice       the first time: people you don't know see your IP address
 *   character    the game's own character select (VS only)
 *   searching    until someone's found; B stops
 *   lobby        the two games trade picks, the host makes the match on a
 *                random legal stage (draw_stage), and it plays
 *   after        A plays again, B finds someone else
 *
 * A mode a mod offers online (a shared main menu entry, tgg/menu.h) needs no
 * code for this: once paired, the host's game picks its entry, which opens it
 * on both games, as if the host had picked it from the main menu.
 *
 * In the lobby the games talk in short messages (netplay_send):
 *   qpick <character> <costume>    each game's pick
 *   qagain                         this player wants another match
 */
#include "dc.h"
#include "ui.h"

#include "tgg/draw.h"
#include "tgg/events.h"
#include "tgg/files.h"
#include "tgg/input.h"
#include "tgg/internal/companion.h"
#include "tgg/match.h"
#include "tgg/menu.h"
#include "tgg/net.h"
#include "tgg/screen.h"
#include "tgg/session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The rules of a queue's VS match */
#define STOCKS 4
#define MINUTES 8

/* How long the after-match screen waits for both players to want another
 * match before searching again */
#define AGAIN_MS 30000.0

static const struct {
    const char* id;
    const char* name;
} STAGES[] = {
    {"melee/battlefield", "Battlefield"},
    {"melee/final-destination", "Final Destination"},
    {"melee/dream-land", "Dream Land"},
    {"melee/yoshis-story", "Yoshi's Story"},
    {"melee/fountain-of-dreams", "Fountain of Dreams"},
    {"melee/pokemon-stadium", "Pokemon Stadium"},
};
#define N_STAGES ((int) (sizeof STAGES / sizeof STAGES[0]))

enum {
    Q_CONNECT,
    Q_MODES,
    Q_NOTICE,
    Q_CHARACTER,
    Q_SEARCH,
    Q_LOBBY,
    Q_PLAYING,
    Q_AFTER,
    Q_FAILED,
};

/* The modes: VS, then shared main menu entries mods add */
#define MODES 4
typedef struct Mode {
    char id[64];    /* the queue's mode: "vs", or the entry's id */
    char label[48]; /* what the list says */
} Mode;

static const tgg_screen screen;

static struct {
    int view;
    unsigned anim_frames;
    Mode modes[MODES];
    int n_modes;
    int cursor;
    int mode; /* the picked one */
    char why[320];
    /* This player's pick (VS) */
    char character[64];
    int costume;
    int choosing;
    int chose; /* 1 picked, -1 backed out */
    /* The lobby */
    int sent_pick;
    char their_character[64];
    int their_costume;
    int have_theirs;
    int started;    /* netplay_play went through */
    int opened;     /* a mod's mode was opened */
    int games;      /* matches played with this opponent */
    int stage;      /* the host: this game's stage, -1 not drawn yet */
    int again_me, again_them;
    double after_ms;
    char opponent[64];
    /* The host's stages not drawn yet with this opponent (draw_stage) */
    int bag[N_STAGES];
    int bag_n;
} q;

static int notice_seen = -1;
/* TGG_DIRECT_CONNECT=search:<mode>: the mode to search in once connected */
static char auto_mode[64];
static char text_of_mode[128];

static const char* auto_yes(void)
{
    const char* yes = getenv("TGG_DIRECT_CONNECT_YES");
    return yes != NULL && strcmp(yes, "1") == 0 ? yes : NULL;
}

/* ---- The stranger notice, once per machine */

static void notice_path(char* out, size_t cap)
{
    snprintf(out, cap, "%s/direct-connect-strangers", tgg_data_dir());
}

static int seen_notice(void)
{
    if (notice_seen < 0) {
        char path[1024];
        notice_path(path, sizeof path);
        FILE* f = fopen(path, "rb");
        notice_seen = f != NULL;
        if (f != NULL) {
            fclose(f);
        }
    }
    return notice_seen;
}

static void remember_notice(void)
{
    char path[1024];
    notice_path(path, sizeof path);
    FILE* f = fopen(path, "wb");
    if (f != NULL) {
        fputs("seen\n", f);
        fclose(f);
    }
    notice_seen = 1;
}

/* ---- Modes */

static void find_modes(void)
{
    q.n_modes = 0;
    snprintf(q.modes[0].id, sizeof q.modes[0].id, "vs");
    snprintf(q.modes[0].label, sizeof q.modes[0].label, "VS, 1 on 1");
    q.n_modes = 1;
    for (int i = 0; q.n_modes < MODES; i++) {
        const char* id = tgg_menu_shared(i);
        if (id == NULL) {
            break;
        }
        const char* label = tgg_menu_label(id);
        Mode* m = &q.modes[q.n_modes++];
        snprintf(m->id, sizeof m->id, "%s", id);
        snprintf(m->label, sizeof m->label, "%s", label != NULL ? label : id);
    }
    for (int i = 0; i < q.n_modes; i++) {
        meet_counts(q.modes[i].id);
    }
}

static int is_vs(void)
{
    return strcmp(q.modes[q.mode].id, "vs") == 0;
}

/* ---- Moving between views */

static void to_failed(const char* why)
{
    snprintf(q.why, sizeof q.why, "%s", why);
    tgg_log("find a match: %s", why);
    q.view = Q_FAILED;
}

static void reset_lobby(void)
{
    q.sent_pick = 0;
    q.have_theirs = 0;
    q.started = 0;
    q.opened = 0;
    q.stage = -1;
    q.again_me = q.again_them = 0;
}

static void search(void)
{
    if (meet_search(q.modes[q.mode].id) != 0) {
        meet_poll(&dc.meet);
        to_failed(dc.meet.why[0] != '\0' ? dc.meet.why : "Can't search from here.");
        return;
    }
    q.games = 0;
    reset_lobby();
    q.view = Q_SEARCH;
}

/* The character select chose (or the player backed out) */
static void chosen(const tgg_match* match)
{
    q.choosing = 0;
    if (match == NULL || tgg_match_character(match, 0) == NULL) {
        q.chose = -1;
        return;
    }
    snprintf(q.character, sizeof q.character, "%s", tgg_match_character(match, 0));
    q.costume = tgg_match_costume(match, 0);
    q.chose = 1;
}

static void pick_mode(void)
{
    if (!seen_notice() && !auto_yes()) {
        q.view = Q_NOTICE;
        return;
    }
    if (!is_vs()) {
        search();
        return;
    }
    if (q.character[0] != '\0' && auto_yes()) {
        search(); /* TGG_DIRECT_CONNECT=search:vs:<character> */
        return;
    }
    q.view = Q_CHARACTER;
    q.chose = 0;
}

/* ---- The lobby: picks, the stage, the match */

static void send_text(const char* text)
{
    if (netplay_send(text, (int) strlen(text)) != 0) {
        tgg_log("find a match: couldn't send %s", text);
    }
}

/* The stage, as Slippi's unranked draws it (project-slippi/dolphin,
 * EXI_DeviceSlippi.cpp, getRandomStage): from a bag of the legal stages, so
 * none comes up twice until all have been played, then the bag fills again.
 * Each new opponent starts a new bag, and Slippi draws once on connecting
 * without playing it (new_opponent). Only the host's draws count: Slippi plays
 * port 1's stage. */
static int draw_stage(void)
{
    if (q.bag_n == 0) {
        for (int i = 0; i < N_STAGES; i++) {
            q.bag[i] = i;
        }
        q.bag_n = N_STAGES;
    }
    uint8_t b = 0;
    tgg_net_random(&b, 1);
    const int at = b % q.bag_n;
    const int stage = q.bag[at];
    q.bag[at] = q.bag[--q.bag_n];
    return stage;
}

static void new_opponent(void)
{
    q.bag_n = 0;
    draw_stage();
}

static void host_play(void)
{
    if (!q.have_theirs) {
        return;
    }
    if (q.stage < 0) {
        q.stage = draw_stage();
    }
    const int stage = q.stage;
    static tgg_match match;
    tgg_match_defaults(&match);
    tgg_match_set_stage(&match, STAGES[stage].id);
    tgg_match_set_stocks(&match, STOCKS);
    tgg_match_set_time(&match, MINUTES * 60);
    const char* seconds = getenv("TGG_DIRECT_CONNECT_TIME");
    if (seconds != NULL && atoi(seconds) > 0) {
        /* For scripted runs: a short time match */
        tgg_match_set_stocks(&match, 0);
        tgg_match_set_time(&match, atoi(seconds));
    }
    tgg_match_set_items(&match, -1);
    tgg_match_set_pause(&match, 0);
    const char* chars[2] = {q.character, q.their_character};
    const int costumes[2] = {q.costume, q.their_costume};
    for (int side = 0; side < 2; side++) {
        tgg_match_set_player(&match, side, TGG_PLAYER_HUMAN);
        if (tgg_match_set_character(&match, side, chars[side]) != 0) {
            to_failed("The other player picked a character this game doesn't have.");
            dc_netplay_close();
            return;
        }
        tgg_match_set_costume(&match, side, costumes[side]);
    }
    if (netplay_play(&match) == 0) {
        q.started = 1;
        tgg_log("find a match: game %d on %s, %s vs %s", q.games + 1, STAGES[stage].name, chars[0],
                chars[1]);
    }
}

static void got_message(const char* text)
{
    char word[16], arg[64];
    int n = 0;
    if (sscanf(text, "%15s %63s %d", word, arg, &n) < 1) {
        return;
    }
    if (strcmp(word, "qpick") == 0) {
        snprintf(q.their_character, sizeof q.their_character, "%s", arg);
        q.their_costume = n;
        q.have_theirs = 1;
    } else if (strcmp(word, "qagain") == 0) {
        q.again_them = 1;
    }
}

static void read_messages(void)
{
    char buf[400];
    int from = 0;
    int n;
    while ((n = netplay_receive(buf, sizeof buf - 1, &from)) > 0) {
        buf[n] = '\0';
        if (!played_got(buf, from)) {
            got_message(buf);
        }
    }
}

static void lobby_frame(void)
{
    netplay_info info;
    netplay_status(&info);
    if (info.state != NETPLAY_CONNECTED) {
        return;
    }
    if (!is_vs()) {
        /* A mod's mode: the host opens it on both games */
        if (info.side == 0 && !q.opened) {
            if (netplay_lock() == 0 && tgg_menu_pick(q.modes[q.mode].id) == 0) {
                q.opened = 1;
                tgg_log("find a match: opening %s on both games", q.modes[q.mode].id);
            }
        }
        return;
    }
    if (!q.sent_pick) {
        char msg[96];
        snprintf(msg, sizeof msg, "qpick %s %d", q.character, q.costume);
        send_text(msg);
        q.sent_pick = 1;
    }
    if (q.started) {
        return;
    }
    if (info.side == 0) {
        host_play();
    } else if (netplay_play(NULL) == 0) {
        q.started = 1;
    }
    if (q.started) {
        q.view = Q_PLAYING;
    }
}

/* ---- The screen */

static void back_to_modes(void)
{
    meet_search_stop();
    q.view = Q_MODES;
    find_modes();
}

static void frame(void)
{
    q.anim_frames++;
    /* Scripted runs (TGG_DIRECT_CONNECT_YES) answer for the player; the input
     * script is for the matches */
    const uint32_t in = auto_yes() ? 0 : tgg_menu_input();
    meet_poll(&dc.meet);
    if (dc.meet.state == MEET_FAILED && q.view != Q_FAILED) {
        tgg_sound_menu(TGG_SOUND_ERROR);
        to_failed(dc.meet.why);
    }
    netplay_info info;
    netplay_status(&info);
    if (q.view == Q_LOBBY || q.view == Q_PLAYING || q.view == Q_AFTER) {
        read_messages();
        if (info.state == NETPLAY_FAILED || info.state == NETPLAY_OFF) {
            /* They left, or the link dropped: someone else */
            char note[160];
            snprintf(note, sizeof note, "%s left.", q.opponent[0] != '\0' ? q.opponent : "They");
            tgg_log("find a match: %s %s", note, info.why);
            meet_search_again();
            snprintf(dc.meet.note, sizeof dc.meet.note, "%s", note);
            reset_lobby();
            q.games = 0;
            q.view = Q_SEARCH;
            return;
        }
    }
    switch (q.view) {
    case Q_CONNECT:
        if (in & TGG_MENU_BACK) {
            tgg_sound_menu(TGG_SOUND_BACK);
            meet_leave();
            tgg_screen_close();
        } else if (dc.meet.state == MEET_READY || dc.meet.state == MEET_SEARCHING) {
            q.view = Q_MODES;
            find_modes();
            if (auto_mode[0] != '\0') {
                /* TGG_DIRECT_CONNECT=search:<mode>... */
                int found = -1;
                for (int i = 0; i < q.n_modes; i++) {
                    if (strcmp(q.modes[i].id, auto_mode) == 0) {
                        found = i;
                    }
                }
                if (found < 0) {
                    snprintf(text_of_mode, sizeof text_of_mode, "There's no mode %s to play online.", auto_mode);
                    to_failed(text_of_mode);
                } else {
                    q.cursor = q.mode = found;
                    pick_mode();
                }
                auto_mode[0] = '\0';
            }
        }
        break;
    case Q_MODES:
        if (in & TGG_MENU_BACK) {
            tgg_sound_menu(TGG_SOUND_BACK);
            meet_leave();
            tgg_screen_close();
        } else if (in & TGG_MENU_DOWN) {
            q.cursor = (q.cursor + 1) % q.n_modes;
            tgg_sound_menu(TGG_SOUND_MOVE);
        } else if (in & TGG_MENU_UP) {
            q.cursor = (q.cursor + q.n_modes - 1) % q.n_modes;
            tgg_sound_menu(TGG_SOUND_MOVE);
        } else if (in & TGG_MENU_ACCEPT) {
            tgg_sound_menu(TGG_SOUND_ACCEPT);
            q.mode = q.cursor;
            pick_mode();
        } else {
            for (int i = 0; i < q.n_modes; i++) {
                meet_counts(q.modes[i].id); /* sent at most every 20 seconds */
            }
        }
        break;
    case Q_NOTICE:
        if (in & TGG_MENU_BACK) {
            tgg_sound_menu(TGG_SOUND_BACK);
            q.view = Q_MODES;
        } else if (in & (TGG_MENU_ACCEPT | TGG_MENU_START)) {
            tgg_sound_menu(TGG_SOUND_ACCEPT);
            remember_notice();
            pick_mode();
        }
        break;
    case Q_CHARACTER:
        if (q.chose == 1) {
            search();
        } else if (q.chose == -1) {
            q.view = Q_MODES;
        } else if (!q.choosing) {
            /* The real character select; the stage comes from the queue's rule */
            static const tgg_match_choose_how how = {.start = NULL, .stage = "melee/battlefield"};
            if (tgg_match_choose(&how, chosen) == 0) {
                q.choosing = 1;
            } else {
                q.view = Q_MODES;
            }
        }
        break;
    case Q_SEARCH:
        if (in & TGG_MENU_BACK) {
            tgg_sound_menu(TGG_SOUND_BACK);
            back_to_modes();
        } else if (dc.meet.state == MEET_CONNECTED && info.state == NETPLAY_MISMATCH) {
            /* Shouldn't happen in a queue: same mods by its key */
            tgg_log("find a match: the other game has other mods: %s", info.why);
            meet_search_again();
        } else if (dc.meet.state == MEET_CONNECTED && info.state == NETPLAY_CONNECTED) {
            snprintf(q.opponent, sizeof q.opponent, "%s", dc.meet.n_peers > 0 ? dc.meet.peers[0] : "");
            tgg_log("find a match: playing %s as side %d", q.opponent, info.side);
            tgg_sound_menu(TGG_SOUND_ACCEPT);
            reset_lobby();
            new_opponent();
            q.view = Q_LOBBY;
        }
        break;
    case Q_LOBBY:
        lobby_frame();
        break;
    case Q_PLAYING:
        break;
    case Q_AFTER: {
        const double now = tgg_net_time_ms();
        const int yes = auto_yes() != NULL;
        const char* until = getenv("TGG_DIRECT_CONNECT_MATCHES");
        const int enough = until != NULL && q.games >= atoi(until);
        if ((in & TGG_MENU_BACK) || (yes && enough) || now - q.after_ms > AGAIN_MS) {
            tgg_sound_menu(TGG_SOUND_BACK);
            tgg_log("find a match: someone else");
            meet_search_again();
            reset_lobby();
            q.games = 0;
            q.view = Q_SEARCH;
        } else if (((in & TGG_MENU_ACCEPT) || yes) && !q.again_me) {
            tgg_sound_menu(TGG_SOUND_ACCEPT);
            q.again_me = 1;
            send_text("qagain");
        }
        if (q.view == Q_AFTER && q.again_me && q.again_them) {
            tgg_log("find a match: again");
            reset_lobby();
            q.view = Q_LOBBY;
        }
        break;
    }
    case Q_FAILED:
        if (in & (TGG_MENU_ACCEPT | TGG_MENU_BACK | TGG_MENU_START)) {
            tgg_sound_menu(TGG_SOUND_BACK);
            meet_leave();
            tgg_screen_close();
        }
        break;
    }
}

static void enter(int from_match)
{
    if (from_match && q.view == Q_PLAYING) {
        q.games++;
        tgg_log("find a match: game %d over", q.games);
        q.after_ms = tgg_net_time_ms();
        q.view = Q_AFTER;
    }
}

/* ---- Drawing */

static void lines(float y, const char* text)
{
    /* Word by word, a line at a time, up to 480 wide */
    char line[160];
    const char* p = text;
    while (*p != '\0') {
        int cut = 0;
        for (int i = 0;; i++) {
            if (p[i] == ' ' || p[i] == '\0') {
                snprintf(line, sizeof line, "%.*s", i, p);
                if (cut > 0 && tgg_draw_text_width(19.0f, line) > 480.0f) {
                    break;
                }
                cut = i;
                if (p[i] == '\0' || i + 1 >= (int) sizeof line) {
                    break;
                }
            }
        }
        if (cut == 0) {
            break;
        }
        snprintf(line, sizeof line, "%.*s", cut, p);
        tgg_draw_text(320.0f, y, 19.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER, line);
        y += 28.0f;
        p += cut;
        while (*p == ' ') {
            p++;
        }
    }
}

static void draw_q_pill(float y, tgg_color dot, tgg_color border, const char* text)
{
    const float font = 16.0f;
    const float pad_x = 13.0f;
    const float dot_r = 3.5f;
    const float dot_gap = 8.0f;
    const float tw = tgg_draw_text_width(font, text);
    const float w = pad_x * 2.0f + dot_r * 2.0f + dot_gap + tw;
    const float h = 30.0f;
    const float x = 320.0f - w * 0.5f;
    tgg_ui_rounded_rect(x, y, w, h, 7.0f, TGG_UI_BG_PANEL_DEEP, border, 1.25f);
    tgg_draw_circle(x + pad_x + dot_r, y + h * 0.5f, dot_r, dot);
    tgg_draw_text(x + pad_x + dot_r * 2.0f + dot_gap, y + (h - font) * 0.5f - 0.5f, font,
                  TGG_UI_TEXT_PRIMARY, TGG_ALIGN_LEFT, text);
}

static void draw_ping_pill(float y)
{
    netplay_info info;
    netplay_status(&info);
    if (info.rtt_ms <= 0.0f) {
        return;
    }
    char ping[24];
    snprintf(ping, sizeof ping, "%d ms", (int) (info.rtt_ms + 0.5f));
    const tgg_color col = tgg_ui_ping_color(info.rtt_ms);
    draw_q_pill(y, col, TGG_UI_BORDER_SUBTLE, ping);
}

static void draw(void)
{
    static const char* const dots[4] = { "", ".", "..", "..." };
    const char* el = dots[(q.anim_frames / 22u) & 3u];
    tgg_ui_header("Find a match", NULL);
    tgg_ui_hint hints[3];
    int n_hints = 0;
    char text[320];
    switch (q.view) {
    case Q_CONNECT:
        if (dc.meet.state == MEET_SIGN_IN) {
            tgg_ui_panel(80.0f, 98.0f, 480.0f, 260.0f);
            tgg_draw_text(320.0f, 118.0f, 24.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER,
                          "Sign in to textures.gg");
            tgg_draw_text(320.0f, 162.0f, 17.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER,
                          "On your phone or computer, go to");
            tgg_draw_text_fit(text, sizeof text, 20.0f, 440.0f, dc.meet.signin_url);
            tgg_draw_text(320.0f, 190.0f, 20.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER, text);
            tgg_draw_text(320.0f, 232.0f, 17.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER, "and enter");
            tgg_ui_modal(195.0f, 260.0f, 250.0f, 60.0f);
            tgg_draw_text(320.0f, 272.0f, 36.0f, TGG_UI_TEXT_GOLD, TGG_ALIGN_CENTER,
                          dc.meet.signin_code);
        } else {
            tgg_ui_panel(110.0f, 166.0f, 420.0f, 108.0f);
            snprintf(text, sizeof text, "Connecting%s", el);
            draw_q_pill(192.0f, TGG_UI_BG_FILL, TGG_UI_BORDER_BRIGHT, text);
            tgg_draw_text(320.0f, 238.0f, 15.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER,
                          "textures.gg");
        }
        hints[n_hints++] = (tgg_ui_hint) { TGG_UI_BTN_B, "Back" };
        break;
    case Q_MODES:
        for (int i = 0; i < q.n_modes; i++) {
            const float y = 96.0f + (float) i * 66.0f;
            const int on = i == q.cursor;
            tgg_ui_row(72.0f, y, 496.0f, 56.0f, on);
            tgg_draw_text(92.0f, y + 7.0f, 22.0f,
                          on ? TGG_UI_TEXT_INK : TGG_UI_TEXT_PRIMARY, TGG_ALIGN_LEFT,
                          q.modes[i].label);
            char label[64] = "";
            const int n = meet_count(q.modes[i].id, label, sizeof label);
            if (n >= 0) {
                snprintf(text, sizeof text, "%s  %d searching", label, n);
            } else {
                snprintf(text, sizeof text, "Players with your mods");
            }
            char fit[96];
            tgg_draw_text_fit(fit, sizeof fit, 15.0f, 444.0f, text);
            tgg_draw_text(92.0f, y + 33.0f, 15.0f,
                          on ? TGG_UI_TEXT_INK : TGG_UI_TEXT_MUTED, TGG_ALIGN_LEFT, fit);
        }
        hints[n_hints++] = (tgg_ui_hint) { TGG_UI_BTN_A, "Search" };
        hints[n_hints++] = (tgg_ui_hint) { TGG_UI_BTN_B, "Back" };
        break;
    case Q_NOTICE:
        tgg_ui_modal(64.0f, 134.0f, 512.0f, 166.0f);
        tgg_draw_text(320.0f, 154.0f, 22.0f, TGG_UI_TEXT_GOLD, TGG_ALIGN_CENTER, "Before you search");
        lines(196.0f, "Find a match pairs you with someone you might not know. Like anyone you "
                      "play online, they can see your IP address.");
        hints[n_hints++] = (tgg_ui_hint) { TGG_UI_BTN_A, "OK" };
        hints[n_hints++] = (tgg_ui_hint) { TGG_UI_BTN_B, "Back" };
        break;
    case Q_CHARACTER:
        hints[n_hints++] = (tgg_ui_hint) { TGG_UI_BTN_B, "Back" };
        break;
    case Q_SEARCH: {
        const int s = (int) (dc.meet.search_ms / 1000);
        tgg_ui_panel(76.0f, 126.0f, 488.0f, 198.0f);
        if (dc.meet.state == MEET_PUNCHING) {
            snprintf(text, sizeof text, "Found %s",
                     dc.meet.n_peers > 0 ? dc.meet.peers[0] : "someone");
            tgg_draw_text(320.0f, 150.0f, 26.0f, TGG_UI_TEXT_GOLD, TGG_ALIGN_CENTER, text);
            snprintf(text, sizeof text, "Connecting%s", el);
            draw_q_pill(194.0f, TGG_UI_TEXT_GOLD, TGG_UI_BORDER_BRIGHT, text);
        } else if (dc.meet.state == MEET_CONNECTED) {
            draw_q_pill(176.0f, TGG_UI_TEXT_GOOD, TGG_UI_TEXT_GOOD, "Connected");
        } else {
            snprintf(text, sizeof text, "Searching%s  %d:%02d", el, s / 60, s % 60);
            tgg_draw_text(320.0f, 150.0f, 26.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER, text);
            if (dc.meet.label[0] != '\0') {
                snprintf(text, sizeof text, "%s  %d searching", dc.meet.label,
                         dc.meet.searching > 0 ? dc.meet.searching : 1);
                tgg_draw_text(320.0f, 198.0f, 17.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER, text);
            }
        }
        if (dc.meet.note[0] != '\0') {
            lines(248.0f, dc.meet.note);
        }
        hints[n_hints++] = (tgg_ui_hint) { TGG_UI_BTN_B, "Stop searching" };
        break;
    }
    case Q_LOBBY:
        tgg_ui_panel(100.0f, 156.0f, 440.0f, 126.0f);
        snprintf(text, sizeof text, "Playing %s", q.opponent[0] != '\0' ? q.opponent : "someone");
        tgg_draw_text(320.0f, 182.0f, 22.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER, text);
        draw_ping_pill(226.0f);
        hints[n_hints++] = (tgg_ui_hint) { TGG_UI_BTN_B, "Back" };
        break;
    case Q_PLAYING:
        hints[n_hints++] = (tgg_ui_hint) { TGG_UI_BTN_B, "Back" };
        break;
    case Q_AFTER:
        tgg_ui_modal(90.0f, 150.0f, 460.0f, 134.0f);
        if (q.again_me) {
            snprintf(text, sizeof text, "Waiting for %s%s",
                     q.opponent[0] != '\0' ? q.opponent : "them", el);
        } else {
            snprintf(text, sizeof text, "Play %s again?",
                     q.opponent[0] != '\0' ? q.opponent : "them");
        }
        tgg_draw_text(320.0f, 178.0f, 24.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER, text);
        draw_ping_pill(226.0f);
        hints[n_hints++] = (tgg_ui_hint) { TGG_UI_BTN_A, "Play again" };
        hints[n_hints++] = (tgg_ui_hint) { TGG_UI_BTN_B, "Find someone else" };
        break;
    case Q_FAILED:
        tgg_ui_modal(72.0f, 158.0f, 496.0f, 120.0f);
        tgg_draw_text(320.0f, 178.0f, 21.0f, TGG_UI_TEXT_GOLD, TGG_ALIGN_CENTER, "Match ended");
        lines(216.0f, q.why);
        hints[n_hints++] = (tgg_ui_hint) { TGG_UI_BTN_A, "OK" };
        break;
    }
    tgg_ui_footer_hints(hints, n_hints);
}

static const tgg_screen screen = {
    .enter = enter,
    .frame = frame,
    .draw = draw,
};
TGG_SCREEN("find-match", screen);

/* ---- Online's entry, and starting from the environment */

static void start(void)
{
    memset(&q, 0, sizeof q);
    q.view = Q_CONNECT;
    if (meet_online() != 0) {
        meet_poll(&dc.meet);
        to_failed(dc.meet.why[0] != '\0' ? dc.meet.why : "Can't go online from here.");
    }
}

void queue_open(void)
{
    if (dc.mode != MODE_NONE) {
        tgg_sound_menu(TGG_SOUND_ERROR);
        return;
    }
    start();
    tgg_sound_menu(TGG_SOUND_ACCEPT);
    tgg_screen_open(&screen);
}

int queue_shown(void)
{
    return tgg_screen_shown() == &screen;
}

/* TGG_DIRECT_CONNECT=search[:<mode>[:<character>[:<costume>]]]: called each frame
 * until its screen is up */
int queue_autostart(const char* spec)
{
    static int started;
    if (started) {
        if (tgg_screen_shown() != &screen && !tgg_in_match()) {
            tgg_screen_open(&screen);
        }
        return tgg_screen_shown() == &screen;
    }
    started = 1;
    start();
    char mode[64] = "vs", character[64] = "";
    int costume = 0;
    if (spec[0] == ':') {
        sscanf(spec + 1, "%63[^:]:%63[^:]:%d", mode, character, &costume);
    }
    snprintf(auto_mode, sizeof auto_mode, "%s", mode);
    snprintf(q.character, sizeof q.character, "%s", character);
    q.costume = costume;
    tgg_log("find a match: starting a search for %s as %s", auto_mode, character);
    return 0;
}

int queue_busy(void)
{
    return (q.view >= Q_SEARCH && q.view <= Q_AFTER) || (q.view == Q_CHARACTER && q.choosing);
}

int queue_companion_state(tgg_companion_online* out)
{
    if (!queue_shown() && !queue_busy()) {
        return 0;
    }
    memset(out, 0, sizeof *out);
    out->queue_label = dc.meet.label;
    if (q.opponent[0] != '\0') {
        out->players[0] = q.opponent;
        out->n_players = 1;
    }
    out->signin_url = dc.meet.signin_url;
    out->signin_code = dc.meet.signin_code;
    switch (q.view) {
    case Q_CONNECT:
        out->state = dc.meet.state == MEET_SIGN_IN ? "signing-in" : "connecting";
        break;
    case Q_MODES:
    case Q_NOTICE:
    case Q_CHARACTER:
        out->state = "choosing";
        break;
    case Q_SEARCH:
        out->state = "searching";
        /* Quantize to whole seconds so companion deduplication sends at most 1 msg/sec */
        out->search_ms = dc.meet.search_ms > 0.0
                             ? ((uint64_t) dc.meet.search_ms / 1000) * 1000
                             : 0;
        break;
    case Q_LOBBY:
    case Q_PLAYING:
    case Q_AFTER:
        out->state = "matched";
        break;
    case Q_FAILED:
        out->state = "ended";
        out->why = q.why;
        break;
    default:
        out->state = "idle";
        break;
    }
    return 1;
}

void queue_companion_stop(void)
{
    const int was_choosing = q.choosing;
    dc.ended_why[0] = '\0';
    meet_leave();
    dc_netplay_close();
    memset(&q, 0, sizeof q);
    if (tgg_screen_shown() == &screen || was_choosing) {
        tgg_screen_close();
    }
}

void queue_companion_start(const char* mode)
{
    dc.ended_why[0] = '\0';
    if (mode != NULL && mode[0] != '\0') {
        snprintf(auto_mode, sizeof auto_mode, "%s", mode);
    } else {
        snprintf(auto_mode, sizeof auto_mode, "vs");
    }
    queue_open();
}
