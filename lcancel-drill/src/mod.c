/* SPDX-License-Identifier: GPL-3.0-or-later */
/* L-cancel drill: Practice > L-cancel. Training Mode with nobody else on the
 * stage and a clean screen (tgg.practice-stage), starting on Final
 * Destination, for SHFFLing on your own. The feedback is
 * tgg.lcancel-trainer's: with it installed, every aerial
 * landing says how the L-cancel went. Without it, the drill says so once.
 *
 * An offline mod: the port switches it off for netplay. */
#include "practice_stage.h"

#include "tgg/draw.h"
#include "tgg/events.h"
#include "tgg/menu.h"

#include <stddef.h>
#include <string.h>

#define ID "tgg.lcancel-drill"
/* Displayed frames the missing-trainer line shows */
#define SHOW 300

static void start(void)
{
    static const practice_how how = { .drill = ID, .stage = "melee/final-destination" };
    practice_start(&how);
}

static const tgg_menu_entry entry = {
    .label = "L-cancel",
    .description = "Just you on the stage, with your L-cancel timing after every aerial.",
    .action = start,
};
TGG_MENU_ADD("practice/lcancel", entry);

static int shown = SHOW;
static int was_in_match;

static int mine(void)
{
    const char* drill = practice_drill();
    return tgg_in_match() && drill != NULL && strcmp(drill, ID) == 0;
}

static void on_frame(void)
{
    const int in_match = mine();
    if (in_match && !was_in_match) {
        shown = tgg_mod_loaded("tgg.lcancel-trainer") != NULL ? SHOW : 0;
    }
    was_in_match = in_match;
    if (shown < SHOW) {
        shown++;
    }
}
TGG_ON(frame, on_frame);

static void on_hud(void)
{
    if (!mine() || shown >= SHOW) {
        return;
    }
    const char* text = "Install the L-cancel trainer to see your timing";
    const float w = tgg_text_width(18, text) + 24;
    tgg_draw_rect(320 - w / 2, 96, w, 28, TGG_RGBA(0, 0, 0, 150));
    tgg_draw_text(320, 100, 18, TGG_RGBA(255, 255, 255, 255), TGG_ALIGN_CENTER, text);
}
TGG_ON(hud_draw, on_hud);
