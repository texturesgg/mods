/* SPDX-License-Identifier: GPL-3.0-or-later */
/* L-cancel trainer: after every aerial landing that could be L-canceled, a
 * line by the player's percent says whether it was, and by how many frames it
 * was missed, with the running rate.
 *
 * It reads the game's own decision. When a fighter lands from an aerial, the
 * game halves the landing lag if L, R or Z was pressed fewer than the window
 * (7) frames before; the fighter_land event passes both, so "early" is exact
 * and analog presses count as the game counts them. A press during the
 * landing lag after a miss counts as late, by the frames since the landing.
 *
 * Display only: fighter_land and fighter_update, which only read. The result
 * of each landing is simulation state (TGG_STATE), so it's right through a
 * rollback; the rate is counted from presentation code once a landing's frame
 * is final, so a frame run again is never counted twice.
 */
#include "tgg/action.h"
#include "tgg/draw.h"
#include "tgg/events.h"
#include "tgg/fighter.h"
#include "tgg/mode.h"
#include "tgg/player.h"
#include "tgg/rollback.h"
#include "tgg/settings.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum { WHERE_TRAINING, WHERE_EVERY_MATCH };
static int where;
TGG_SETTING_CHOICE_LOCAL(where, "where", "Show in", WHERE_TRAINING, "Training Mode",
                         "Every match");
static int show_rate;
TGG_SETTING_BOOL_LOCAL(show_rate, "rate", "Show the rate", 1);

enum { RESULT_CANCEL = 1, RESULT_EARLY, RESULT_LATE, RESULT_MISSED };

/* A press further back than this before landing isn't "early", just missed */
#define EARLY_MAX 20

/* What the simulation decides, per port */
static struct {
    int tick; /* logic frames into the match */
    struct {
        int seq;     /* landings decided so far */
        int result;  /* the last one's RESULT_* */
        int frames;  /* frames early or late */
        int frame;   /* tgg_frame() it was decided on */
        int waiting; /* landed without a press in time: watching for a late one */
        int landed;  /* tick of that landing */
    } port[4];
} st;
TGG_STATE(st);

static int showing(void);
static void label(char* out, size_t cap, int result, int frames);

static void decide(int port, int result, int frames)
{
    st.port[port].seq++;
    st.port[port].result = result;
    st.port[port].frames = frames;
    st.port[port].frame = tgg_frame();
    st.port[port].waiting = 0;
    if (showing()) {
        char text[32];
        label(text, sizeof text, result, frames);
        tgg_log("P%d: %s", port + 1, text); /* dropped during a re-run */
    }
}

static void on_match_start(const tgg_match* match)
{
    (void) match;
    memset(&st, 0, sizeof st);
}
TGG_ON(match_start, on_match_start);

static void on_tick(void)
{
    if (tgg_in_match()) {
        st.tick++;
    }
}
TGG_ON(tick, on_tick);

static int is_aerial(int action)
{
    return action >= TGG_ACTION_ATTACK_AIR_N && action <= TGG_ACTION_ATTACK_AIR_LW;
}

static int is_aerial_landing(int action)
{
    return action >= TGG_ACTION_LANDING_AIR_N && action <= TGG_ACTION_LANDING_AIR_LW;
}

/* A fighter touches down: the game is about to pick the landing lag */
static void on_land(tgg_fighter fighter, const tgg_landing* land)
{
    const int port = tgg_fighter_port(fighter);
    /* Without landing lag it auto-cancels and there's nothing to L-cancel.
     * Nana follows Popo. */
    if (port < 0 || port >= 4 || tgg_fighter_follower(fighter) || !land->lag_applies ||
        !is_aerial(land->action)) {
        return;
    }
    const int since = land->press_age;
    const int window = land->window;
    if (since < window) {
        decide(port, RESULT_CANCEL, 0);
    } else if (since - window < EARLY_MAX) {
        decide(port, RESULT_EARLY, since - window + 1);
    } else {
        st.port[port].waiting = 1;
        st.port[port].landed = st.tick;
    }
}
TGG_ON(fighter_land, on_land);

/* While a missed landing's lag lasts, a press is late */
static void on_fighter_update(tgg_fighter fighter)
{
    const int port = tgg_fighter_port(fighter);
    if (port < 0 || port >= 4 || tgg_fighter_follower(fighter) || !st.port[port].waiting) {
        return;
    }
    if (!is_aerial_landing(tgg_fighter_action(fighter))) {
        decide(port, RESULT_MISSED, 0);
    } else if (tgg_fighter_cancel_press_age(fighter) == 0) {
        decide(port, RESULT_LATE, st.tick - st.port[port].landed);
    }
}
TGG_ON(fighter_update, on_fighter_update);

/* ---- What the player sees ---------------------------------------------- */

/* Displayed frames a result stays up, the last FADE of them fading */
#define SHOW 100
#define FADE 20

/* The gap above the percent */
#define HUD_ABOVE 95.0f

static struct {
    int in_match;
    int seen[4];  /* st.port[].seq counted so far */
    int hits[4];  /* L-cancels */
    int tries[4]; /* landings that could be L-canceled */
    int age[4];   /* displayed frames since the last result, SHOW when none */
    int result[4], frames[4];
} view;

static int showing(void)
{
    return tgg_in_match() &&
           (where == WHERE_EVERY_MATCH || tgg_mode() == TGG_MODE_TRAINING);
}

/* Before each logic frame: counts the landings whose frame is final */
static void on_advance(tgg_advance* adv)
{
    (void) adv;
    if (!tgg_in_match()) {
        view.in_match = 0;
        return;
    }
    if (!view.in_match) {
        memset(&view, 0, sizeof view);
        view.in_match = 1;
        for (int p = 0; p < 4; p++) {
            view.age[p] = SHOW;
        }
    }
    const int final = tgg_frame_final();
    for (int p = 0; p < 4; p++) {
        if (st.port[p].seq < view.seen[p]) {
            view.seen[p] = st.port[p].seq; /* the state went back (a rollback past it) */
        }
        if (st.port[p].seq > view.seen[p] && (final == INT_MAX || st.port[p].frame <= final)) {
            view.seen[p] = st.port[p].seq;
            view.tries[p]++;
            view.hits[p] += st.port[p].result == RESULT_CANCEL;
            view.result[p] = st.port[p].result;
            view.frames[p] = st.port[p].frames;
            view.age[p] = 0;
        }
    }
}
TGG_ON(advance, on_advance);

static void on_frame(void)
{
    for (int p = 0; p < 4; p++) {
        if (view.age[p] < SHOW) {
            view.age[p]++;
        }
    }
}
TGG_ON(frame, on_frame);

/* Where the game draws a player's percent, in the 640 by 480 screen: x is the
 * percent's center and y the bottom of the HUD. 0 when the port has no HUD. */
static int hud_anchor(int port, float* x, float* y)
{
    *x = tgg_player_hud_x(port);
    *y = tgg_player_hud_y(port);
    return *x >= 0;
}

static void label(char* out, size_t cap, int result, int frames)
{
    switch (result) {
    case RESULT_CANCEL:
        snprintf(out, cap, "L-cancel");
        break;
    case RESULT_EARLY:
        snprintf(out, cap, "%d frame%s early", frames, frames == 1 ? "" : "s");
        break;
    case RESULT_LATE:
        snprintf(out, cap, "%d frame%s late", frames, frames == 1 ? "" : "s");
        break;
    default:
        snprintf(out, cap, "Missed");
        break;
    }
}

static void on_hud(void)
{
    if (!showing()) {
        return;
    }
    for (int p = 0; p < 4; p++) {
        if (view.age[p] >= SHOW || tgg_player_kind(p) != TGG_PLAYER_HUMAN) {
            continue;
        }
        float x, y;
        if (!hud_anchor(p, &x, &y)) {
            continue;
        }
        const int left = SHOW - view.age[p];
        const float a = left < FADE ? (float) left / FADE : 1.0f;
        char text[32];
        label(text, sizeof text, view.result[p], view.frames[p]);
        char rate[32] = "";
        if (show_rate && view.tries[p] > 0) {
            snprintf(rate, sizeof rate, "%d/%d  %d%%", view.hits[p], view.tries[p],
                     view.hits[p] * 100 / view.tries[p]);
        }
        const float w = 20 + (tgg_text_width(20, text) > tgg_text_width(16, rate)
                                  ? tgg_text_width(20, text)
                                  : tgg_text_width(16, rate));
        const float h = rate[0] != '\0' ? 46 : 28;
        const float top = y - HUD_ABOVE - h;
        tgg_draw_rect(x - w / 2, top, w, h, TGG_RGBA(0, 0, 0, 150 * a));
        const tgg_color good = TGG_RGBA(130, 235, 130, 255 * a);
        const tgg_color bad = TGG_RGBA(255, 120, 100, 255 * a);
        tgg_draw_text(x, top + 4, 20, view.result[p] == RESULT_CANCEL ? good : bad,
                      TGG_ALIGN_CENTER, text);
        if (rate[0] != '\0') {
            tgg_draw_text(x, top + 26, 16, TGG_RGBA(230, 230, 240, 255 * a), TGG_ALIGN_CENTER,
                          rate);
        }
    }
}
TGG_ON(hud_draw, on_hud);
