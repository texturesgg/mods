/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Practice stage: Training Mode with nobody else on the stage and a clean
 * screen, and a reset point, for drills (include/practice_stage.h) and, on its
 * own, as two Mods-page settings for plain Training Mode.
 *
 * - Nobody else: the CPUs Training puts in are taken out with the game's own
 *   removal (fn_8016EF98, what Training's "No. of CPs" row uses), each frame
 *   one is there, so Training's menu can't bring one back.
 * - Clean screen: Training's readouts (speed, enemy, damage, combo panels) are
 *   one model, which also holds its pause menu: it's hidden while the game
 *   isn't paused, and shows again for the menu.
 * - The reset point is a snapshot (tgg/snapshot.h), kept and loaded from
 *   presentation code, with one line on screen ("Ready", "Reset") so every
 *   drill feels the same.
 *
 * An offline mod ("class": "offline"): the port switches it off for netplay.
 */
#include "practice_stage.h"
#include "tgg/content.h"

#include "tgg/game/hooks.h"
#include "tgg/draw.h"
#include "tgg/events.h"
#include "tgg/settings.h"
#include "tgg/snapshot.h"

#include <melee/gm/forward.h>
#include <melee/gm/gm_1884.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/mn/mnmain.h>
#include <melee/mn/mnstagesel.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/sislib.h>

#include <stddef.h>
#include <string.h>

static int setting_alone;
TGG_SETTING_BOOL(setting_alone, "alone", "Training: nobody else", 0);
static int setting_clean;
TGG_SETTING_BOOL(setting_clean, "clean", "Training: clean screen", 0);

/* The drill Training Mode was started for, from practice_start until the
 * game leaves Training Mode */
static struct {
    int pending; /* asked for, Training Mode not reached yet */
    int on;
    char drill[65];
    char stage[65];
    practice_how how;
} drill;

static int in_training(void)
{
    return gm_GetCurrentGameMode() == GM_TRAINING;
}

static int want_alone(void)
{
    return drill.on ? !drill.how.keep_cpu : setting_alone;
}

static int want_clean(void)
{
    return drill.on ? !drill.how.keep_screen : setting_clean;
}

/* ---- The match --------------------------------------------------------------- */

/* Simulation code reads want_alone() and want_clean(): they change only
 * between matches, from the menus. */
static void on_tick(void)
{
    if (!tgg_in_match() || !in_training() || !want_alone()) {
        return;
    }
    for (int p = 1; p < 4; p++) {
        if (Player_GetEntity(p) != NULL) {
            fn_8016EF98(p);
        }
    }
}
TGG_ON(tick, on_tick);

static void on_tick_end(void)
{
    if (!tgg_in_match() || !in_training() || !want_clean()) {
        return;
    }
    HSD_GObj* gobj = gm_80473814.gobj;
    HSD_JObj* jobj = gobj != NULL ? HSD_GObjGetHSDObj(gobj) : NULL;
    const int menu = gm_GetDbPauseFlag(2) != 0;
    if (jobj != NULL) {
        if (menu) {
            HSD_JObjClearFlagsAll(jobj, JOBJ_HIDDEN);
        } else {
            HSD_JObjSetFlagsAll(jobj, JOBJ_HIDDEN);
        }
    }
    if (gm_80473814.text != NULL) {
        gm_80473814.text->hidden = !menu;
    }
}
TGG_ON(tick_end, on_tick_end);

/* Training's menu shows "No. of CPs" as an animation frame of one joint, the
 * count plus one: with nobody else on, it shows 0 */
static void after_training_hud(HSD_GObj* gobj)
{
    (void) gobj;
    if (!in_training() || !want_alone() || gm_80473814.jobjs[32] == NULL) {
        return;
    }
    HSD_JObjReqAnimAll(gm_80473814.jobjs[32], 0.0f);
    HSD_JObjAnimAll(gm_80473814.jobjs[32]);
}
TGG_AFTER(fn_80188EE8, after_training_hud);

/* ---- The API ----------------------------------------------------------------- */

void practice_start(const practice_how* how)
{
    memset(&drill, 0, sizeof drill);
    if (how != NULL) {
        drill.how = *how;
        if (how->drill != NULL) {
            strncpy(drill.drill, how->drill, sizeof drill.drill - 1);
        }
        if (how->stage != NULL) {
            strncpy(drill.stage, how->stage, sizeof drill.stage - 1);
        }
    }
    drill.how.drill = drill.drill;
    drill.how.stage = drill.stage[0] != '\0' ? drill.stage : NULL;
    drill.pending = 1;
    tgg_log("practice: %s", drill.drill[0] != '\0' ? drill.drill : "Training");
    /* as the 1-P menu leaves for Training Mode: the menu scene's exit data
     * names the mode, and the scene ends */
    mn_80229860(GM_TRAINING);
}

/* practice_start for scripts (assets/script-api.json), in plain arguments: "" for no stage */
static void start_for_script(const char* drill_id, const char* stage, int keep_cpu, int keep_screen)
{
    const practice_how how = {
        .drill = drill_id, .keep_cpu = keep_cpu, .keep_screen = keep_screen, .stage = stage};
    practice_start(&how);
}
TGG_EXPORT(practice_start_drill, start_for_script);

const char* practice_drill(void)
{
    return drill.on && drill.drill[0] != '\0' ? drill.drill : NULL;
}

#define SHOW 50
#define FADE 12

static tgg_snapshot* reset_point;
static const char* message;
static int message_age = SHOW;
static int was_in_match;

void practice_reset_here(void)
{
    if (!tgg_in_match()) {
        return;
    }
    tgg_snapshot_free(reset_point);
    reset_point = tgg_snapshot_save();
    if (reset_point != NULL) {
        message = "Ready";
        tgg_log("reset point kept");
        message_age = 0;
    }
}

int practice_reset(int say)
{
    if (reset_point == NULL || tgg_snapshot_load(reset_point) != TGG_SNAPSHOT_OK) {
        return 0;
    }
    if (say) {
        message = "Reset";
        message_age = 0;
    }
    return 1;
}

int practice_has_reset(void)
{
    return reset_point != NULL;
}

/* ---- Presentation ------------------------------------------------------------ */

static void on_frame(void)
{
    if (message_age < SHOW) {
        message_age++;
    }
    /* the drill lasts while the game is in Training Mode */
    if (drill.pending && in_training()) {
        drill.pending = 0;
        drill.on = 1;
    } else if (drill.on && !in_training()) {
        memset(&drill, 0, sizeof drill);
    }
    const int in_match = tgg_in_match();
    if (!in_match && was_in_match) {
        tgg_snapshot_free(reset_point);
        reset_point = NULL;
    }
    was_in_match = in_match;
}
TGG_ON(frame, on_frame);

static void on_hud(void)
{
    if (message_age >= SHOW || !tgg_in_match()) {
        return;
    }
    const int left = SHOW - message_age;
    const float a = left < FADE ? (float) left / FADE : 1.0f;
    const float w = tgg_text_width(22, message) + 28;
    tgg_draw_rect(320 - w / 2, 96, w, 32, TGG_RGBA(0, 0, 0, 150 * a));
    tgg_draw_text(320, 100, 22, TGG_RGBA(255, 255, 255, 255 * a), TGG_ALIGN_CENTER, message);
}
TGG_ON(hud_draw, on_hud);

/* ---- The stage select starts on the drill's stage -------------------------- */

/* The stage select's icons (mnstagesel.c), as the game lays them out */
typedef struct StageListInfo {
    HSD_JObj* icon;
    int x4;
    u8 shown;
    u8 x9, xA;
    u8 stkind;
    f32 xC, x10, x14, x18;
} StageListInfo;
static StageListInfo* stage_icons;
TGG_GAME_SYMBOL(stage_icons, "mnstagesel.c:mnStageSel_803F06D0");

/* Cursor updates since the stage select opened; the icons slide in first,
 * so the cursor moves once they've settled, if the player hasn't moved it */
#define SETTLED 60
static int sss_frames;
static Vec3 sss_start;

static void on_match_start_placed(const tgg_match* match)
{
    (void) match;
    sss_frames = 0; /* the next stage select starts on it again */
}
TGG_ON(match_start, on_match_start_placed);

static void before_sss_cursor(HSD_GObj* gobj)
{
    if (!drill.on || drill.how.stage == NULL || stage_icons == NULL || sss_frames > SETTLED) {
        return;
    }
    HSD_JObj* cursor = HSD_GObjGetHSDObj(gobj);
    Vec3 now;
    HSD_JObjGetTranslation(cursor, &now);
    if (sss_frames++ == 0) {
        sss_start = now;
    }
    if (now.x != sss_start.x || now.y != sss_start.y) {
        sss_frames = SETTLED + 1; /* the player is choosing */
        return;
    }
    if (sss_frames < SETTLED) {
        return;
    }
    for (int i = 0; i < 30; i++) {
        if (stage_icons[i].stkind == tgg_stage_kind(drill.how.stage) && stage_icons[i].shown &&
            stage_icons[i].icon != NULL)
        {
            Vec3 at;
            lb_8000B1CC(stage_icons[i].icon, NULL, &at);
            HSD_JObjSetTranslate(cursor, &at);
            break;
        }
    }
}
TGG_BEFORE(fn_8025A310, before_sss_cursor);
