/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Save states: D-pad left saves the match as it is right now, D-pad right
 * puts it back: both fighters, percents, the CPU mid-action, items, the
 * stage, the RNG. One state, kept until the match ends.
 *
 * Built on the port's snapshots (tgg/snapshot.h): a save and a load are asked
 * for from the frame event and happen at the next boundary between logic
 * frames. The buttons are read from the viewer's controllers (tgg/input.h),
 * so they never reach the simulation.
 *
 * An offline mod ("class": "offline"): the port switches it off while a
 * netplay session is on, so it never counts toward netplay, and the snapshot
 * API refuses online anyway. Off while a replay plays too: the replay player
 * has its own controls on the D-pad.
 *
 * With tgg.practice-stage installed it's also Practice > Free practice, and it
 * stays off in another drill's match, which has its own resets on the D-pad.
 */
#include "tgg/draw.h"
#include "tgg/events.h"
#include "tgg/input.h"
#include "tgg/menu.h"
#include "tgg/mode.h"
#include "tgg/settings.h"
#include "tgg/snapshot.h"

#include <stddef.h>
#include <string.h>

enum { WHERE_TRAINING, WHERE_EVERY_MATCH };
static int where;
TGG_SETTING_CHOICE_LOCAL(where, "where", "Use in", WHERE_TRAINING, "Training Mode",
                         "Every match");

/* Displayed frames a message stays up, the last FADE of them fading */
#define SHOW 60
#define FADE 15

static tgg_snapshot* saved;
static int was_in_match;
static const char* message;
static int message_age = SHOW;

static void say(const char* text)
{
    message = text;
    message_age = 0;
}

static int replay_playing(void)
{
    static int (*playing)(void);
    static int looked;
    if (!looked) {
        looked = 1;
        playing = (int (*)(void)) tgg_mod_export("tgg.replay-player", "replay_playing@1");
    }
    return playing != NULL && playing();
}

/* tgg.practice-stage, when it's installed: Free practice starts there, and
 * another drill's match is that drill's (it has its own resets on the D-pad) */
typedef struct practice_how {
    const char* drill;
    int keep_cpu, keep_screen, stage;
} practice_how;
static void (*practice_start)(const practice_how*);
static const char* (*practice_drill)(void);

static void init(void)
{
    practice_start = (void (*)(const practice_how*)) tgg_mod_export("tgg.practice-stage",
                                                                     "practice_start@1");
    practice_drill = (const char* (*)(void)) tgg_mod_export("tgg.practice-stage",
                                                             "practice_drill@1");
}
TGG_INIT(init);

static void start_free_practice(void)
{
    static const practice_how how = { .drill = "tgg.save-states" };
    if (practice_start != NULL) {
        practice_start(&how);
    }
}

static const tgg_menu_entry free_practice = {
    .label = "Free practice",
    .description = "Just you on the stage. D-pad left saves, D-pad right loads.",
    .action = start_free_practice,
};
TGG_MENU_ADD("practice/free", free_practice);

static int usable(void)
{
    const char* drill = practice_drill != NULL ? practice_drill() : NULL;
    if (drill != NULL) {
        return tgg_in_match() && strcmp(drill, "tgg.save-states") == 0;
    }
    return tgg_in_match() && !replay_playing() &&
           (where == WHERE_EVERY_MATCH || tgg_mode() == TGG_MODE_TRAINING);
}

/* What went down on any controller since the last displayed frame */
static uint32_t pressed(void)
{
    uint32_t any = 0;
    for (int port = 0; port < 4; port++) {
        tgg_viewer_pad pad;
        if (tgg_viewer_pad_read(port, &pad)) {
            any |= pad.pressed;
        }
    }
    return any;
}

static void on_frame(void)
{
    if (message_age < SHOW) {
        message_age++;
    }
    const int in_match = tgg_in_match();
    if (!in_match && was_in_match) {
        /* a snapshot only loads into its own match */
        tgg_snapshot_free(saved);
        saved = NULL;
    }
    was_in_match = in_match;
    if (!usable()) {
        return;
    }
    const uint32_t in = pressed();
    if (in & TGG_BUTTON_LEFT) {
        tgg_snapshot_free(saved);
        saved = tgg_snapshot_save();
        say(saved != NULL ? "Saved" : "Couldn't save");
        if (saved != NULL) {
            tgg_log("saved");
        }
    } else if (in & TGG_BUTTON_RIGHT) {
        if (saved == NULL) {
            say("Nothing saved yet");
        } else if (tgg_snapshot_load(saved) != TGG_SNAPSHOT_OK) {
            say("Couldn't load");
        }
    }
}
TGG_ON(frame, on_frame);

static void on_loaded(void)
{
    if (usable()) {
        say("Loaded");
        tgg_log("loaded (states take %zu MB)", tgg_snapshot_bytes() >> 20);
    }
}
TGG_ON(state_loaded, on_loaded);

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
