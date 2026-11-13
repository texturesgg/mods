/* SPDX-License-Identifier: GPL-3.0-or-later */
/* L-cancel trainer: after every aerial landing that could be L-canceled, a
 * line by the player's percent says whether it was, and by how many frames it
 * was missed, with the running rate.
 *
 * It reads the game's own decision. When a fighter lands from an aerial,
 * ftCo_LandingAir_EnterWithLag halves the landing lag if L, R or Z was
 * pressed fewer than p_ftCommonData->xE4 (7) frames before (fp->x67F counts
 * the frames since). A before hook there reads both, so "early" is exact and
 * analog presses count as the game counts them. A press during the landing
 * lag after a miss counts as late, by the frames since the landing.
 *
 * Display only: before hooks and fighter_update, which only read. The result
 * of each landing is simulation state (TGG_STATE), so it's right through a
 * rollback; the rate is counted from presentation code once a landing's frame
 * is final, so a frame run again is never counted twice.
 */
#include "tgg/game/bridge.h"
#include "tgg/game/hooks.h"
#include "tgg/draw.h"
#include "tgg/events.h"
#include "tgg/rollback.h"
#include "tgg/settings.h"

#include <melee/ft/fighter.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_LandingAir.h>
#include <melee/ft/types.h>
#include <melee/gm/forward.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/if/ifstatus.h>
#include <melee/if/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

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

static int is_aerial(int msid)
{
    return msid >= ftCo_MS_AttackAirN && msid <= ftCo_MS_AttackAirLw;
}

static int is_aerial_landing(int msid)
{
    return msid >= ftCo_MS_LandingAirN && msid <= ftCo_MS_LandingAirLw;
}

/* A fighter touches down: the game is about to pick the landing lag */
static void before_landing(Fighter_GObj* gobj)
{
    const Fighter* fp = GET_FIGHTER(gobj);
    const int port = fp->player_idx;
    /* cmd_vars[0] is the aerial's "landing lag applies" flag: clear, it
     * auto-cancels and there's nothing to L-cancel. Nana follows Popo. */
    if (port < 0 || port >= 4 || fp->is_sub_fighter || !fp->cmd_vars[0] ||
        !is_aerial(fp->motion_id)) {
        return;
    }
    const int since = fp->x67F;
    const int window = p_ftCommonData->xE4;
    if (since < window) {
        decide(port, RESULT_CANCEL, 0);
    } else if (since - window < EARLY_MAX) {
        decide(port, RESULT_EARLY, since - window + 1);
    } else {
        st.port[port].waiting = 1;
        st.port[port].landed = st.tick;
    }
}
TGG_BEFORE(ftCo_LandingAir_EnterWithLag, before_landing);

/* While a missed landing's lag lasts, a press is late */
static void on_fighter_update(tgg_fighter fighter)
{
    const Fighter* fp = GET_FIGHTER(tgg_fighter_gobj(fighter));
    const int port = fp->player_idx;
    if (port < 0 || port >= 4 || fp->is_sub_fighter || !st.port[port].waiting) {
        return;
    }
    if (!is_aerial_landing(fp->motion_id)) {
        decide(port, RESULT_MISSED, 0);
    } else if (fp->x67F == 0) {
        decide(port, RESULT_LATE, st.tick - st.port[port].landed);
    }
}
TGG_ON(fighter_update, on_fighter_update);

/* ---- What the player sees ---------------------------------------------- */

/* Displayed frames a result stays up, the last FADE of them fading */
#define SHOW 100
#define FADE 20

/* The HUD camera to the screen (measured), and the gap above the percent */
#define HUD_CENTER_X 355.0f
#define HUD_SCALE_X 10.8f
#define HUD_SCALE_Y 10.0f
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
           (where == WHERE_EVERY_MATCH || gm_GetCurrentGameMode() == GM_TRAINING);
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
 * percent's center and y the bottom of the HUD, from the HUD's own position
 * for the port (HUD camera units, measured against the screen). 0 when the
 * port has no HUD. */
static int hud_anchor(int port, float* x, float* y)
{
    const IfDamageState* hud = &ifStatus_GetHUDInfo()->players[port];
    if (hud->HUD_parent_entity == NULL) {
        return 0;
    }
    HSD_JObj* jobj = HSD_GObjGetHSDObj(hud->HUD_parent_entity);
    if (jobj == NULL) {
        return 0;
    }
    *x = HUD_CENTER_X + HSD_JObjGetTranslationX(jobj) * HUD_SCALE_X;
    *y = 240.0f - HSD_JObjGetTranslationY(jobj) * HUD_SCALE_Y;
    return 1;
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
        if (view.age[p] >= SHOW || Player_GetPlayerSlotType(p) != Gm_PKind_Human) {
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
