/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Taking over from a replay, and moving around in one.
 *
 * While a replay plays, a snapshot (tgg/snapshot.h) is kept about once a
 * second for the last REWIND_KEEP seconds, so the viewer can go back a couple
 * of seconds at a time.
 *
 * Taking over: the viewer picks, for each player in the replay, whether the
 * recording keeps playing them, they stand still, or one of the viewer's
 * controllers plays them from here. The match goes on from that frame as an
 * ordinary match. The takeover point is a snapshot, so the viewer can go back
 * to it and try the moment again as often as they like.
 *
 * Who plays which port lives here, outside the simulation's state: it changes
 * only while the game is held (the takeover panel pauses it), and loading the
 * takeover point has to keep it. pad_read reads it to give each port its
 * input.
 */
#include "player.h"

#include "tgg/control.h"
#include "tgg/draw.h"
#include "tgg/input.h"
#include "tgg/snapshot.h"

#include <melee/gm/gmscene.h>
#include <melee/mn/types.h>
#include <melee/pl/forward.h>

#include <stdio.h>
#include <string.h>

/* Replay frames between rewind snapshots, how many are kept, and how far
 * back one press goes */
#define REWIND_EVERY 60
#define REWIND_KEEP 30
#define REWIND_BACK 120

static struct {
    tgg_snapshot* snap;
    int frame; /* the replay frame it was taken before */
} ring[REWIND_KEEP];
static int last_kept = -1000000;

/* The players, from the replay's match */
static struct {
    int present;
    int ckind;
} players[4];

/* Takeover */
static int source[4] = { TAKE_RECORDING, TAKE_RECORDING, TAKE_RECORDING, TAKE_RECORDING };
static int taken;
static tgg_snapshot* take_point;
static int help_frames; /* displayed frames the help line has shown */

/* The panel */
static int panel;
static int panel_row;
static int panel_source[4];
static int panel_was_paused;

static const char* fighter_name(int ckind)
{
    static const char* const names[] = {
        "Captain Falcon", "Donkey Kong", "Fox",     "Mr. Game & Watch", "Kirby",
        "Bowser",         "Link",        "Luigi",   "Mario",            "Marth",
        "Mewtwo",         "Ness",        "Peach",   "Pikachu",          "Ice Climbers",
        "Jigglypuff",     "Samus",       "Yoshi",   "Zelda",            "Sheik",
        "Falco",          "Young Link",  "Dr. Mario", "Roy",            "Pichu",
        "Ganondorf",
    };
    return ckind >= 0 && ckind < (int) (sizeof names / sizeof *names) ? names[ckind] : "?";
}

static void forget_ring(void)
{
    for (int i = 0; i < REWIND_KEEP; i++) {
        tgg_snapshot_free(ring[i].snap);
        ring[i].snap = NULL;
    }
    last_kept = -1000000;
}

void takeover_reset(const StartMeleeData* match)
{
    forget_ring();
    tgg_snapshot_free(take_point);
    take_point = NULL;
    taken = 0;
    panel = 0;
    for (int p = 0; p < 4; p++) {
        source[p] = TAKE_RECORDING;
        players[p].present = match != NULL && match->players[p].slot_type != Gm_PKind_NA;
        players[p].ckind = match != NULL ? match->players[p].ckind : -1;
    }
}

void takeover_match_end(void)
{
    forget_ring();
    tgg_snapshot_free(take_point);
    take_point = NULL;
    taken = 0;
    panel = 0;
    for (int p = 0; p < 4; p++) {
        source[p] = TAKE_RECORDING;
    }
}

int takeover_active(void)
{
    return taken;
}

int takeover_source(int port)
{
    return source[port];
}

int takeover_panel_shown(void)
{
    return panel;
}

void takeover_advance(int replay_frame)
{
    if (taken || replay_frame - last_kept < REWIND_EVERY) {
        return;
    }
    /* an empty slot, else the oldest */
    int slot = -1;
    for (int i = 0; i < REWIND_KEEP && slot < 0; i++) {
        if (ring[i].snap == NULL) {
            slot = i;
        }
    }
    for (int i = 0; slot < 0 && i < REWIND_KEEP; i++) {
        int oldest = 0;
        for (int j = 1; j < REWIND_KEEP; j++) {
            if (ring[j].frame < ring[oldest].frame) {
                oldest = j;
            }
        }
        slot = oldest;
    }
    tgg_snapshot_free(ring[slot].snap);
    ring[slot].snap = tgg_snapshot_save();
    ring[slot].frame = replay_frame;
    last_kept = replay_frame;
}

int takeover_rewind(int replay_frame)
{
    if (taken) {
        return -1;
    }
    const int target = replay_frame - REWIND_BACK;
    int best = -1, oldest = -1;
    for (int i = 0; i < REWIND_KEEP; i++) {
        if (ring[i].snap == NULL) {
            continue;
        }
        if (oldest < 0 || ring[i].frame < ring[oldest].frame) {
            oldest = i;
        }
        if (ring[i].frame <= target && (best < 0 || ring[i].frame > ring[best].frame)) {
            best = i;
        }
    }
    if (best < 0) {
        best = oldest;
    }
    if (best < 0 || tgg_snapshot_load(ring[best].snap) != TGG_SNAPSHOT_OK) {
        return -1;
    }
    /* what came after it gets played (and kept) again */
    const int to = ring[best].frame;
    for (int i = 0; i < REWIND_KEEP; i++) {
        if (ring[i].snap != NULL && ring[i].frame > to) {
            tgg_snapshot_free(ring[i].snap);
            ring[i].snap = NULL;
        }
    }
    last_kept = to;
    tgg_log("back to frame %d", to);
    return 0;
}


/* ---- The panel ----------------------------------------------------------- */

static int controller_there(int c)
{
    tgg_viewer_pad pad;
    return tgg_viewer_pad_read(c, &pad);
}

/* The next choice for a row after `from`, one way or the other: the
 * recording, standing still, or a connected controller */
static int next_choice(int from, int dir)
{
    /* choices in order: TAKE_RECORDING, TAKE_STILL, controller 0..3 */
    int i = from == TAKE_RECORDING ? 0 : from == TAKE_STILL ? 1 : from + 2;
    for (int n = 0; n < 6; n++) {
        i = (i + dir + 6) % 6;
        if (i < 2 || controller_there(i - 2)) {
            return i == 0 ? TAKE_RECORDING : i == 1 ? TAKE_STILL : i - 2;
        }
    }
    return from;
}

void takeover_open(int controller)
{
    if (taken || panel) {
        return;
    }
    panel = 1;
    panel_was_paused = tgg_paused();
    tgg_pause(1);
    panel_row = -1;
    for (int p = 0; p < 4; p++) {
        panel_source[p] = TAKE_RECORDING;
        if (players[p].present && panel_row < 0) {
            panel_row = p;
            panel_source[p] = controller;
        }
    }
}

static void start(void)
{
    int any = 0;
    for (int p = 0; p < 4; p++) {
        source[p] = players[p].present ? panel_source[p] : TAKE_RECORDING;
        any |= source[p] != TAKE_RECORDING;
    }
    panel = 0;
    if (!any) {
        tgg_pause(panel_was_paused);
        return;
    }
    taken = 1;
    help_frames = 0;
    forget_ring();
    take_point = tgg_snapshot_save();
    tgg_set_speed(1.0f);
    tgg_pause(0);
    for (int p = 0; p < 4; p++) {
        if (source[p] >= 0) {
            tgg_log("taking over P%d with controller %d", p + 1, source[p] + 1);
        }
    }
}

int takeover_panel_frame(void)
{
    if (!panel) {
        return 0;
    }
    const uint32_t in = tgg_menu_input();
    if (in & TGG_MENU_BACK) {
        panel = 0;
        tgg_pause(panel_was_paused);
        return 1;
    }
    if (in & (TGG_MENU_UP | TGG_MENU_DOWN)) {
        const int dir = in & TGG_MENU_DOWN ? 1 : 3;
        for (int n = 0; n < 4; n++) {
            panel_row = (panel_row + dir) % 4;
            if (players[panel_row].present) {
                break;
            }
        }
    }
    if (in & (TGG_MENU_LEFT | TGG_MENU_RIGHT)) {
        const int v = next_choice(panel_source[panel_row], in & TGG_MENU_RIGHT ? 1 : -1);
        if (v >= 0) {
            /* one controller plays one fighter */
            for (int p = 0; p < 4; p++) {
                if (panel_source[p] == v) {
                    panel_source[p] = TAKE_RECORDING;
                }
            }
        }
        panel_source[panel_row] = v;
    }
    if (in & TGG_MENU_ACCEPT) {
        start();
    }
    return 1;
}

int takeover_again(void)
{
    if (!taken || take_point == NULL) {
        return -1;
    }
    help_frames = 0;
    return tgg_snapshot_load(take_point) == TGG_SNAPSHOT_OK ? 0 : -1;
}

/* ---- Drawing --------------------------------------------------------------- */

static void choice_text(char* out, size_t cap, int v)
{
    if (v == TAKE_RECORDING) {
        snprintf(out, cap, "Recording");
    } else if (v == TAKE_STILL) {
        snprintf(out, cap, "Stand still");
    } else {
        snprintf(out, cap, "Controller %d", v + 1);
    }
}

void takeover_draw(void)
{
    if (taken) {
        if (help_frames < 300 && !gm_GetDbPauseFlag(1)) {
            help_frames++;
            const char* help = "D-pad left: Try again    L+R+A+Start: Leave";
            const float w = tgg_text_width(18, help) + 24;
            tgg_draw_rect(320 - w / 2, 4, w, 26, TGG_RGBA(0, 0, 0, 150));
            tgg_draw_text(320, 8, 18, TGG_RGBA(220, 225, 240, 255), TGG_ALIGN_CENTER, help);
        }
        return;
    }
    if (!panel) {
        return;
    }
    int rows = 0;
    for (int p = 0; p < 4; p++) {
        rows += players[p].present;
    }
    const float w = 470, row_h = 34, x = 320 - w / 2;
    const float h = 48 + rows * (row_h + 6) + 36;
    float y = 240 - h / 2;
    tgg_draw_rect(x, y, w, h, TGG_RGBA(8, 12, 30, 220));
    tgg_draw_rect(x, y, w, 40, TGG_RGBA(0, 0, 0, 200));
    tgg_draw_text(320, y + 8, 26, TGG_RGBA(255, 255, 255, 255), TGG_ALIGN_CENTER, "Take over");
    y += 48;
    for (int p = 0; p < 4; p++) {
        if (!players[p].present) {
            continue;
        }
        const int on = p == panel_row;
        tgg_draw_rect(x + 10, y, w - 20, row_h,
                      on ? TGG_RGBA(230, 190, 60, 255) : TGG_RGBA(30, 45, 100, 230));
        char who[48], what[24];
        snprintf(who, sizeof who, "P%d %s", p + 1, fighter_name(players[p].ckind));
        choice_text(what, sizeof what, panel_source[p]);
        const tgg_color text = on ? TGG_RGBA(20, 20, 30, 255) : TGG_RGBA(255, 255, 255, 255);
        const tgg_color value = on ? TGG_RGBA(20, 20, 30, 255) : TGG_RGBA(240, 200, 80, 255);
        tgg_draw_text(x + 22, y + 6, 22, text, TGG_ALIGN_LEFT, who);
        tgg_draw_text(x + w - 22, y + 6, 22, value, TGG_ALIGN_RIGHT, what);
        y += row_h + 6;
    }
    tgg_draw_text(320, y + 6, 18, TGG_RGBA(220, 225, 240, 255), TGG_ALIGN_CENTER,
                  "Left/Right: Who plays    A: Play from here    B: Back");
}
