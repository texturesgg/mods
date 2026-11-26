/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The screen: filter/sort strip on top, list on the left, selected replay on
 * the right, and delete confirmation modal, using the shared Melee UI kit. */
#include "browser.h"
#include "ui.h"

#include "tgg/content.h"
#include "tgg/draw.h"

#include <stdio.h>
#include <string.h>

#define FILTER_Y 66.0f
#define FILTER_H 26.0f
#define LIST_X 24.0f
#define LIST_W 298.0f
#define TOP 100.0f
#define ROW_H 40.0f
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

static float draw_filter_pill(float x, const char* label, float max_w, int active, int focused)
{
    char fit[64];
    tgg_draw_text_fit(fit, sizeof fit, 14.0f, max_w - 18.0f, label);
    const float w = tgg_draw_text_width(14.0f, fit) + 18.0f;
    tgg_color bg = TGG_UI_BG_CHIP;
    tgg_color border = TGG_UI_BORDER_ROW;
    tgg_color fg = TGG_UI_TEXT_MUTED;
    float bw = 1.0f;
    if (focused) {
        bg = TGG_UI_BG_ROW_FOCUS;
        border = TGG_UI_BORDER_FOCUS;
        fg = TGG_UI_TEXT_INK;
        bw = 1.4f;
    } else if (active) {
        bg = TGG_RGBA(64, 98, 198, 115);
        border = TGG_UI_BORDER_BRIGHT;
        fg = TGG_UI_TEXT_PRIMARY;
        bw = 1.2f;
    }
    tgg_ui_rounded_rect(x, FILTER_Y, w, FILTER_H, FILTER_H * 0.5f, bg, border, bw);
    tgg_draw_text(x + w * 0.5f, FILTER_Y + 6.0f, 14.0f, fg, TGG_ALIGN_CENTER, fit);
    return w;
}

static void draw_filter_strip(void)
{
    const int in_bar = br.focus == FOCUS_FILTERS;
    float x = LIST_X;

    const char* mode_label = br.filter_mode == FILTER_ONLINE  ? "Online"
                             : br.filter_mode == FILTER_LOCAL ? "Local"
                                                              : "All";
    x += draw_filter_pill(x, mode_label, 78.0f, br.filter_mode != FILTER_ALL,
                          in_bar && br.filter_col == 0) +
         7.0f;

    const char* fighter_label =
        br.filter_fighter >= 0 ? fighter_name(br.filter_fighter) : "Any fighter";
    x += draw_filter_pill(x, fighter_label, 118.0f, br.filter_fighter >= 0,
                          in_bar && br.filter_col == 1) +
         7.0f;

    const char* stage_label = br.filter_stage >= 0 ? stage_name(br.filter_stage) : "Any stage";
    x += draw_filter_pill(x, stage_label, 134.0f, br.filter_stage >= 0,
                          in_bar && br.filter_col == 2) +
         7.0f;

    if (list_has_filters()) {
        draw_filter_pill(x, "Reset", 66.0f, 0, in_bar && br.filter_col == 3);
    }

    /* Right side: sort order + replay count badge (or scanning progress) */
    char badge[80];
    const char* order = setting_sort == SORT_NEWEST ? "Newest" : "Oldest";
    const int scanning = br.scan_idx < br.n && (in_bar || list_has_filters());
    if (scanning) {
        snprintf(badge, sizeof badge, "Indexing %d / %d", br.scan_idx, br.n);
    } else if (list_has_filters()) {
        snprintf(badge, sizeof badge, "%s  %d / %d", order, br.shown_replays, br.total_replays);
    } else {
        snprintf(badge, sizeof badge, "%s  %d %s", order, br.total_replays,
                 br.total_replays == 1 ? "replay" : "replays");
    }
    const float bw = tgg_draw_text_width(14.0f, badge) + 20.0f;
    const float bx = PREVIEW_X + PREVIEW_W - bw;
    tgg_ui_rounded_rect(bx, FILTER_Y, bw, FILTER_H, FILTER_H * 0.5f, TGG_UI_BG_BAND,
                        TGG_UI_BORDER_SUBTLE, 1.0f);
    tgg_draw_text(bx + bw * 0.5f, FILTER_Y + 6.0f, 14.0f,
                  list_has_filters() ? TGG_UI_TEXT_GOLD : TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER,
                  badge);
}

static void draw_row(int view_idx, float y)
{
    const int entry_idx = br.view[view_idx];
    const Entry* e = &br.entries[entry_idx];
    const int selected = view_idx == br.sel;
    const int focused = selected && br.focus == FOCUS_LIST;
    if (focused) {
        tgg_ui_row(LIST_X, y + 2.0f, LIST_W, ROW_H - 4.0f, 1);
    } else if (selected) {
        tgg_ui_rounded_rect(LIST_X, y + 2.0f, LIST_W, ROW_H - 4.0f, TGG_UI_RADIUS_ROW,
                            TGG_RGBA(34, 48, 108, 224), TGG_UI_BORDER_BRIGHT, 1.4f);
    } else {
        tgg_ui_row(LIST_X, y + 2.0f, LIST_W, ROW_H - 4.0f, 0);
    }
    const tgg_color fg = focused ? TGG_UI_TEXT_INK : TGG_UI_TEXT_PRIMARY;
    const tgg_color muted = focused ? TGG_UI_TEXT_INK : TGG_UI_TEXT_MUTED;
    char text[256];
    if (e->is_dir) {
        tgg_draw_text_fit(text, sizeof text, 19.0f, LIST_W - 40.0f,
                          strcmp(e->name, "..") == 0 ? "Up a folder" : e->name);
        tgg_draw_text(LIST_X + 12.0f, y + 10.0f, 19.0f, muted, TGG_ALIGN_LEFT, text);
        tgg_draw_text(LIST_X + LIST_W - 12.0f, y + 10.0f, 19.0f, muted, TGG_ALIGN_RIGHT,
                      strcmp(e->name, "..") == 0 ? "<" : ">");
        return;
    }
    const replay_summary* s = list_summary(entry_idx);
    if (s == NULL) {
        tgg_draw_text_fit(text, sizeof text, 18.0f, LIST_W - 24.0f, e->name);
        tgg_draw_text(LIST_X + 12.0f, y + 11.0f, 18.0f, muted, TGG_ALIGN_LEFT, text);
        return;
    }
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
                       x, y + 8.0f, 24.0f, 24.0f,
                       lost ? TGG_RGBA(255, 255, 255, 110) : TGG_RGBA(255, 255, 255, 255));
    }
    const float label_max = (x - 8.0f) - (LIST_X + 12.0f);
    if (s->date[0] != '\0') {
        short_date(s->date, text, sizeof text);
        tgg_draw_text(LIST_X + 12.0f, y + 11.0f, 18.0f, fg, TGG_ALIGN_LEFT, text);
    } else {
        tgg_draw_text_fit(text, sizeof text, 17.0f, label_max > 80.0f ? label_max : 80.0f,
                          stage_name(s->stage));
        tgg_draw_text(LIST_X + 12.0f, y + 11.0f, 17.0f, fg, TGG_ALIGN_LEFT, text);
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
    if (br.n_view == 0) {
        tgg_ui_panel(LIST_X, TOP + 2.0f, LIST_W, ROW_H * 2.2f);
        tgg_draw_text(LIST_X + LIST_W * 0.5f, TOP + 24.0f, 19.0f, TGG_UI_TEXT_MUTED,
                      TGG_ALIGN_CENTER,
                      list_has_filters() ? "No matching replays" : "No replays here");
        if (list_has_filters()) {
            tgg_draw_text(LIST_X + LIST_W * 0.5f, TOP + 52.0f, 14.0f, TGG_UI_TEXT_GOLD,
                          TGG_ALIGN_CENTER, "Press Z to adjust or reset filters");
        }
        return;
    }
    for (int i = br.top; i < br.n_view && i < br.top + LIST_ROWS; i++) {
        draw_row(i, TOP + ROW_H * (float) (i - br.top));
    }
    if (list_has_filters() && br.shown_replays == 0) {
        const float ey = TOP + ROW_H * (float) (br.n_view - br.top) + 12.0f;
        if (ey + 68.0f <= TOP + ROW_H * LIST_ROWS) {
            tgg_ui_panel(LIST_X, ey, LIST_W, 66.0f);
            tgg_draw_text(LIST_X + LIST_W * 0.5f, ey + 14.0f, 17.0f, TGG_UI_TEXT_MUTED,
                          TGG_ALIGN_CENTER, "No matching replays");
            tgg_draw_text(LIST_X + LIST_W * 0.5f, ey + 38.0f, 14.0f, TGG_UI_TEXT_GOLD,
                          TGG_ALIGN_CENTER, "Press Z to adjust or reset filters");
        }
    }
    /* Where the rows shown are in the list */
    tgg_ui_scrollbar(LIST_X + LIST_W + 6.0f, TOP + 2.0f, ROW_H * LIST_ROWS - 4.0f, br.top,
                     LIST_ROWS, br.n_view);
}

static void draw_preview(void)
{
    const float x = PREVIEW_X, w = PREVIEW_W;
    const float panel_h = ROW_H * LIST_ROWS - 4.0f;
    tgg_ui_panel(x, TOP + 2.0f, w, panel_h);
    const int idx = list_selected_entry();
    const replay_summary* s = idx >= 0 ? list_summary(idx) : NULL;
    if (s == NULL) {
        const char* what = "";
        if (idx >= 0 && br.entries[idx].is_dir) {
            what = strcmp(br.entries[idx].name, "..") == 0 ? "Parent folder" : "Folder";
        } else if (idx >= 0) {
            what = "Not a replay this game can read";
        }
        tgg_draw_text(x + w * 0.5f, TOP + 142.0f, 18.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER,
                      what);
        return;
    }

    /* Stage icon and name */
    float y = TOP + 12.0f;
    const tgg_image* icon = tgg_image_stage(tgg_content_stage_id(s->stage));
    const tgg_image* name = tgg_image_stage_name(tgg_content_stage_id(s->stage));
    char text[160];
    if (icon != NULL) {
        tgg_ui_rounded_rect(x + 11.0f, y - 1.0f, 74.0f, 64.0f, 4.0f, TGG_UI_BG_CHIP,
                            TGG_UI_BORDER_BRIGHT, 1.25f);
        tgg_draw_image(icon, x + 12.0f, y, 72.0f, 62.0f, TGG_RGBA(255, 255, 255, 255));
    }
    if (name != NULL) {
        tgg_draw_image(name, x + 92.0f, y + 11.0f, 174.0f, 40.0f, TGG_UI_TEXT_GOLD);
    } else {
        tgg_draw_text_fit(text, sizeof text, 18.0f, w - 104.0f, stage_name(s->stage));
        tgg_draw_text(x + 92.0f, y + 21.0f, 18.0f, TGG_UI_TEXT_GOLD, TGG_ALIGN_LEFT, text);
    }
    y += 72.0f;
    tgg_draw_rect(x + 12.0f, y - 4.0f, w - 24.0f, 1.0f, TGG_UI_DIVIDER);
    y += 3.0f;

    int present = 0;
    for (int p = 0; p < 4; p++) {
        if (s->players[p].present) {
            present++;
        }
    }
    const float row_step = present <= 2 ? 39.0f : 31.0f;
    const float icon_sz = present <= 2 ? 28.0f : 23.0f;
    const float name_sz = present <= 2 ? 18.0f : 15.5f;
    const float sub_sz = present <= 2 ? 14.0f : 12.5f;

    /* Players, winner marked with a gold chip */
    for (int p = 0; p < 4; p++) {
        const replay_player_slot* pl = &s->players[p];
        if (!pl->present) {
            continue;
        }
        tgg_draw_image(tgg_image_stock_icon(tgg_content_character_id(pl->ckind), pl->costume),
                       x + 12.0f, y + 3.0f, icon_sz, icon_sz, TGG_RGBA(255, 255, 255, 255));
        const int won = s->winner >= 0 &&
                        (p == s->winner || (s->teams && pl->team == s->players[s->winner].team));
        const float tx = x + 18.0f + icon_sz;
        const float room = won ? (x + w - 82.0f) - tx : (x + w - 14.0f) - tx;
        tgg_draw_text_fit(text, sizeof text, name_sz, room, who(pl));
        tgg_draw_text(tx, y, name_sz, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_LEFT, text);
        char sub[80];
        if (pl->cpu) {
            snprintf(sub, sizeof sub, "%s (CPU)", fighter_name(pl->ckind));
        } else if (pl->name[0] != '\0' && pl->code[0] != '\0') {
            snprintf(sub, sizeof sub, "%s  %s", fighter_name(pl->ckind), pl->code);
        } else {
            snprintf(sub, sizeof sub, "P%d  %s", p + 1, fighter_name(pl->ckind));
        }
        tgg_draw_text_fit(text, sizeof text, sub_sz, room, sub);
        tgg_draw_text(tx, y + (present <= 2 ? 19.0f : 15.5f), sub_sz, TGG_UI_TEXT_MUTED,
                      TGG_ALIGN_LEFT, text);
        if (won) {
            const float cw = tgg_draw_text_width(12.5f, "Winner") + 12.0f;
            tgg_ui_chip(x + w - 12.0f - cw, y + 5.0f, cw, 19.0f, TGG_RGBA(246, 200, 56, 36),
                        TGG_RGBA(246, 200, 56, 175));
            tgg_draw_text(x + w - 12.0f - cw * 0.5f, y + 8.0f, 12.5f, TGG_UI_TEXT_GOLD,
                          TGG_ALIGN_CENTER, "Winner");
        }
        y += row_step;
    }

    /* Match details */
    y = TOP + panel_h - 86.0f;
    tgg_draw_rect(x + 12.0f, y - 6.0f, w - 24.0f, 1.0f, TGG_UI_DIVIDER);
    char line[160];
    if (s->frames > 0) {
        const int secs = s->frames / 60;
        snprintf(line, sizeof line, "%d:%02d", secs / 60, secs % 60);
    } else {
        snprintf(line, sizeof line, "Unknown length");
    }
    const char* how = s->end == REPLAY_END_TIME   ? "Time"
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
    tgg_draw_text_fit(text, sizeof text, 16.0f, w - 24.0f, line);
    tgg_draw_text(x + 12.0f, y, 16.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_LEFT, text);
    long_date(s->date, text, sizeof text);
    tgg_draw_text(x + 12.0f, y + 22.0f, 13.5f, TGG_UI_TEXT_MUTED, TGG_ALIGN_LEFT, text);
    snprintf(line, sizeof line, "Slippi %d.%d.%d", s->version[0], s->version[1], s->version[2]);
    tgg_draw_text(x + w - 12.0f, y + 22.0f, 13.5f, TGG_UI_TEXT_MUTED, TGG_ALIGN_RIGHT, line);

    /* Delete replay action pill */
    const int del_on = br.focus == FOCUS_DELETE_BTN;
    const float by = TOP + panel_h - 36.0f;
    const float bh = 26.0f;
    tgg_ui_rounded_rect(x + 12.0f, by, w - 24.0f, bh, bh * 0.5f,
                        del_on ? TGG_UI_GC_B_FILL : TGG_RGBA(56, 22, 34, 150),
                        del_on ? TGG_UI_BORDER_FOCUS : TGG_RGBA(224, 92, 98, 115),
                        del_on ? 1.6f : 1.0f);
    tgg_draw_text(x + w * 0.5f, by + 6.0f, 14.5f,
                  del_on ? TGG_UI_TEXT_PRIMARY : TGG_RGBA(244, 158, 162, 235), TGG_ALIGN_CENTER,
                  "Delete replay");
}

/* Over the preview while tgg gets what a replay needs */
static void draw_restart(void)
{
    const tgg_restart_status* r = &br.restart;
    const float x = PREVIEW_X, w = PREVIEW_W, y = TOP + 48.0f, h = 186.0f;
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

static void draw_delete_modal(void)
{
    const tgg_rect b = tgg_draw_bounds();
    tgg_draw_rect(b.x, b.y, b.w, b.h, TGG_RGBA(4, 7, 18, 175));
    const float x = 132.0f, y = 148.0f, w = 376.0f, h = 180.0f;
    tgg_ui_modal(x, y, w, h);

    tgg_draw_text(x + w * 0.5f, y + 18.0f, 23.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER,
                  "Delete replay?");

    const int idx = list_selected_entry();
    char line[256] = "";
    if (idx >= 0) {
        const replay_summary* s = list_summary(idx);
        if (s != NULL) {
            char fighters[128] = "";
            int n_p = 0;
            for (int p = 0; p < 4; p++) {
                if (!s->players[p].present) {
                    continue;
                }
                const size_t len = strlen(fighters);
                snprintf(fighters + len, sizeof fighters - len, "%s%s", n_p > 0 ? " vs " : "",
                         fighter_name(s->players[p].ckind));
                n_p++;
            }
            snprintf(line, sizeof line, "%s  -  %s", fighters, stage_name(s->stage));
        } else {
            snprintf(line, sizeof line, "%s", br.entries[idx].name);
        }
    }
    char fit[200];
    tgg_draw_text_fit(fit, sizeof fit, 16.0f, w - 36.0f, line);
    tgg_draw_text(x + w * 0.5f, y + 54.0f, 16.0f, TGG_UI_TEXT_GOLD, TGG_ALIGN_CENTER, fit);

    if (idx >= 0) {
        tgg_draw_text_fit(fit, sizeof fit, 13.5f, w - 36.0f, br.entries[idx].name);
        tgg_draw_text(x + w * 0.5f, y + 78.0f, 13.5f, TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER, fit);
    }

    tgg_draw_text(x + w * 0.5f, y + 104.0f, 14.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER,
                  "This permanently removes the .slp file.");

    const float btn_y = y + h - 46.0f;
    const float btn_w = 148.0f;
    const float btn_h = 30.0f;
    const int cancel_on = !br.confirm_yes;
    const int del_on = br.confirm_yes;

    tgg_ui_rounded_rect(x + 28.0f, btn_y, btn_w, btn_h, btn_h * 0.5f,
                        cancel_on ? TGG_UI_BG_ROW_FOCUS : TGG_UI_BG_CHIP,
                        cancel_on ? TGG_UI_BORDER_FOCUS : TGG_UI_BORDER_ROW,
                        cancel_on ? 1.5f : 1.0f);
    tgg_draw_text(x + 28.0f + btn_w * 0.5f, btn_y + 7.0f, 16.0f,
                  cancel_on ? TGG_UI_TEXT_INK : TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER, "Cancel");

    tgg_ui_rounded_rect(x + w - 28.0f - btn_w, btn_y, btn_w, btn_h, btn_h * 0.5f,
                        del_on ? TGG_UI_GC_B_FILL : TGG_RGBA(56, 22, 34, 175),
                        del_on ? TGG_UI_BORDER_FOCUS : TGG_RGBA(224, 92, 98, 120),
                        del_on ? 1.5f : 1.0f);
    tgg_draw_text(x + w - 28.0f - btn_w * 0.5f, btn_y + 7.0f, 16.0f,
                  del_on ? TGG_UI_TEXT_PRIMARY : TGG_RGBA(244, 158, 162, 235), TGG_ALIGN_CENTER,
                  "Delete");
}

void browser_draw(void)
{
    char text[PATH_MAX_LEN];
    tgg_draw_text_fit_end(text, sizeof text, 16.0f, 380.0f, br.folder);
    tgg_ui_header("Replays", text);

    draw_filter_strip();
    draw_list();
    draw_preview();
    if (br.restarting) {
        draw_restart();
    }
    if (br.confirm_delete) {
        draw_delete_modal();
    }

    if (br.confirm_delete) {
        const tgg_ui_hint hints[] = {
            { TGG_UI_BTN_LR, "Choose" },
            { TGG_UI_BTN_A, "Confirm" },
            { TGG_UI_BTN_B, "Cancel" },
        };
        tgg_ui_footer_hints(hints, 3);
    } else if (br.message_frames > 0) {
        tgg_ui_footer_band();
        tgg_draw_text_fit(text, sizeof text, 19.0f, 600.0f, br.message);
        tgg_draw_text(320.0f, 450.0f, 19.0f, TGG_UI_TEXT_GOLD, TGG_ALIGN_CENTER, text);
    } else if (br.focus == FOCUS_FILTERS) {
        const tgg_ui_hint hints[] = {
            { TGG_UI_BTN_LR, "Filter" },
            { TGG_UI_BTN_A, "Next" },
            { TGG_UI_BTN_X, "Prev" },
            { TGG_UI_BTN_Y, "Reset" },
            { TGG_UI_BTN_B, "List" },
        };
        tgg_ui_footer_band();
        tgg_ui_hint_bar(hints, 5, 320.0f, TGG_UI_FOOTER_Y + TGG_UI_FOOTER_H * 0.5f, 16.0f);
    } else if (br.focus == FOCUS_DELETE_BTN) {
        const tgg_ui_hint hints[] = {
            { TGG_UI_BTN_A, "Delete replay" },
            { TGG_UI_BTN_B, "Back to list" },
        };
        tgg_ui_footer_hints(hints, 2);
    } else {
        const int idx = list_selected_entry();
        const int dir = idx >= 0 && br.entries[idx].is_dir;
        const tgg_ui_hint hints[] = {
            { TGG_UI_BTN_A, dir ? "Open" : "Play" },
            { TGG_UI_BTN_Z, "Filter" },
            { TGG_UI_BTN_X, "Order" },
            { TGG_UI_BTN_Y, "Folder" },
            { TGG_UI_BTN_L, "" },
            { TGG_UI_BTN_R, "Page" },
            { TGG_UI_BTN_B, "Back" },
        };
        tgg_ui_footer_band();
        tgg_ui_hint_bar(hints, 7, 320.0f, TGG_UI_FOOTER_Y + TGG_UI_FOOTER_H * 0.5f, 15.5f);
    }
}
