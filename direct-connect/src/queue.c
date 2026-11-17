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
 *   lobby        the two games trade picks, the host makes the match with the
 *                queue's stage rule (STAGE_RULE), and it plays
 *   after        A plays again, B finds someone else
 *
 * A mode a mod offers online (a shared main menu entry, tgg/menu.h) needs no
 * code for this: once paired, the host's game picks its entry, which opens it
 * on both games, as if the host had picked it from the main menu.
 *
 * In the lobby the games talk in short messages (netplay_send):
 *   qpick <character> <costume>    each game's pick
 *   qban <stage>, qstage <stage>   the counterpick rule: the winner's ban and
 *                                  the loser's pick
 *   qwin <side>                    the host: who won the last match
 *   qagain                         this player wants another match
 */
#include "dc.h"

#include "tgg/draw.h"
#include "tgg/events.h"
#include "tgg/files.h"
#include "tgg/input.h"
#include "tgg/fighter.h"
#include "tgg/match.h"
#include "tgg/menu.h"
#include "tgg/net.h"
#include "tgg/screen.h"
#include "tgg/session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The stage rule. STAGES_RANDOM: a random legal stage every game, drawn by
 * the host, as Slippi's unranked does. STAGES_COUNTERPICK: game one random,
 * then the last game's winner bans one and the loser picks from the rest. */
enum { STAGES_RANDOM, STAGES_COUNTERPICK };
#define STAGE_RULE STAGES_RANDOM

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
    int winner;     /* the last match's winner's side, -1 not known */
    int banned;     /* the winner's ban, -1 none yet */
    int stage;      /* the loser's pick, -1 none yet */
    int again_me, again_them;
    double after_ms;
    /* What the last match looked like, for who won */
    int stocks[2];
    float percent[2];
    char opponent[64];
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
    q.banned = -1;
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
    q.winner = -1;
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

static int random_stage(void)
{
    uint8_t b = 0;
    tgg_net_random(&b, 1);
    return b % N_STAGES;
}

/* The host: the stage this game plays on, or -1 while it isn't known yet */
static int stage_now(void)
{
    if (STAGE_RULE == STAGES_RANDOM || q.games == 0 || q.winner < 0) {
        return random_stage();
    }
    return q.stage;
}

static void host_play(void)
{
    const int stage = stage_now();
    if (stage < 0 || !q.have_theirs) {
        return;
    }
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
            netplay_close();
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

static void got_message(const char* text, int from)
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
    } else if (strcmp(word, "qban") == 0) {
        q.banned = atoi(arg);
    } else if (strcmp(word, "qstage") == 0) {
        q.stage = atoi(arg);
    } else if (strcmp(word, "qwin") == 0 && from == 0) {
        q.winner = atoi(arg);
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
        got_message(buf, from);
    }
}

/* Counterpick: the stage list's input, for whoever has to choose now */
static void counterpick_frame(uint32_t in, int me)
{
    if (STAGE_RULE != STAGES_COUNTERPICK || q.games == 0 || q.winner < 0 || q.stage >= 0) {
        return;
    }
    const int banning = q.banned < 0;
    const int chooser = banning ? q.winner : 1 - q.winner;
    if (me != chooser) {
        return;
    }
    if (in & TGG_MENU_DOWN) {
        q.cursor = (q.cursor + 1) % N_STAGES;
        tgg_menu_sound(TGG_SOUND_MOVE);
    } else if (in & TGG_MENU_UP) {
        q.cursor = (q.cursor + N_STAGES - 1) % N_STAGES;
        tgg_menu_sound(TGG_SOUND_MOVE);
    } else if ((in & TGG_MENU_ACCEPT) || auto_yes()) {
        if (!banning && q.cursor == q.banned) {
            q.cursor = (q.cursor + 1) % N_STAGES;
            if (!auto_yes()) {
                tgg_menu_sound(TGG_SOUND_ERROR);
                return;
            }
        }
        char msg[32];
        snprintf(msg, sizeof msg, "%s %d", banning ? "qban" : "qstage", q.cursor);
        send_text(msg);
        if (banning) {
            q.banned = q.cursor;
        } else {
            q.stage = q.cursor;
        }
        tgg_menu_sound(TGG_SOUND_ACCEPT);
    }
}

static void lobby_frame(uint32_t in)
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
    counterpick_frame(in, info.side);
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
    /* Scripted runs (TGG_DIRECT_CONNECT_YES) answer for the player; the input
     * script is for the matches */
    const uint32_t in = auto_yes() ? 0 : tgg_menu_input();
    meet_poll(&dc.meet);
    if (dc.meet.state == MEET_FAILED && q.view != Q_FAILED) {
        tgg_menu_sound(TGG_SOUND_ERROR);
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
            q.winner = -1;
            q.view = Q_SEARCH;
            return;
        }
    }
    switch (q.view) {
    case Q_CONNECT:
        if (in & TGG_MENU_BACK) {
            tgg_menu_sound(TGG_SOUND_BACK);
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
            tgg_menu_sound(TGG_SOUND_BACK);
            meet_leave();
            tgg_screen_close();
        } else if (in & TGG_MENU_DOWN) {
            q.cursor = (q.cursor + 1) % q.n_modes;
            tgg_menu_sound(TGG_SOUND_MOVE);
        } else if (in & TGG_MENU_UP) {
            q.cursor = (q.cursor + q.n_modes - 1) % q.n_modes;
            tgg_menu_sound(TGG_SOUND_MOVE);
        } else if (in & TGG_MENU_ACCEPT) {
            tgg_menu_sound(TGG_SOUND_ACCEPT);
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
            tgg_menu_sound(TGG_SOUND_BACK);
            q.view = Q_MODES;
        } else if (in & (TGG_MENU_ACCEPT | TGG_MENU_START)) {
            tgg_menu_sound(TGG_SOUND_ACCEPT);
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
            static const tgg_choose_how how = {.start = NULL, .stage = "melee/battlefield"};
            if (tgg_choose_match(&how, chosen) == 0) {
                q.choosing = 1;
            } else {
                q.view = Q_MODES;
            }
        }
        break;
    case Q_SEARCH:
        if (in & TGG_MENU_BACK) {
            tgg_menu_sound(TGG_SOUND_BACK);
            back_to_modes();
        } else if (dc.meet.state == MEET_CONNECTED && info.state == NETPLAY_MISMATCH) {
            /* Shouldn't happen in a queue: same mods by its key */
            tgg_log("find a match: the other game has other mods: %s", info.why);
            meet_search_again();
        } else if (dc.meet.state == MEET_CONNECTED && info.state == NETPLAY_CONNECTED) {
            snprintf(q.opponent, sizeof q.opponent, "%s", dc.meet.peer);
            tgg_log("find a match: playing %s as side %d", q.opponent, info.side);
            tgg_menu_sound(TGG_SOUND_ACCEPT);
            reset_lobby();
            q.view = Q_LOBBY;
        }
        break;
    case Q_LOBBY:
        lobby_frame(in);
        break;
    case Q_PLAYING:
        break;
    case Q_AFTER: {
        const double now = tgg_net_time_ms();
        const int yes = auto_yes() != NULL;
        const char* until = getenv("TGG_DIRECT_CONNECT_MATCHES");
        const int enough = until != NULL && q.games >= atoi(until);
        if ((in & TGG_MENU_BACK) || (yes && enough) || now - q.after_ms > AGAIN_MS) {
            tgg_menu_sound(TGG_SOUND_BACK);
            tgg_log("find a match: someone else");
            meet_search_again();
            reset_lobby();
            q.games = 0;
            q.winner = -1;
            q.view = Q_SEARCH;
        } else if (((in & TGG_MENU_ACCEPT) || yes) && !q.again_me) {
            tgg_menu_sound(TGG_SOUND_ACCEPT);
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
            tgg_menu_sound(TGG_SOUND_BACK);
            meet_leave();
            tgg_screen_close();
        }
        break;
    }
}

/* Who won: more stocks, then less damage; -1 for a tie */
static int winner_of_last(void)
{
    if (q.stocks[0] != q.stocks[1]) {
        return q.stocks[0] > q.stocks[1] ? 0 : 1;
    }
    if (q.percent[0] != q.percent[1]) {
        return q.percent[0] < q.percent[1] ? 0 : 1;
    }
    return -1;
}

static void enter(int from_match)
{
    if (!from_match) {
        return;
    }
    if (q.view == Q_PLAYING) {
        q.games++;
        netplay_info info;
        netplay_status(&info);
        if (info.side == 0) {
            q.winner = winner_of_last();
            if (q.winner < 0) {
                q.winner = random_stage() % 2;
            }
            char msg[16];
            snprintf(msg, sizeof msg, "qwin %d", q.winner);
            send_text(msg);
        } else {
            q.winner = -1; /* the host says */
        }
        tgg_log("find a match: game %d over", q.games);
        q.after_ms = tgg_net_time_ms();
        q.view = Q_AFTER;
    }
}

/* What the last match looked like, while it runs */
static void on_match_frame(void)
{
    if (!tgg_in_match() || tgg_screen_shown() != NULL) {
        return;
    }
    if (q.view != Q_PLAYING) {
        return;
    }
    for (int side = 0; side < 2; side++) {
        const tgg_fighter f = tgg_fighter_at(side, 0);
        if (f != TGG_NO_FIGHTER) {
            q.stocks[side] = tgg_fighter_stocks(f);
            q.percent[side] = tgg_fighter_percent(f);
        }
    }
}
TGG_ON(frame, on_match_frame);

/* ---- Drawing */

#define WHITE TGG_RGBA(255, 255, 255, 255)
#define GOLD TGG_RGBA(255, 200, 0, 255)
#define DIM TGG_RGBA(170, 180, 210, 255)
#define PANEL TGG_RGBA(10, 12, 40, 200)
#define BAND TGG_RGBA(0, 0, 0, 130)

static void lines(float y, const char* text)
{
    /* Word by word, a line at a time, up to 520 wide */
    char line[160];
    const char* p = text;
    while (*p != '\0') {
        int cut = 0;
        for (int i = 0;; i++) {
            if (p[i] == ' ' || p[i] == '\0') {
                snprintf(line, sizeof line, "%.*s", i, p);
                if (cut > 0 && tgg_text_width(20, line) > 520) {
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
        tgg_draw_text(320, y, 20, WHITE, TGG_ALIGN_CENTER, line);
        y += 28;
        p += cut;
        while (*p == ' ') {
            p++;
        }
    }
}

static void draw(void)
{
    tgg_draw_rect(-200, 0, 1040, 64, BAND);
    tgg_draw_text(40, 16, 32, WHITE, TGG_ALIGN_LEFT, "Find a match");
    const char* help = "B: back";
    char text[320];
    switch (q.view) {
    case Q_CONNECT:
        if (dc.meet.state == MEET_SIGN_IN) {
            snprintf(text, sizeof text, "Sign in at %s with the code %s.", dc.meet.signin_url,
                     dc.meet.signin_code);
            lines(200, text);
        } else {
            lines(200, "Connecting to textures.gg.");
        }
        break;
    case Q_MODES:
        for (int i = 0; i < q.n_modes; i++) {
            const float y = 110 + (float) i * 64;
            tgg_draw_rect(80, y, 480, 52, PANEL);
            tgg_draw_text(100, y + 6, 24, i == q.cursor ? GOLD : WHITE, TGG_ALIGN_LEFT,
                          q.modes[i].label);
            char label[64] = "";
            const int n = meet_count(q.modes[i].id, label, sizeof label);
            if (n >= 0) {
                snprintf(text, sizeof text, "%s: %d searching", label, n);
            } else {
                snprintf(text, sizeof text, "Players with your mods");
            }
            char fit[96];
            tgg_text_fit(fit, sizeof fit, 16, 440, text);
            tgg_draw_text(100, y + 32, 16, DIM, TGG_ALIGN_LEFT, fit);
        }
        help = "A: search   B: back";
        break;
    case Q_NOTICE:
        lines(150, "Find a match pairs you with someone you might not know. Like anyone you "
                   "play online, they can see your IP address.");
        help = "A: OK   B: back";
        break;
    case Q_CHARACTER:
        break;
    case Q_SEARCH: {
        const int s = (int) (dc.meet.search_ms / 1000);
        if (dc.meet.state == MEET_PUNCHING) {
            snprintf(text, sizeof text, "Found %s. Connecting.",
                     dc.meet.peer[0] != '\0' ? dc.meet.peer : "someone");
        } else if (dc.meet.state == MEET_CONNECTED) {
            snprintf(text, sizeof text, "Connected.");
        } else {
            snprintf(text, sizeof text, "Searching %d:%02d", s / 60, s % 60);
        }
        tgg_draw_text(320, 150, 32, WHITE, TGG_ALIGN_CENTER, text);
        if (dc.meet.label[0] != '\0') {
            snprintf(text, sizeof text, "%s: %d searching", dc.meet.label,
                     dc.meet.searching > 0 ? dc.meet.searching : 1);
            tgg_draw_text(320, 200, 20, DIM, TGG_ALIGN_CENTER, text);
        }
        if (dc.meet.note[0] != '\0') {
            lines(260, dc.meet.note);
        }
        help = "B: stop searching";
        break;
    }
    case Q_LOBBY:
        if (STAGE_RULE == STAGES_COUNTERPICK && q.games > 0 && q.winner >= 0 && q.stage < 0) {
            netplay_info info;
            netplay_status(&info);
            const int banning = q.banned < 0;
            const int chooser = banning ? q.winner : 1 - q.winner;
            if (info.side != chooser) {
                snprintf(text, sizeof text, "%s is %s a stage.",
                         q.opponent[0] != '\0' ? q.opponent : "The other player",
                         banning ? "banning" : "picking");
                lines(200, text);
            } else {
                tgg_draw_text(320, 80, 22, WHITE, TGG_ALIGN_CENTER,
                              banning ? "You won. Ban a stage." : "Pick the stage.");
                for (int i = 0; i < N_STAGES; i++) {
                    const tgg_color c = i == q.banned ? DIM : i == q.cursor ? GOLD : WHITE;
                    tgg_draw_text(320, 120 + (float) i * 40, 22, c, TGG_ALIGN_CENTER,
                                  STAGES[i].name);
                }
                help = "A: choose";
            }
        } else {
            snprintf(text, sizeof text, "Playing %s.", q.opponent[0] != '\0' ? q.opponent : "someone");
            lines(200, text);
        }
        break;
    case Q_PLAYING:
        break;
    case Q_AFTER:
        if (q.again_me) {
            snprintf(text, sizeof text, "Waiting for %s.", q.opponent[0] != '\0' ? q.opponent : "them");
        } else {
            snprintf(text, sizeof text, "Play %s again?", q.opponent[0] != '\0' ? q.opponent : "them");
        }
        lines(180, text);
        help = "A: play again   B: find someone else";
        break;
    case Q_FAILED:
        lines(180, q.why);
        help = "A: OK";
        break;
    }
    tgg_draw_rect(-200, 440, 1040, 40, BAND);
    tgg_draw_text(320, 450, 18, DIM, TGG_ALIGN_CENTER, help);
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
    q.winner = -1;
    q.view = Q_CONNECT;
    if (meet_online() != 0) {
        meet_poll(&dc.meet);
        to_failed(dc.meet.why[0] != '\0' ? dc.meet.why : "Can't go online from here.");
    }
}

void queue_open(void)
{
    if (dc.mode != MODE_NONE) {
        tgg_menu_sound(TGG_SOUND_ERROR);
        return;
    }
    start();
    tgg_menu_sound(TGG_SOUND_ACCEPT);
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
    return q.view >= Q_SEARCH && q.view <= Q_AFTER;
}
