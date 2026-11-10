/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Ledge dash trainer: after each ledge dash (let go of the ledge, jump, air
 * dodge onto the stage), a breakdown above the player's percent: the frames
 * on the ledge, from the drop to the jump and from the jump to the air dodge,
 * and how much of the ledge's intangibility was left once the landing lag
 * ended. That last number is the one that matters: above zero, nobody could
 * hit you the moment you could act.
 *
 * It follows each fighter's motion state in fighter_update: hanging
 * (CliffCatch, CliffWait), off the ledge, a jump (jumps used goes up),
 * EscapeAir, then LandingFallSpecial, the waveland's landing. The
 * intangibility is the game's own timer (fp->x1990, which the ledge grab sets
 * through ftColl_8007B760 and the fighter's update counts down).
 *
 * Display only: fighter_update, which only reads. The attempt in progress and
 * each result live in TGG_STATE, so they're right through a rollback, and a
 * result is counted once its frame is final, as the L-cancel trainer does.
 */
#include "tgg/draw.h"
#include "tgg/events.h"
#include "tgg/rollback.h"
#include "tgg/settings.h"

#include <melee/ft/fighter.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/forward.h>
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

enum {
    PHASE_NONE,
    PHASE_LEDGE,   /* hanging */
    PHASE_DROPPED, /* let go, no jump yet */
    PHASE_JUMPED,  /* jumped, no air dodge yet */
    PHASE_DODGED,  /* air dodging */
    PHASE_LANDED,  /* in the waveland's landing lag */
};

enum { RESULT_DASH = 1, RESULT_NO_STAGE };

/* An attempt that takes longer than this from the drop isn't a ledge dash */
#define GIVE_UP 60

typedef struct Attempt {
    int phase;
    int grab, drop, jump, dodge; /* ticks each happened on */
    int jumps;                   /* jumps used when the fighter let go */
    int gone;                    /* tick the intangibility ran out, 0 while it lasts */
} Attempt;

typedef struct Result {
    int seq;    /* results so far */
    int frame;  /* tgg_frame() it was decided on */
    int kind;   /* RESULT_* */
    int ledge;  /* frames on the ledge */
    int to_jump;  /* drop to jump, -1 with no jump */
    int to_dodge; /* jump (or drop) to air dodge */
    int left;   /* intangible frames left once actionable; below 0, frames short */
} Result;

static struct {
    int tick;
    Attempt at[4];
    Result res[4];
} st;
TGG_STATE(st);

static int showing(void);
static void describe(char* line1, char* line2, size_t cap, const Result* r);

static void on_match_start(const StartMeleeData* data)
{
    (void) data;
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

static void decide(int port, Result r)
{
    r.seq = st.res[port].seq + 1;
    r.frame = tgg_frame();
    st.res[port] = r;
    st.at[port].phase = PHASE_NONE;
    if (showing()) {
        char a[48], b[64];
        describe(a, b, sizeof b, &r);
        tgg_log("P%d: %s%s%s", port + 1, a, b[0] != '\0' ? ", " : "", b); /* dropped in re-runs */
    }
}

static int on_ledge(int msid)
{
    return msid == ftCo_MS_CliffCatch || msid == ftCo_MS_CliffWait;
}

/* The ledge's own options: getting up, rolling, attacking or jumping from it */
static int ledge_option(int msid)
{
    return msid >= ftCo_MS_CliffClimbSlow && msid <= ftCo_MS_CliffJumpQuick2;
}

static void on_fighter_update(Fighter_GObj* gobj)
{
    const Fighter* fp = GET_FIGHTER(gobj);
    const int port = fp->player_idx;
    if (port < 0 || port >= 4 || fp->is_sub_fighter) {
        return;
    }
    Attempt* a = &st.at[port];
    const int msid = fp->motion_id;
    if (on_ledge(msid)) {
        if (a->phase != PHASE_LEDGE) {
            memset(a, 0, sizeof *a);
            a->phase = PHASE_LEDGE;
            a->grab = st.tick;
        }
        return;
    }
    if (a->phase == PHASE_NONE) {
        return;
    }
    if (fp->x1990 == 0 && a->gone == 0) {
        a->gone = st.tick;
    }
    if (a->phase != PHASE_LEDGE && a->phase != PHASE_LANDED && st.tick - a->drop > GIVE_UP) {
        a->phase = PHASE_NONE;
        return;
    }
    switch (a->phase) {
    case PHASE_LEDGE:
        if (ledge_option(msid)) {
            a->phase = PHASE_NONE;
        } else {
            a->phase = PHASE_DROPPED;
            a->drop = st.tick;
            a->jumps = fp->x1968_jumpsUsed;
        }
        break;
    case PHASE_DROPPED:
    case PHASE_JUMPED:
        if (a->phase == PHASE_DROPPED && fp->x1968_jumpsUsed > a->jumps) {
            a->phase = PHASE_JUMPED;
            a->jump = st.tick;
        }
        if (msid == ftCo_MS_EscapeAir) {
            a->phase = PHASE_DODGED;
            a->dodge = st.tick;
        } else if (fp->ground_or_air == GA_Ground) {
            a->phase = PHASE_NONE; /* landed some other way */
        }
        break;
    case PHASE_DODGED:
        if (msid == ftCo_MS_LandingFallSpecial) {
            a->phase = PHASE_LANDED;
        } else if (msid != ftCo_MS_EscapeAir) {
            decide(port, (Result) { .kind = RESULT_NO_STAGE });
        }
        break;
    case PHASE_LANDED:
        if (msid != ftCo_MS_LandingFallSpecial) {
            /* the first frame the fighter can act */
            Result r = { .kind = RESULT_DASH };
            r.ledge = a->drop - a->grab;
            r.to_jump = a->jump != 0 ? a->jump - a->drop : -1;
            r.to_dodge = a->dodge - (a->jump != 0 ? a->jump : a->drop);
            r.left = fp->x1990 > 0 ? fp->x1990 : -(st.tick - a->gone);
            decide(port, r);
        }
        break;
    }
}
TGG_ON(fighter_update, on_fighter_update);

/* ---- What the player sees ---------------------------------------------- */

#define SHOW 150
#define FADE 20

/* The HUD camera to the screen (measured, as in tgg.lcancel-trainer) */
#define HUD_CENTER_X 355.0f
#define HUD_SCALE_X 10.8f
#define HUD_SCALE_Y 10.0f
#define HUD_ABOVE 95.0f

static struct {
    int in_match;
    int seen[4];
    int age[4];
    Result shown[4];
} view;

static int showing(void)
{
    return tgg_in_match() &&
           (where == WHERE_EVERY_MATCH || gm_GetCurrentGameMode() == GM_TRAINING);
}

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
        const Result* r = &st.res[p];
        if (r->seq < view.seen[p]) {
            view.seen[p] = r->seq;
        }
        if (r->seq > view.seen[p] && (final == INT_MAX || r->frame <= final)) {
            view.seen[p] = r->seq;
            view.shown[p] = *r;
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

/* x is the center of the player's percent, y the bottom of its HUD, in the
 * 640 by 480 screen; 0 when the port has no HUD */
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

static void describe(char* line1, char* line2, size_t cap, const Result* r)
{
    line2[0] = '\0';
    if (r->kind == RESULT_NO_STAGE) {
        snprintf(line1, cap, "Air dodge missed the stage");
        return;
    }
    if (r->left > 0) {
        snprintf(line1, cap, "Intangible: %d frame%s left", r->left, r->left == 1 ? "" : "s");
    } else if (r->left == 0) {
        snprintf(line1, cap, "No intangibility left");
    } else {
        snprintf(line1, cap, "Too slow by %d frame%s", -r->left, r->left == -1 ? "" : "s");
    }
    if (r->to_jump >= 0) {
        snprintf(line2, cap, "Ledge %d   Jump %d   Air dodge %d", r->ledge, r->to_jump,
                 r->to_dodge);
    } else {
        snprintf(line2, cap, "Ledge %d   Air dodge %d", r->ledge, r->to_dodge);
    }
}

static void on_hud(void)
{
    if (!showing()) {
        return;
    }
    const tgg_rect bounds = tgg_draw_bounds();
    for (int p = 0; p < 4; p++) {
        if (view.age[p] >= SHOW || Player_GetPlayerSlotType(p) != Gm_PKind_Human) {
            continue;
        }
        float x, y;
        if (!hud_anchor(p, &x, &y)) {
            continue;
        }
        const Result* r = &view.shown[p];
        const int left = SHOW - view.age[p];
        const float a = left < FADE ? (float) left / FADE : 1.0f;
        char line1[48], line2[64];
        describe(line1, line2, sizeof line2, r);
        const float w1 = tgg_text_width(20, line1), w2 = tgg_text_width(16, line2);
        const float w = 20 + (w1 > w2 ? w1 : w2);
        const float h = line2[0] != '\0' ? 46 : 28;
        /* centered on the percent, kept inside the window */
        float cx = x;
        if (cx - w / 2 < bounds.x + 4) {
            cx = bounds.x + 4 + w / 2;
        }
        if (cx + w / 2 > bounds.x + bounds.w - 4) {
            cx = bounds.x + bounds.w - 4 - w / 2;
        }
        const float top = y - HUD_ABOVE - h;
        tgg_draw_rect(cx - w / 2, top, w, h, TGG_RGBA(0, 0, 0, 150 * a));
        const int good = r->kind == RESULT_DASH && r->left > 0;
        tgg_draw_text(cx, top + 4, 20,
                      good ? TGG_RGBA(130, 235, 130, 255 * a) : TGG_RGBA(255, 120, 100, 255 * a),
                      TGG_ALIGN_CENTER, line1);
        if (line2[0] != '\0') {
            tgg_draw_text(cx, top + 26, 16, TGG_RGBA(230, 230, 240, 255 * a), TGG_ALIGN_CENTER,
                          line2);
        }
    }
}
TGG_ON(hud_draw, on_hud);
