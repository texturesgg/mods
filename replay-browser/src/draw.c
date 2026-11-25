/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The screen: the list on the left, the selected replay on the right, using
 * the shared Melee UI kit over the menus' background. */
#include "browser.h"
#include "ui.h"

#include "tgg/content.h"
#include "tgg/draw.h"

#include <stdio.h>
#include <string.h>

#define LIST_X 24.0f
#define LIST_W 298.0f
#define TOP 76.0f
#define ROW_H 38.0f
#define PREVIEW_X 338.0f
#define PREVIEW_W 278.0f

static const char* const months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

/* "2026-08-14 22:58" as "Aug 14 22:58" */
static void short_date(const char* date, char* out, int cap)
{
    int y, mo, d, h, mi;
    if (sscanf(date, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) == 5 && mo >= 1 && mo <= 12) {
        snprintf(out, (size_t) cap, "%s %d  %02d:%02d", months[mo - 1], d, h, mi);
    } else {
        snprintf(out, (size_t) cap, "%s", date);
    }
}

static void long_date(const char* date, char* out, int cap)
{
    int y, mo, d, h, mi;
    if (sscanf(date, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) == 5 && mo >= 1 && mo <= 12) {
        snprintf(out, (size_t) cap, "%s %d, %d  %02d:%02d", months[mo - 1], d, y, h, mi);
    } else {
        snprintf(out, (size_t) cap, "%s", date[0] != '\0' ? date : "Unknown date");
    }
}

/* Who a slot is, for people: their name, else their code, else the fighter */
static const char* who(const replay_player_slot* s)
{
    if (s->name[0] != '\0') {
        return s->name;
    }
    if (s->code[0] != '\0') {
        return s->code;
    }
    return fighter_name(s->ckind);
}

static void draw_row(int i, float y)
{
    const Entry* e = &br.entries[i];
    const int on = i == br.sel;
    tgg_ui_row(LIST_X, y + 2.0f, LIST_W, ROW_H - 4.0f, on);
    const tgg_color fg = on ? TGG_UI_TEXT_INK : TGG_UI_TEXT_PRIMARY;
    char text[256];
    if (e->is_dir) {
        tgg_draw_text_fit(text, sizeof text, 19.0f, LIST_W - 40.0f,
                          strcmp(e->name, "..") == 0 ? "Up a folder" : e->name);
        tgg_draw_text(LIST_X + 12.0f, y + 9.0f, 19.0f,
                      on ? TGG_UI_TEXT_INK : TGG_UI_TEXT_MUTED, TGG_ALIGN_LEFT, text);
        tgg_draw_text(LIST_X + LIST_W - 12.0f, y + 9.0f, 19.0f,
                      on ? TGG_UI_TEXT_INK : TGG_UI_TEXT_MUTED, TGG_ALIGN_RIGHT,
                      strcmp(e->name, "..") == 0 ? "<" : ">");
        return;
    }
    const replay_summary* s = list_summary(i);
    if (s == NULL) {
        tgg_draw_text_fit(text, sizeof text, 18.0f, LIST_W - 24.0f, e->name);
        tgg_draw_text(LIST_X + 12.0f, y + 10.0f, 18.0f,
                      on ? TGG_UI_TEXT_INK : TGG_UI_TEXT_MUTED, TGG_ALIGN_LEFT, text);
        return;
    }
    short_date(s->date, text, sizeof text);
    tgg_draw_text(LIST_X + 12.0f, y + 10.0f, 18.0f, fg, TGG_ALIGN_LEFT, text);
    /* The fighters' stock icons, the winner's bright */
    float x = LIST_X + LIST_W - 10.0f;
    for (int p = 3; p >= 0; p--) {
        const replay_player_slot* pl = &s->players[p];
        if (!pl->present) {
            continue;
        }
        x -= 26.0f;
        const int lost = s->winner >= 0 && pl->placement != 0 &&
                         !(s->teams && pl->team == s->players[s->winner].team);
        tgg_draw_image(tgg_image_stock_icon(tgg_content_character_id(pl->ckind), pl->costume),
                       x, y + 7.0f, 24.0f, 24.0f,
                       lost ? TGG_RGBA(255, 255, 255, 110) : TGG_RGBA(255, 255, 255, 255));
    }
}

static void draw_list(void)
{
    if (br.sel < br.top) {
        br.top = br.sel;
    }
    if (br.sel >= br.top + LIST_ROWS) {
        br.top = br.sel - LIST_ROWS + 1;
    }
    if (br.n == 0) {
        tgg_ui_panel(LIST_X, TOP + 2.0f, LIST_W, ROW_H * 2.0f);
        tgg_draw_text(LIST_X + LIST_W * 0.5f, TOP + 28.0f, 19.0f, TGG_UI_TEXT_MUTED,
                      TGG_ALIGN_CENTER, "No replays here");
        return;
    }
    for (int i = br.top; i < br.n && i < br.top + LIST_ROWS; i++) {
        draw_row(i, TOP + ROW_H * (float) (i - br.top));
    }
    /* Where the rows shown are in the folder */
    tgg_ui_scrollbar(LIST_X + LIST_W + 6.0f, TOP + 2.0f, ROW_H * LIST_ROWS - 4.0f, br.top,
                     LIST_ROWS, br.n);
}

static void draw_preview(void)
{
    const float x = PREVIEW_X, w = PREVIEW_W;
    const float panel_h = ROW_H * LIST_ROWS - 4.0f;
    tgg_ui_panel(x, TOP + 2.0f, w, panel_h);
    const replay_summary* s = br.n > 0 ? list_summary(br.sel) : NULL;
    if (s == NULL) {
        const char* what = br.n > 0 && br.entries[br.sel].is_dir ? "Folder" : "";
        if (br.n > 0 && !br.entries[br.sel].is_dir) {
            what = "Not a replay this game can read";
        }
        tgg_draw_text(x + w * 0.5f, TOP + 150.0f, 19.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER, what);
        return;
    }

    /* The stage: its icon from the stage select, and its name */
    float y = TOP + 14.0f;
    const tgg_image* icon = tgg_image_stage(tgg_content_stage_id(s->stage));
    const tgg_image* name = tgg_image_stage_name(tgg_content_stage_id(s->stage));
    char text[160];
    if (icon != NULL) {
        tgg_ui_rounded_rect(x + 11.0f, y - 1.0f, 82.0f, 72.0f, 4.0f, TGG_UI_BG_CHIP,
                            TGG_UI_BORDER_BRIGHT, 1.25f);
        tgg_draw_image(icon, x + 12.0f, y, 80.0f, 70.0f, TGG_RGBA(255, 255, 255, 255));
    }
    if (name != NULL) {
        tgg_draw_image(name, x + 98.0f, y + 13.0f, 168.0f, 42.0f, TGG_UI_TEXT_GOLD);
    } else {
        tgg_draw_text_fit(text, sizeof text, 19.0f, w - 112.0f, stage_name(s->stage));
        tgg_draw_text(x + 98.0f, y + 24.0f, 19.0f, TGG_UI_TEXT_GOLD, TGG_ALIGN_LEFT, text);
    }
    y += 84.0f;
    tgg_draw_rect(x + 12.0f, y - 6.0f, w - 24.0f, 1.0f, TGG_UI_DIVIDER);
    y += 4.0f;

    /* The players, winner marked with a clean gold chip */
    for (int p = 0; p < 4; p++) {
        const replay_player_slot* pl = &s->players[p];
        if (!pl->present) {
            continue;
        }
        tgg_draw_image(tgg_image_stock_icon(tgg_content_character_id(pl->ckind), pl->costume),
                       x + 12.0f, y + 3.0f, 28.0f, 28.0f, TGG_RGBA(255, 255, 255, 255));
        const int won = s->winner >= 0 &&
                        (p == s->winner || (s->teams && pl->team == s->players[s->winner].team));
        const float room = won ? w - 126.0f : w - 60.0f;
        tgg_draw_text_fit(text, sizeof text, 18.0f, room, who(pl));
        tgg_draw_text(x + 48.0f, y, 18.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_LEFT, text);
        char sub[80];
        if (pl->cpu) {
            snprintf(sub, sizeof sub, "%s (CPU)", fighter_name(pl->ckind));
        } else if (pl->name[0] != '\0' && pl->code[0] != '\0') {
            snprintf(sub, sizeof sub, "%s  %s", fighter_name(pl->ckind), pl->code);
        } else {
            snprintf(sub, sizeof sub, "P%d  %s", p + 1, fighter_name(pl->ckind));
        }
        tgg_draw_text_fit(text, sizeof text, 14.0f, room, sub);
        tgg_draw_text(x + 48.0f, y + 20.0f, 14.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_LEFT, text);
        if (won) {
            const float cw = tgg_draw_text_width(13.0f, "Winner") + 14.0f;
            tgg_ui_chip(x + w - 12.0f - cw, y + 7.0f, cw, 20.0f, TGG_RGBA(246, 200, 56, 36),
                        TGG_RGBA(246, 200, 56, 175));
            tgg_draw_text(x + w - 12.0f - cw * 0.5f, y + 10.0f, 13.0f, TGG_UI_TEXT_GOLD,
                          TGG_ALIGN_CENTER, "Winner");
        }
        y += 39.0f;
    }

    /* How it went */
    y = TOP + panel_h - 60.0f;
    tgg_draw_rect(x + 12.0f, y - 8.0f, w - 24.0f, 1.0f, TGG_UI_DIVIDER);
    char line[160];
    if (s->frames > 0) {
        const int secs = s->frames / 60;
        snprintf(line, sizeof line, "%d:%02d", secs / 60, secs % 60);
    } else {
        snprintf(line, sizeof line, "Unknown length");
    }
    const char* how = s->end == REPLAY_END_TIME ? "Time"
                      : s->end == REPLAY_END_QUIT ? "No contest"
                      : s->end == REPLAY_END_GAME ? "Game"
                                                   : "";
    if (how[0] != '\0') {
        const size_t n = strlen(line);
        snprintf(line + n, sizeof line - n, "   %s", how);
    }
    if (s->online) {
        const size_t n = strlen(line);
        snprintf(line + n, sizeof line - n, "   Online");
    }
    tgg_draw_text_fit(text, sizeof text, 17.0f, w - 24.0f, line);
    tgg_draw_text(x + 12.0f, y, 17.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_LEFT, text);
    long_date(s->date, text, sizeof text);
    tgg_draw_text(x + 12.0f, y + 25.0f, 14.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_LEFT, text);
    snprintf(line, sizeof line, "Slippi %d.%d.%d", s->version[0], s->version[1], s->version[2]);
    tgg_draw_text(x + w - 12.0f, y + 25.0f, 14.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_RIGHT, line);
}

/* Over the preview while tgg gets what a replay needs */
static void draw_restart(void)
{
    const tgg_restart_status* r = &br.restart;
    const float x = PREVIEW_X, w = PREVIEW_W, y = TOP + 56.0f, h = 186.0f;
    tgg_ui_modal(x, y, w, h);
    char text[300];
    if (r->state == TGG_RESTART_NEEDS_TRUST) {
        tgg_draw_text(x + 14.0f, y + 16.0f, 18.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_LEFT,
                      "It needs native code by");
        tgg_draw_text_fit(text, sizeof text, 18.0f, w - 28.0f, r->trust);
        tgg_draw_text(x + 14.0f, y + 42.0f, 18.0f, TGG_UI_TEXT_GOLD, TGG_ALIGN_LEFT, text);
        tgg_draw_text(x + 14.0f, y + 74.0f, 15.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_LEFT,
                      "It runs on your computer.");
        tgg_draw_text(x + 14.0f, y + 96.0f, 15.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_LEFT,
                      "Download it to watch?");
        const tgg_ui_hint hints[] = {
            { TGG_UI_BTN_A, "Yes, always" },
            { TGG_UI_BTN_B, "No" },
        };
        tgg_ui_hint_bar(hints, 2, x + w * 0.5f, y + h - 24.0f, 15.0f);
        return;
    }
    tgg_draw_text(x + 14.0f, y + 16.0f, 18.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_LEFT,
                  "Downloading what it needs");
    tgg_draw_text_fit(text, sizeof text, 15.0f, w - 28.0f, r->label);
    tgg_draw_text(x + 14.0f, y + 48.0f, 15.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_LEFT, text);
    const float done = r->total > 0 ? (float) (r->bytes / r->total) : 0.0f;
    tgg_ui_progress(x + 14.0f, y + 78.0f, w - 28.0f, 10.0f, done);
    if (r->steps > 0) {
        snprintf(text, sizeof text, "%d of %d", r->step + 1, r->steps);
        tgg_draw_text(x + w - 14.0f, y + 96.0f, 14.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_RIGHT, text);
    }
    tgg_draw_text(x + 14.0f, y + 124.0f, 14.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_LEFT,
                  "The game restarts to play it.");
    const tgg_ui_hint hints[] = { { TGG_UI_BTN_B, "Stop" } };
    tgg_ui_hint_bar(hints, 1, x + w * 0.5f, y + h - 22.0f, 15.0f);
}

void browser_draw(void)
{
    char text[PATH_MAX_LEN];
    tgg_draw_text_fit_end(text, sizeof text, 16.0f, 380.0f, br.folder);
    tgg_ui_header("Replays", text);

    draw_list();
    draw_preview();
    if (br.restarting) {
        draw_restart();
    }

    if (br.message_frames > 0) {
        tgg_ui_footer_band();
        tgg_draw_text_fit(text, sizeof text, 19.0f, 600.0f, br.message);
        tgg_draw_text(320.0f, 450.0f, 19.0f, TGG_UI_TEXT_GOLD, TGG_ALIGN_CENTER, text);
    } else {
        const int dir = br.n > 0 && br.entries[br.sel].is_dir;
        const tgg_ui_hint hints[] = {
            { TGG_UI_BTN_A, dir ? "Open" : "Play" },
            { TGG_UI_BTN_Y, "Use folder" },
            { TGG_UI_BTN_X, "Order" },
            { TGG_UI_BTN_B, "Back" },
        };
        tgg_ui_footer_hints(hints, 4);
    }
}
