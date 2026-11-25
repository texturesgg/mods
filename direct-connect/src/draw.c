/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The screen, using the shared Melee UI kit: edge-to-edge header and footer,
 * rounded dark-navy panels, gold focus states, and drawn GameCube button pills. */
#include "dc.h"
#include "ui.h"

#include "tgg/draw.h"

#include <stdio.h>
#include <string.h>

static void band(const char* title, const char* status)
{
    tgg_ui_header(title, status);
}

static void footer_hints(const tgg_ui_hint* hints, int n)
{
    if (dc.message_frames > 0) {
        tgg_ui_panel(60.0f, 394.0f, 520.0f, 36.0f);
        tgg_draw_text(320.0f, 403.0f, 18.0f, TGG_UI_TEXT_GOLD, TGG_ALIGN_CENTER, dc.message);
    }
    tgg_ui_footer_hints(hints, n);
}

/* The room's code and who's in it, from `y` */
static void draw_room_code(float y)
{
    const meet_status* m = &dc.meet;
    tgg_draw_text(320.0f, y, 17.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER, "Room code");
    tgg_ui_modal(190.0f, y + 26.0f, 260.0f, 60.0f);
    tgg_draw_text(320.0f, y + 38.0f, 36.0f, TGG_UI_TEXT_GOLD, TGG_ALIGN_CENTER, m->code);
    y += 106.0f;
    for (int i = 0; i < m->n_members && i < MEET_MEMBERS; i++) {
        char fit[64];
        tgg_draw_text_fit(fit, sizeof fit, 19.0f, 320.0f, m->members[i]);
        const float w = tgg_draw_text_width(19.0f, fit) + 28.0f;
        tgg_ui_chip(320.0f - w * 0.5f, y, w, 28.0f,
                    i == 0 ? TGG_UI_BG_ROW : TGG_UI_BG_CHIP,
                    i == 0 ? TGG_UI_BORDER_BRIGHT : TGG_UI_BORDER_ROW);
        tgg_draw_text(320.0f, y + 4.0f, 19.0f,
                      i == 0 ? TGG_UI_TEXT_PRIMARY : TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER, fit);
        y += 34.0f;
    }
}

/* Making or joining a room */
static void draw_room(void)
{
    const meet_status* m = &dc.meet;
    band("Online", NULL);
    char line[320];
    switch (m->state) {
    case MEET_SIGN_IN:
        tgg_ui_panel(80.0f, 98.0f, 480.0f, 260.0f);
        tgg_draw_text(320.0f, 118.0f, 24.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER,
                      "Sign in to textures.gg");
        tgg_draw_text(320.0f, 162.0f, 17.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER,
                      "On your phone or computer, go to");
        tgg_draw_text_fit(line, sizeof line, 20.0f, 440.0f, m->signin_url);
        tgg_draw_text(320.0f, 190.0f, 20.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER, line);
        tgg_draw_text(320.0f, 232.0f, 17.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER, "and enter");
        tgg_ui_modal(195.0f, 260.0f, 250.0f, 60.0f);
        tgg_draw_text(320.0f, 272.0f, 36.0f, TGG_UI_TEXT_GOLD, TGG_ALIGN_CENTER, m->signin_code);
        break;
    case MEET_IN_ROOM:
        draw_room_code(102.0f);
        tgg_draw_text(320.0f, 342.0f, 18.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER,
                      m->n_members < 2 ? "Give your friends this code."
                                       : "Waiting for everyone to connect.");
        break;
    case MEET_PUNCHING:
        draw_room_code(102.0f);
        tgg_draw_text(320.0f, 342.0f, 21.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER, "Connecting");
        break;
    case MEET_CONNECTED:
        draw_room_code(102.0f);
        tgg_draw_text(320.0f, 342.0f, 21.0f, TGG_UI_TEXT_GOOD, TGG_ALIGN_CENTER, "Connected");
        break;
    default:
        tgg_ui_panel(110.0f, 176.0f, 420.0f, 84.0f);
        tgg_draw_text(320.0f, 207.0f, 21.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER,
                      "Getting a room on textures.gg");
        break;
    }
    const tgg_ui_hint hints[] = { { TGG_UI_BTN_B, "Leave" } };
    footer_hints(hints, 1);
}

static void draw_address(void)
{
    band(dc.typing_code ? "Join a room" : "Join", NULL);
    tgg_draw_text(320.0f, 70.0f, 17.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER,
                  dc.typing_code ? "Room code" : "Host address");
    keyboard_draw();
    const tgg_ui_hint hints[] = {
        { TGG_UI_BTN_A, "Type" },
        { TGG_UI_BTN_X, "Delete" },
        { TGG_UI_BTN_START, "Connect" },
        { TGG_UI_BTN_B, "Back" },
    };
    footer_hints(hints, 4);
}

static void draw_connecting(void)
{
    band("Join", NULL);
    netplay_info info;
    netplay_status(&info);
    tgg_ui_panel(80.0f, 168.0f, 480.0f, 116.0f);
    char line[128], fit[128];
    snprintf(line, sizeof line, "Connecting to %s", info.peer[0] != '\0' ? info.peer : "...");
    tgg_draw_text_fit(fit, sizeof fit, 24.0f, 444.0f, line);
    tgg_draw_text(320.0f, 194.0f, 24.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER, fit);
    tgg_draw_text(320.0f, 236.0f, 17.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER,
                  "The host needs to be on the Hosting screen.");
    const tgg_ui_hint hints[] = { { TGG_UI_BTN_B, "Cancel" } };
    footer_hints(hints, 1);
}

static void draw_failed(void)
{
    band("Online", NULL);
    tgg_ui_modal(64.0f, 164.0f, 512.0f, 112.0f);
    char fit[160];
    tgg_draw_text_fit(fit, sizeof fit, 19.0f, 476.0f, dc.why);
    tgg_draw_text(320.0f, 186.0f, 22.0f, TGG_UI_TEXT_GOLD, TGG_ALIGN_CENTER, "Disconnected");
    tgg_draw_text(320.0f, 224.0f, 19.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER, fit);
    const tgg_ui_hint hints[] = {
        { TGG_UI_BTN_A, dc.for_netplay ? "Back to your game" : "OK" },
    };
    footer_hints(hints, 1);
}

/* ---- The host has other mods */

static void draw_list(float x, const char* title, char lines[][DIFF_TEXT], int n)
{
    tgg_ui_panel(x, 148.0f, 270.0f, 234.0f);
    tgg_draw_text(x + 14.0f, 158.0f, 17.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_LEFT, title);
    tgg_draw_rect(x + 12.0f, 182.0f, 246.0f, 1.0f, TGG_UI_DIVIDER);
    float y = 192.0f;
    const int shown = n < DIFF_LINES ? n : DIFF_LINES;
    for (int i = 0; i < shown; i++) {
        char fit[DIFF_TEXT];
        tgg_draw_text_fit(fit, sizeof fit, 17.0f, 242.0f, lines[i]);
        tgg_draw_text(x + 14.0f, y, 17.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_LEFT, fit);
        y += 23.0f;
    }
    if (n > shown) {
        char more[32];
        snprintf(more, sizeof more, "and %d more", n - shown);
        tgg_draw_text(x + 14.0f, y, 17.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_LEFT, more);
    } else if (n == 0) {
        tgg_draw_text(x + 14.0f, y, 17.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_LEFT, "Nothing else");
    }
}

static void draw_different(void)
{
    band("Join", NULL);
    netplay_info info;
    netplay_status(&info);
    tgg_draw_text(320.0f, 76.0f, 23.0f, TGG_UI_TEXT_GOLD, TGG_ALIGN_CENTER, info.why);
    if (dc.bad_profile) {
        tgg_ui_panel(110.0f, 176.0f, 420.0f, 84.0f);
        tgg_draw_text(320.0f, 208.0f, 18.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER,
                      "Can't read the host's list of mods.");
        const tgg_ui_hint hints[] = { { TGG_UI_BTN_B, "Leave" } };
        footer_hints(hints, 1);
        return;
    }
    if (!dc.have_diff) {
        tgg_ui_panel(110.0f, 176.0f, 420.0f, 84.0f);
        tgg_draw_text(320.0f, 208.0f, 18.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER,
                      "Checking the host's mods...");
        const tgg_ui_hint hints[] = { { TGG_UI_BTN_B, "Leave" } };
        footer_hints(hints, 1);
        return;
    }
    const Diff* d = &dc.diff;
    if (strcmp(d->game_there, d->game_here) != 0) {
        char line[128];
        snprintf(line, sizeof line, "Game: %s there, %s here", d->game_there, d->game_here);
        tgg_draw_text(320.0f, 112.0f, 18.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER, line);
    } else if (d->n_there == 0 && d->n_here == 0) {
        tgg_draw_text(320.0f, 112.0f, 18.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER,
                      "Same versions, but the builds differ.");
    }
    draw_list(40.0f, "The host has", dc.diff.there, d->n_there);
    draw_list(330.0f, "You have", dc.diff.here, d->n_here);
    if (dc.for_netplay) {
        tgg_draw_text(320.0f, 398.0f, 17.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER,
                      "The host's mods changed. Leave, then join again.");
        const tgg_ui_hint hints[] = { { TGG_UI_BTN_B, "Leave" } };
        footer_hints(hints, 1);
        return;
    }
    const tgg_ui_hint hints[] = {
        { TGG_UI_BTN_A, "Get them and restart" },
        { TGG_UI_BTN_B, "Leave" },
    };
    footer_hints(hints, 2);
}

/* While tgg gets the host's mods */
static void draw_restarting(void)
{
    band("Join", NULL);
    const tgg_restart_status* r = &dc.restart;
    tgg_ui_modal(60.0f, 146.0f, 520.0f, 178.0f);
    char text[300];
    if (r->state == TGG_RESTART_NEEDS_TRUST) {
        /* tgg only asks about native code from publishers who aren't official */
        tgg_draw_text(82.0f, 168.0f, 20.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_LEFT,
                      "The host's mods run native code by");
        tgg_draw_text_fit(text, sizeof text, 20.0f, 476.0f, r->trust);
        tgg_draw_text(82.0f, 198.0f, 20.0f, TGG_UI_TEXT_GOLD, TGG_ALIGN_LEFT, text);
        tgg_draw_text(82.0f, 236.0f, 18.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_LEFT,
                      "It runs on your computer. Download it?");
        const tgg_ui_hint hints[] = {
            { TGG_UI_BTN_A, "Yes, always" },
            { TGG_UI_BTN_B, "No" },
        };
        footer_hints(hints, 2);
        return;
    }
    tgg_draw_text(82.0f, 166.0f, 20.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_LEFT,
                  "Downloading the host's mods");
    tgg_draw_text_fit(text, sizeof text, 18.0f, 476.0f, r->label);
    tgg_draw_text(82.0f, 198.0f, 18.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_LEFT, text);
    const float done = r->total > 0 ? (float) (r->bytes / r->total) : 0.0f;
    tgg_ui_progress(82.0f, 232.0f, 476.0f, 12.0f, done);
    if (r->steps > 0) {
        snprintf(text, sizeof text, "%d of %d", r->step + 1, r->steps);
        tgg_draw_text(558.0f, 252.0f, 16.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_RIGHT, text);
    }
    tgg_draw_text(82.0f, 286.0f, 16.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_LEFT,
                  "Then the game restarts and joins the host.");
    const tgg_ui_hint hints[] = { { TGG_UI_BTN_B, "Stop" } };
    footer_hints(hints, 1);
}

void dc_draw(void)
{
    switch (dc.view) {
    case VIEW_ADDRESS:
        draw_address();
        break;
    case VIEW_CONNECTING:
        draw_connecting();
        break;
    case VIEW_FAILED:
        draw_failed();
        break;
    case VIEW_DIFFERENT:
        draw_different();
        break;
    case VIEW_RESTARTING:
        draw_restarting();
        break;
    case VIEW_ROOM:
    case VIEW_PLAY:
        draw_room();
        break;
    }
}
