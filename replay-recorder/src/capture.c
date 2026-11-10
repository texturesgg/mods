/* The simulation's side of the recorder: each logic frame of a match being
 * recorded, the events Slippi's recording code sends, captured at the same
 * points in the game and encoded as the console would send them.
 *
 * - tick: the frame starts. The RNG seed and the scene's frame counter
 *   (Frame Start), or the match's end, which Slippi sends from the VS
 *   scene's think, before the frame's objects run, and after which it
 *   records nothing.
 * - fighter_input: a fighter's input was read and processed, before it acts
 *   on it (Pre-Frame Update). Slippi reads it at 0x8006B0E0, in
 *   Fighter_procInput just after the same values are set.
 * - after Fighter_procCamera: the fighter's frame is done (Post-Frame Update,
 *   0x8006DA34). The L-cancel status is reset before Fighter_procMap and set
 *   in ftCo_LandingAir_EnterWithLag, as Slippi's own two hooks do.
 * - after the Fountain of Dreams, Pokemon Stadium and Dream Land updates:
 *   the stage events, where Slippi sends them.
 * - before the first Fighter_procDynamics: the items, which Slippi reads in
 *   an object process of its own between the fighters' collision and
 *   dynamics processes.
 * - tick_end: the frame is kept by its Slippi frame number. A frame the match was
 *   paused through runs no fighters and is dropped: a paused console sends
 *   nothing, and the frame that unpauses is recorded under the same number.
 *
 * A frame run again after a rollback is captured again and replaces the one
 * kept. mod.c writes a frame once tgg_frame_final says it is final.
 */
#include "recorder.h"

#include "tgg/events.h"
#include "tgg/rollback.h"

#include <melee/ft/fighter.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_LandingAir.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/types.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/types.h>
#include <melee/gr/grizumi.h>
#include <melee/gr/groldpupupu.h>
#include <melee/gr/grpstadium.h>
#include <melee/gr/types.h>
#include <melee/it/forward.h>
#include <melee/it/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/random.h>

#include <limits.h>
#include <stddef.h>
#include <string.h>

_Static_assert(offsetof(MatchPlayerData, is_small_loser) == 6,
               "Slippi reads the placement at byte 6 of each player's standing");

/* Frames kept for writing: more than rollback can hold back */
#define RING 64

/* The scene's frame counter (gmscene.c), which Slippi's Frame Start reads */
static struct gm_80479D58_t* scene;
TGG_GAME_SYMBOL(scene, "gm_80479D58");

/* What the simulation counts from frame to frame */
static struct {
    int on;    /* recording this match */
    int frame; /* the Slippi frame being captured */
    int ended; /* the match ended: nothing more is recorded */
    RecEnd end;
    uint8_t lcancel[4][2]; /* by port and follower: 1 L-cancelled, 2 missed */
    uint32_t stadium;      /* the transformation last sent */
    int32_t whispy;        /* the direction last sent */
} st;
TGG_STATE(st);

/* The frame being captured, from tick to tick_end; only the simulation
 * reads it, within the frame */
static RecFrame cur;
static int in_frame;
static int items_sent; /* this frame's items are in it */
static void send_items(void);

/* The frames captured, by Slippi frame; mod.c reads them */
static RecFrame ring[RING];
/* For each match frame, the Slippi frames captured up to its end */
static struct {
    int match_frame;
    int count;
} done[RING];

static uint8_t* put(int len)
{
    if (cur.len + len > REC_FRAME_BYTES) {
        return NULL;
    }
    uint8_t* p = cur.bytes + cur.len;
    memset(p, 0, (size_t) len);
    cur.len += len;
    return p;
}

static void be16(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 8);
    p[1] = (uint8_t) v;
}

static void be32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

static void bef(uint8_t* p, float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof bits);
    be32(p, bits);
}

/* An event's command and frame number */
static uint8_t* event(uint8_t cmd, int size)
{
    uint8_t* p = put(1 + size);
    if (p != NULL) {
        p[0] = cmd;
        be32(p + 1, (uint32_t) st.frame);
    }
    return p;
}

static int capturing(void)
{
    return st.on && !st.ended && in_frame;
}

void capture_start(void)
{
    memset(&st, 0, sizeof st);
    st.on = 1;
    st.frame = REC_FIRST_FRAME;
    st.stadium = 5;
    for (int i = 0; i < RING; i++) {
        ring[i].index = INT_MIN;
        done[i].match_frame = INT_MIN;
    }
}

void capture_stop(void)
{
    st.on = 0;
}

/* The match's end as Slippi sends it: how, who quit, and each port's
 * placement, which the game works out as it does for the results screen
 * (gm_80166378), into a copy of its own */
static void match_ended(const VsSceneController* c)
{
    static MatchEnd standings;
    standings = c->state.x24C;
    standings.is_teams = c->start.is_teams;
    standings.outcome = c->state.match_result;
    gm_80166378(&standings);
    st.end.known = 1;
    st.end.method = c->state.match_result;
    st.end.lras = c->state.match_result == 7 ? c->state.pauser : -1;
    for (int i = 0; i < 4; i++) {
        const MatchPlayerData* p = &standings.player_standings[i];
        st.end.placements[i] = p->pkind == Gm_PKind_NA ? -1 : (int8_t) p->is_small_loser;
    }
}

static void on_tick(void)
{
    in_frame = 0;
    if (!st.on || st.ended || !tgg_in_match()) {
        return;
    }
    const VsSceneController* c = gmVs_GetSceneController();
    if (c->state.match_result != 0) {
        st.ended = 1;
        match_ended(c);
        return;
    }
    in_frame = 1;
    items_sent = 0;
    cur.len = 0;
    uint8_t* p = event(CMD_FRAME_START, REC_FRAME_START_SIZE);
    be32(p + 5, *HSD_RandSeedPtr);
    be32(p + 9, scene->unk_8);
}
TGG_ON(tick, on_tick);

/* The controller report the game read this frame for `port`: where UCF
 * reads the raw sticks */
static const PADStatus* report(int port)
{
    const PadLibData* lib = &HSD_PadLibData;
    const int newest = lib->qread == 0 ? lib->qnum - 1 : lib->qread - 1;
    return &lib->queue[newest].stat[port];
}

static void on_input(Fighter_GObj* gobj, tgg_input* in)
{
    (void) in;
    if (!capturing()) {
        return;
    }
    const Fighter* fp = GET_FIGHTER(gobj);
    const int port = fp->player_idx;
    if (port > 3) {
        return;
    }
    uint8_t* p = event(CMD_PRE, REC_PRE_SIZE);
    if (p == NULL) {
        return;
    }
    const PADStatus* raw = report(port);
    const HSD_PadStatus* pad = &HSD_PadMasterStatus[port];
    p[5] = (uint8_t) port;
    p[6] = fp->is_sub_fighter;
    be32(p + 0x7, *HSD_RandSeedPtr);
    be16(p + 0xB, (uint32_t) fp->motion_id);
    bef(p + 0xD, fp->cur_pos.x);
    bef(p + 0x11, fp->cur_pos.y);
    bef(p + 0x15, fp->facing_dir);
    bef(p + 0x19, fp->input.lstick[0].x);
    bef(p + 0x1D, fp->input.lstick[0].y);
    bef(p + 0x21, fp->input.cstick[0].x);
    bef(p + 0x25, fp->input.cstick[0].y);
    bef(p + 0x29, fp->input.triggers[0]);
    be32(p + 0x2D, fp->input.held_buttons[0]);
    be16(p + 0x31, raw->button);
    bef(p + 0x33, pad->nml_analogL);
    bef(p + 0x37, pad->nml_analogR);
    p[0x3B] = (uint8_t) raw->stickX;
    bef(p + 0x3C, fp->dmg.x1830_percent);
    p[0x40] = (uint8_t) raw->stickY;
    p[0x41] = (uint8_t) raw->substickX;
    p[0x42] = (uint8_t) raw->substickY;
}
TGG_ON(fighter_input, on_input);

/* Eight one-bit fields into a byte as the console lays them out, the first
 * the most significant */
#define BYTE8(a, b, c, d, e, f, g, h)                                          \
    (uint8_t) ((a) << 7 | (b) << 6 | (c) << 5 | (d) << 4 | (e) << 3 | (f) << 2 | (g) << 1 | (h))

static void on_camera(Fighter_GObj* gobj)
{
    if (!capturing()) {
        return;
    }
    send_items();
    const Fighter* fp = GET_FIGHTER(gobj);
    const int port = fp->player_idx;
    if (port > 3 || fp->is_sleeping) {
        return;
    }
    uint8_t* p = event(CMD_POST, REC_POST_SIZE);
    if (p == NULL) {
        return;
    }
    p[5] = (uint8_t) port;
    p[6] = fp->is_sub_fighter;
    p[7] = (uint8_t) fp->kind;
    be16(p + 0x8, (uint32_t) fp->motion_id);
    bef(p + 0xA, fp->cur_pos.x);
    bef(p + 0xE, fp->cur_pos.y);
    bef(p + 0x12, fp->facing_dir);
    bef(p + 0x16, fp->dmg.x1830_percent);
    bef(p + 0x1A, fp->shield_health);
    p[0x1E] = (uint8_t) fp->x208C;
    p[0x1F] = (uint8_t) fp->x2090;
    p[0x20] = (uint8_t) fp->dmg.x18c4_source_ply;
    p[0x21] = (uint8_t) Player_GetStocks(port);
    bef(p + 0x22, fp->cur_anim_frame);
    p[0x26] = BYTE8(fp->allow_interrupt, fp->x2218_b1, fp->x2218_b2, fp->reflecting,
                    fp->x2218_b4, fp->x2218_b5, fp->x2218_b6, fp->x2218_b7);
    p[0x27] = BYTE8(fp->x221A_b0, fp->x221A_b1, fp->allow_sdi, fp->x221A_b3, fp->fall_fast,
                    fp->x221A_b5, fp->x221A_b6, fp->x221A_b7);
    p[0x28] = BYTE8(fp->x221B.x221B_b0, fp->x221B.x221B_b1, fp->x221B.x221B_b2,
                    fp->x221B.x221B_b3, fp->x221B.x221B_b4, fp->x221B.x221B_b5,
                    fp->x221B.x221B_b6, fp->x221B.x221B_b7);
    /* 0x221C's last bit is the first of a three-bit field */
    p[0x29] = BYTE8(fp->x221C_b0, fp->x221C_b1, fp->x221C_b2, fp->x221C_b3, fp->x221C_b4,
                    fp->x221C_b5, fp->x221C_b6, (fp->x221C_u16_y >> 2) & 1);
    p[0x2A] = BYTE8(fp->x221F_b0, fp->x221F_b1, fp->x221F_b2, fp->is_sleeping,
                    fp->is_sub_fighter, fp->x221F_b5, fp->x221F_b6, fp->x221F_b7);
    /* The first word of the action state's variables: hitstun left, while
     * in hitstun */
    uint32_t misc;
    memcpy(&misc, &fp->mv, sizeof misc);
    be32(p + 0x2B, misc);
    p[0x2F] = (uint8_t) fp->ground_or_air;
    be16(p + 0x30, (uint32_t) fp->coll_data.floor.index);
    p[0x32] = (uint8_t) (fp->co_attrs.max_jumps - fp->x1968_jumpsUsed);
    p[0x33] = st.lcancel[port][fp->is_sub_fighter];
    p[0x34] = (uint8_t) (fp->x1988 != 0 ? fp->x1988 : fp->x198C);
    bef(p + 0x35, fp->self_vel.x);
    bef(p + 0x39, fp->self_vel.y);
    bef(p + 0x3D, fp->x8c_kb_vel.x);
    bef(p + 0x41, fp->x8c_kb_vel.y);
    bef(p + 0x45, fp->gr_vel);
    bef(p + 0x49, fp->dmg.x195c_hitlag_frames);
    be32(p + 0x4D, (uint32_t) fp->anim_id);
    be16(p + 0x51, fp->dmg.x18ec_instancehitby);
    be16(p + 0x53, fp->x2074.x2088);
}
TGG_AFTER(Fighter_procCamera, on_camera);

/* Slippi's L-cancel status lasts from the fighter's collision step, where
 * it's cleared, to the next one */
static void on_map(Fighter_GObj* gobj)
{
    const Fighter* fp = GET_FIGHTER(gobj);
    if (st.on && fp->player_idx <= 3 && !fp->is_sleeping) {
        st.lcancel[fp->player_idx][fp->is_sub_fighter] = 0;
    }
}
TGG_BEFORE(Fighter_procMap, on_map);

/* Landing from an aerial: L-cancelled when the L, R or Z press was recent
 * enough, as the function itself decides */
static void on_landing(Fighter_GObj* gobj)
{
    const Fighter* fp = GET_FIGHTER(gobj);
    if (!st.on || fp->player_idx > 3 || !fp->cmd_vars[0]) {
        return;
    }
    switch (fp->motion_id) {
    case ftCo_MS_AttackAirN:
    case ftCo_MS_AttackAirF:
    case ftCo_MS_AttackAirB:
    case ftCo_MS_AttackAirHi:
    case ftCo_MS_AttackAirLw:
        st.lcancel[fp->player_idx][fp->is_sub_fighter] =
            fp->x67F < p_ftCommonData->xE4 ? 1 : 2;
        break;
    default:
        break;
    }
}
TGG_BEFORE(ftCo_LandingAir_EnterWithLag, on_landing);

/* Fountain of Dreams: a platform's height, by the side its joint is on.
 * Slippi sends it where the update sets the platform's height, which it
 * does when it was still (state 0) or moving (2), or starts moving again
 * after a dip (4 to 2). */
static int fountain_state; /* the platform's state before the update */

static void before_fountain(Ground_GObj* gobj)
{
    fountain_state = ((const Ground*) gobj->user_data)->u.izumi3.xC4;
}
TGG_BEFORE(grIzumi_801CC358, before_fountain);

static void on_fountain(Ground_GObj* gobj)
{
    const Ground* gp = gobj->user_data;
    const int set = fountain_state == 0 || fountain_state == 2 ||
                    (fountain_state == 4 && gp->u.izumi3.xC4 == 2);
    if (!capturing() || !set) {
        return;
    }
    uint8_t* p = event(CMD_FOD, REC_FOD_SIZE);
    if (p != NULL) {
        p[5] = HSD_JObjGetTranslationX(gobj->hsd_obj) < 0 ? 1 : 0;
        bef(p + 6, gp->u.izumi3.xD0);
    }
}
TGG_AFTER(grIzumi_801CC358, on_fountain);

/* Pokemon Stadium: the transformation's step and kind. Slippi starts from
 * step 0 of the neutral stage (kind 5) at frame -39, its first update */
static void on_stadium(Ground_GObj* gobj)
{
    if (!capturing()) {
        return;
    }
    const Ground* gp = gobj->user_data;
    const uint32_t now = (uint32_t) (uint16_t) gp->u.stadium.xDC << 16 |
                         (uint16_t) gp->u.stadium.xDE;
    if (st.frame == -39) {
        st.stadium = 5;
    }
    if (now == st.stadium) {
        return;
    }
    uint8_t* p = event(CMD_STADIUM, REC_STADIUM_SIZE);
    if (p != NULL) {
        be32(p + 5, now);
        st.stadium = now;
    }
}
TGG_AFTER(grStadium_801D4548, on_stadium);

/* Dream Land: which way Whispy blows */
static void on_whispy(Ground_GObj* gobj)
{
    if (!capturing()) {
        return;
    }
    const Ground* gp = gobj->user_data;
    const int32_t now = gp->u.oldpupupu.xDC;
    if (st.frame == REC_FIRST_FRAME) {
        st.whispy = 0;
    }
    if (now == st.whispy) {
        return;
    }
    uint8_t* p = event(CMD_WHISPY, REC_WHISPY_SIZE);
    if (p != NULL) {
        p[5] = (uint8_t) now;
        st.whispy = now;
    }
}
TGG_AFTER(grOldPupupu_802113E0, on_whispy);

/* The low byte of a word of the item's variables, at its console offset,
 * as Slippi sends it. Slippi documents four of them, for the items below,
 * whose variables hold no pointer before them, so the port lays them out as
 * the console does. For any other item the console sends whatever its
 * variables hold there (often a pointer's low byte), which means nothing
 * outside the console: those are 0. */
static uint8_t item_var(const Item* ip, int offset)
{
    switch (ip->kind) {
    case It_Kind_Samus_Missile:
    case It_Kind_Peach_Turnip:
    case It_Kind_Samus_Charge:
    case It_Kind_Kirby_SamusCharge:
    case It_Kind_Mewtwo_ShadowBall:
    case It_Kind_Kirby_MewtwoShadowBall:
        break;
    default:
        return 0;
    }
    uint32_t w;
    memcpy(&w, (const uint8_t*) &ip->xDD4_itemVar + offset, sizeof w);
    return (uint8_t) w;
}

static void capture_items(void)
{
    int n = 0;
    for (HSD_GObj* g = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_ITEM]; g != NULL && n < REC_MAX_ITEMS;
         g = g->next, n++)
    {
        const Item* ip = g->user_data;
        uint8_t* p = event(CMD_ITEM, REC_ITEM_SIZE);
        if (p == NULL) {
            return;
        }
        be16(p + 0x5, (uint32_t) ip->kind);
        p[0x7] = (uint8_t) ip->msid;
        bef(p + 0x8, ip->facing_dir);
        bef(p + 0xC, ip->x40_vel.x);
        bef(p + 0x10, ip->x40_vel.y);
        bef(p + 0x14, ip->pos.x);
        bef(p + 0x18, ip->pos.y);
        be16(p + 0x1C, (uint32_t) ip->xC9C);
        bef(p + 0x1E, ip->xD44_lifeTimer);
        be32(p + 0x22, (uint32_t) ip->x1C);
        p[0x26] = item_var(ip, 0x0);  /* Samus's missile: 1 a super missile */
        p[0x27] = item_var(ip, 0x4);  /* a turnip's face */
        p[0x28] = item_var(ip, 0x14); /* a charge shot or shadow ball launched */
        p[0x29] = item_var(ip, 0x18); /* and its charge */
        /* The owner's port: byte 0xC of its data, a fighter's player index */
        int8_t owner = -1;
        if (ip->owner != NULL && ip->owner->user_data != NULL) {
            if (ip->owner->classifier == HSD_GOBJ_CLASS_FIGHTER) {
                owner = (int8_t) ((const Fighter*) ip->owner->user_data)->player_idx;
            } else if (ip->owner->classifier == HSD_GOBJ_CLASS_ITEM) {
                owner = (int8_t) (((const Item*) ip->owner->user_data)->spawn_kind >> 24);
            }
        }
        p[0x2A] = (uint8_t) owner;
        be16(p + 0x2B, ip->xDA8_short);
    }
}

/* Slippi reads the items in an object process of its own between the
 * fighters' collision (process 14) and their dynamics (16), so before
 * the items' process 16 runs: before the first fighter's dynamics, or its
 * post-frame update, whichever comes first */
static void send_items(void)
{
    if (!items_sent) {
        items_sent = 1;
        capture_items();
    }
}

static void before_dynamics(Fighter_GObj* gobj)
{
    (void) gobj;
    if (capturing()) {
        send_items();
    }
}
TGG_BEFORE(Fighter_procDynamics, before_dynamics);

static void on_tick_end(void)
{
    if (!capturing()) {
        in_frame = 0;
        if (st.on && tgg_in_match()) {
            /* What the frames so far add up to, for the frames after the end */
            const int mf = tgg_frame();
            done[(unsigned) mf % RING].match_frame = mf;
            done[(unsigned) mf % RING].count = st.frame;
        }
        return;
    }
    in_frame = 0;
    if (!gm_GetDbPauseFlag(1)) {
        send_items();
        RecFrame* f = &ring[(unsigned) (st.frame - REC_FIRST_FRAME) % RING];
        f->index = st.frame;
        f->match_frame = tgg_frame();
        f->len = cur.len;
        memcpy(f->bytes, cur.bytes, (size_t) cur.len);
        st.frame++;
    }
    const int mf = tgg_frame();
    done[(unsigned) mf % RING].match_frame = mf;
    done[(unsigned) mf % RING].count = st.frame;
}
TGG_ON(tick_end, on_tick_end);

int capture_ready(int final)
{
    if (final == INT_MAX) {
        return st.frame;
    }
    if (final < 0 || done[(unsigned) final % RING].match_frame != final) {
        return REC_FIRST_FRAME;
    }
    return done[(unsigned) final % RING].count;
}

const RecFrame* capture_frame(int index)
{
    const RecFrame* f = &ring[(unsigned) (index - REC_FIRST_FRAME) % RING];
    return f->index == index ? f : NULL;
}

const RecEnd* capture_end(void)
{
    return &st.end;
}

int capture_count(void)
{
    return st.frame;
}
