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

#include "tgg/game/fighters.h"
#include "tgg/game/hooks.h"
#include "tgg/draw.h"
#include "tgg/events.h"
#include "tgg/fighter.h"
#include "tgg/input.h"
#include "tgg/settings.h"
#include "tgg/snapshot.h"

#include <melee/cm/camera.h>
#include <melee/cm/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftYoshi/forward.h>
#include <melee/ft/types.h>
#include <melee/gm/forward.h>
#include <melee/gm/gm_1884.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/mn/mnmain.h>
#include <melee/mn/mnstagesel.h>
#include <melee/mp/forward.h>
#include <melee/mp/mplib.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/sislib.h>

#include <stddef.h>
#include <string.h>

static int setting_alone;
TGG_SETTING_BOOL(setting_alone, "alone", "Empty stage in Training", 0);
static int setting_clean;
TGG_SETTING_BOOL(setting_clean, "clean", "Clean screen in Training", 0);

/* The drill Training Mode was started for, from practice_start until the
 * game leaves Training Mode */
static struct {
    int pending; /* asked for, Training Mode not reached yet */
    int on;
    char drill[65];
    char stage[65];
    practice_how how;
} drill;

static int pending_ledge;   /* -1 left, +1 right, 0 none */
static int snap_camera;     /* nonzero: snap camera to target on tick_end */
static int cpu_shield;      /* nonzero: keep P2 holding infinite full shield */
static int cpu_pin;         /* nonzero: anchor P2 horizontally (zero pushback) */
static int cpu_pin_have[2]; /* nonzero: cpu_pin_x[sub] is captured */
static float cpu_pin_x[2];  /* anchored X for main (0) and follower/Nana (1) */
static Camera* g_cam;
TGG_GAME_SYMBOL(g_cam, "camera.c:game_camera");

static int is_guarding(const Fighter* fp)
{
    if (fp->ground_or_air != GA_Ground) {
        return 0;
    }
    switch (fp->motion_id) {
    case ftCo_MS_GuardOn:
    case ftCo_MS_Guard:
    case ftCo_MS_GuardSetOff:
    case ftCo_MS_GuardReflect:
        return 1;
    default:
        break;
    }
    if (FT_CODE(fp->kind) == Ft_Kind_Yoshi) {
        switch (fp->motion_id) {
        case ftYs_MS_GuardOn_0:
        case ftYs_MS_GuardHold:
        case ftYs_MS_GuardDamage:
        case ftYs_MS_GuardOn_1:
            return 1;
        default:
            break;
        }
    }
    return 0;
}

static void apply_cpu_position(Fighter_GObj* gobj, int sub)
{
    Fighter* fp = gobj->user_data;
    if (!is_guarding(fp)) {
        cpu_pin_have[sub] = 0;
        return;
    }
    if (cpu_pin) {
        if (!cpu_pin_have[sub]) {
            cpu_pin_x[sub] = fp->cur_pos.x;
            cpu_pin_have[sub] = 1;
        }
        fp->gr_vel = 0.0f;
        fp->self_vel.x = 0.0f;
        fp->cur_pos.x = cpu_pin_x[sub];
        fp->coll_data.cur_pos.x = cpu_pin_x[sub];
        fp->coll_data.prev_pos.x = cpu_pin_x[sub];
        HSD_JObj* jobj = HSD_GObjGetHSDObj(gobj);
        if (jobj != NULL) {
            HSD_JObjSetTranslate(jobj, &fp->cur_pos);
        }
        return;
    }
    const int fid = fp->coll_data.floor.index;
    if (fid == -1) {
        return;
    }
    Vec3 left, right;
    mpFloorGetLeft(fid, &left);
    mpFloorGetRight(fid, &right);
    const float span = right.x - left.x;
    const float margin = span > 8.0f ? 3.5f : span * 0.25f;
    const float min_x = left.x + margin;
    const float max_x = right.x - margin;
    int clamped = 0;
    if (fp->cur_pos.x < min_x) {
        fp->cur_pos.x = min_x;
        if (fp->gr_vel < 0.0f) {
            fp->gr_vel = 0.0f;
        }
        if (fp->self_vel.x < 0.0f) {
            fp->self_vel.x = 0.0f;
        }
        clamped = 1;
    } else if (fp->cur_pos.x > max_x) {
        fp->cur_pos.x = max_x;
        if (fp->gr_vel > 0.0f) {
            fp->gr_vel = 0.0f;
        }
        if (fp->self_vel.x > 0.0f) {
            fp->self_vel.x = 0.0f;
        }
        clamped = 1;
    }
    if (clamped) {
        fp->coll_data.cur_pos.x = fp->cur_pos.x;
        fp->coll_data.prev_pos.x = fp->cur_pos.x;
        HSD_JObj* jobj = HSD_GObjGetHSDObj(gobj);
        if (jobj != NULL) {
            HSD_JObjSetTranslate(jobj, &fp->cur_pos);
        }
    }
}

static int in_training(void)
{
    return gm_GetCurrentGameMode() == GM_TRAINING;
}

static int want_alone(void)
{
    if (cpu_shield) {
        return 0;
    }
    return drill.on ? !drill.how.keep_cpu : setting_alone;
}

static int want_clean(void)
{
    return drill.on ? !drill.how.keep_screen : setting_clean;
}

/* ---- The match --------------------------------------------------------------- */

/* Simulation code reads want_alone(), want_clean(), cpu_shield and pending_ledge. */
static void on_tick(void)
{
    if (!tgg_in_match() || !in_training()) {
        return;
    }
    if (cpu_shield) {
        for (int p = 2; p < 4; p++) {
            if (Player_GetEntity(p) != NULL) {
                fn_8016EF98(p);
            }
        }
        for (int sub = 0; sub < 2; sub++) {
            Fighter_GObj* p2 = Player_GetEntityAtIndex(1, sub);
            if (p2 != NULL) {
                Fighter* fp = p2->user_data;
                fp->shield_health = p_ftCommonData->x260_startShieldHealth;
                fp->dmg.x1830_percent = 0.0f;
                Player_SetHPByIndex(fp->player_idx, fp->is_sub_fighter, 0);
                apply_cpu_position(p2, sub);
            } else {
                cpu_pin_have[sub] = 0;
            }
        }
    } else if (want_alone()) {
        for (int p = 1; p < 4; p++) {
            if (Player_GetEntity(p) != NULL) {
                fn_8016EF98(p);
            }
        }
    }
    if (pending_ledge != 0) {
        Fighter_GObj* p1 = Player_GetEntity(0);
        if (p1 != NULL) {
            const int side = pending_ledge;
            pending_ledge = 0;
            Vec3 ledge_pos;
            const int ledge_id = mpLib_80051BA8_Floor(&ledge_pos, -1, -1, -1, side < 0 ? 1 : -1,
                                                      -400.0f, -200.0f, 400.0f, 200.0f);
            if (ledge_id != -1) {
                Fighter* fp = p1->user_data;
                fp->x2064_ledgeCooldown = 0;
                fp->x1968_jumpsUsed = 0;
                fp->self_vel.x = 0.0f;
                fp->self_vel.y = 0.0f;
                fp->self_vel.z = 0.0f;
                fp->gr_vel = 0.0f;
                fp->cur_pos = ledge_pos;
                fp->coll_data.env_flags = side < 0 ? Collide_LeftLedgeGrab : Collide_RightLedgeGrab;
                if (side < 0) {
                    fp->coll_data.ledge_id_left = ledge_id;
                } else {
                    fp->coll_data.ledge_id_right = ledge_id;
                }
                ftCliffCommon_80081370(p1);
                fp->coll_data.cur_pos = fp->cur_pos;
                fp->coll_data.prev_pos = fp->cur_pos;
                HSD_JObj* jobj = HSD_GObjGetHSDObj(p1);
                if (jobj != NULL) {
                    HSD_JObjSetTranslate(jobj, &fp->cur_pos);
                }
                if (fp->x890_cameraBox != NULL) {
                    fp->x890_cameraBox->pos = fp->cur_pos;
                    fp->x890_cameraBox->bone_pos = fp->cur_pos;
                    fp->x890_cameraBox->facing_dir = fp->facing_dir;
                }
                snap_camera = 1;
            }
        }
    }
}
TGG_ON(tick, on_tick);

static void on_fighter_input(tgg_fighter fighter, tgg_input* in)
{
    if (!tgg_in_match() || !in_training() || !cpu_shield || tgg_fighter_port(fighter) != 1) {
        return;
    }
    /* When repositioning Ice Climbers (e.g. grabbing/throwing Popo), let Nana
     * follow Popo until Popo is grounded and guarding again. */
    if (tgg_fighter_follower(fighter)) {
        Fighter_GObj* popo = Player_GetEntityAtIndex(1, 0);
        if (popo != NULL && !is_guarding(popo->user_data)) {
            return;
        }
    }
    in->stick_x = 0.0f;
    in->stick_y = 0.0f;
    in->cstick_x = 0.0f;
    in->cstick_y = 0.0f;
    in->buttons = TGG_BUTTON_L;
    in->trigger = 1.0f;
}
TGG_ON(fighter_input, on_fighter_input);

static void on_tick_end(void)
{
    if (!tgg_in_match() || !in_training()) {
        return;
    }
    if (cpu_shield) {
        for (int sub = 0; sub < 2; sub++) {
            Fighter_GObj* p2 = Player_GetEntityAtIndex(1, sub);
            if (p2 != NULL) {
                apply_cpu_position(p2, sub);
            }
        }
    }
    if (snap_camera && g_cam != NULL) {
        snap_camera = 0;
        g_cam->transform.interest = g_cam->transform.target_interest;
        g_cam->transform.position = g_cam->transform.target_position;
        g_cam->transform.fov = g_cam->transform.target_fov;
        if (g_cam->gobj != NULL) {
            HSD_CObj* cobj = HSD_GObjGetHSDObj(g_cam->gobj);
            if (cobj != NULL) {
                Camera_8002AF68(cobj, &g_cam->transform);
            }
        }
    }
    if (!want_clean()) {
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
    pending_ledge = 0;
    cpu_shield = 0;
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
static char message_buf[64];
static const char* message;
static int message_age = SHOW;
static int was_in_match;

void practice_say(const char* text)
{
    if (text == NULL || text[0] == '\0') {
        message_age = SHOW;
        return;
    }
    strncpy(message_buf, text, sizeof message_buf - 1);
    message_buf[sizeof message_buf - 1] = '\0';
    message = message_buf;
    message_age = 0;
}

void practice_keep_reset(int say)
{
    if (!tgg_in_match()) {
        return;
    }
    tgg_snapshot_free(reset_point);
    reset_point = tgg_snapshot_save();
    if (reset_point != NULL) {
        tgg_log("reset point kept");
        if (say) {
            practice_say("Ready");
        }
    }
}

void practice_reset_here(void)
{
    practice_keep_reset(1);
}

int practice_reset(int say)
{
    if (reset_point == NULL || tgg_snapshot_load(reset_point) != TGG_SNAPSHOT_OK) {
        return 0;
    }
    cpu_pin_have[0] = 0;
    cpu_pin_have[1] = 0;
    if (say) {
        practice_say("Reset");
    }
    return 1;
}

int practice_has_reset(void)
{
    return reset_point != NULL;
}

int practice_put_on_ledge(int side)
{
    if (!tgg_in_match() || !in_training()) {
        return 0;
    }
    pending_ledge = side < 0 ? -1 : 1;
    return 1;
}

void practice_set_cpu_shield(int on)
{
    cpu_shield = on != 0;
    cpu_pin_have[0] = 0;
    cpu_pin_have[1] = 0;
    if (drill.on) {
        drill.how.keep_cpu = cpu_shield;
    }
}

void practice_set_cpu_pin(int on)
{
    cpu_pin = on != 0;
    cpu_pin_have[0] = 0;
    cpu_pin_have[1] = 0;
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
        pending_ledge = 0;
        cpu_shield = 0;
        cpu_pin = 0;
        cpu_pin_have[0] = 0;
        cpu_pin_have[1] = 0;
    }
    const int in_match = tgg_in_match();
    if (!in_match && was_in_match) {
        tgg_snapshot_free(reset_point);
        reset_point = NULL;
        pending_ledge = 0;
        cpu_shield = 0;
        cpu_pin = 0;
        cpu_pin_have[0] = 0;
        cpu_pin_have[1] = 0;
    }
    was_in_match = in_match;
}
TGG_ON(frame, on_frame);

static void draw_toast_pill(float x, float y, float w, float h, tgg_color fill, tgg_color border,
                            float border_w)
{
    static const float c[4] = { 1.0f, 0.8660254f, 0.5f, 0.0f };
    static const float s[4] = { 0.0f, 0.5f, 0.8660254f, 1.0f };
    const float r = (w < h ? w : h) * 0.5f;
    float pts[32];
    int n = 0;
    for (int i = 0; i < 4; i++) {
        pts[n++] = (x + w - r) + r * s[i];
        pts[n++] = (y + r) - r * c[i];
    }
    for (int i = 0; i < 4; i++) {
        pts[n++] = (x + w - r) + r * c[i];
        pts[n++] = (y + h - r) + r * s[i];
    }
    for (int i = 0; i < 4; i++) {
        pts[n++] = (x + r) - r * s[i];
        pts[n++] = (y + h - r) + r * c[i];
    }
    for (int i = 0; i < 4; i++) {
        pts[n++] = (x + r) - r * c[i];
        pts[n++] = (y + r) - r * s[i];
    }
    tgg_draw_polygon(pts, 16, fill);
    if (border.a > 0 && border_w > 0.0f) {
        tgg_draw_polygon_outline(pts, 16, border_w, border);
    }
}

static void on_hud(void)
{
    if (message_age >= SHOW || !tgg_in_match()) {
        return;
    }
    const int left = SHOW - message_age;
    const float a = left < FADE ? (float) left / FADE : 1.0f;
    const float sz = 17.0f;
    const float tw = tgg_draw_text_width(sz, message);
    const float h = 30.0f;
    const float w = 14.0f + 8.0f + 8.0f + tw + 16.0f;
    const float x = 320.0f - w * 0.5f;
    const float y = 66.0f;
    const float cy = y + h * 0.5f;
    draw_toast_pill(x, y + 2.0f, w, h, TGG_RGBA(0, 0, 0, (int) (115.0f * a)),
                    TGG_RGBA(0, 0, 0, 0), 0.0f);
    draw_toast_pill(x, y, w, h, TGG_RGBA(12, 17, 42, (int) (230.0f * a)),
                    TGG_RGBA(154, 182, 255, (int) (180.0f * a)), 1.2f);
    tgg_draw_circle(x + 17.0f, cy, 4.0f, TGG_RGBA(108, 148, 255, (int) (255.0f * a)));
    tgg_draw_text(x + 28.0f, cy - sz * 0.52f, sz, TGG_RGBA(244, 247, 255, (int) (255.0f * a)),
                  TGG_ALIGN_LEFT, message);
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
        if (stage_icons[i].stkind == tgg_content_stage_number(drill.how.stage) && stage_icons[i].shown &&
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
