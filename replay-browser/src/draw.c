/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The screen: the list on the left, the selected replay on the right, in
 * the game's font, colors and pictures, over the menus' background. */
#include "browser.h"

#include "tgg/content.h"
#include "tgg/draw.h"

#include <stdio.h>
#include <string.h>

#define WHITE TGG_RGBA(255, 255, 255, 255)
#define GOLD TGG_RGBA(255, 200, 0, 255)
#define DIM TGG_RGBA(170, 180, 210, 255)
#define INK TGG_RGBA(20, 20, 30, 255)
#define PANEL TGG_RGBA(10, 12, 40, 200)
#define BAND TGG_RGBA(0, 0, 0, 130)
#define BLUE TGG_RGBA(90, 115, 255, 220)

#define LIST_X 24.0f
#define LIST_W 300.0f
#define TOP 84.0f
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
    tgg_draw_rect(LIST_X, y + 2, LIST_W, ROW_H - 4, on ? GOLD : PANEL);
    const tgg_color fg = on ? INK : WHITE;
    char text[256];
    if (e->is_dir) {
        tgg_text_fit(text, sizeof text, 20, LIST_W - 40,
                     strcmp(e->name, "..") == 0 ? "Up a folder" : e->name);
        tgg_draw_text(LIST_X + 12, y + 9, 20, on ? INK : DIM, TGG_ALIGN_LEFT, text);
        tgg_draw_text(LIST_X + LIST_W - 12, y + 9, 20, on ? INK : DIM, TGG_ALIGN_RIGHT,
                      strcmp(e->name, "..") == 0 ? "<" : ">");
        return;
    }
    const replay_summary* s = list_summary(i);
    if (s == NULL) {
        tgg_text_fit(text, sizeof text, 18, LIST_W - 24, e->name);
        tgg_draw_text(LIST_X + 12, y + 10, 18, on ? INK : DIM, TGG_ALIGN_LEFT, text);
        return;
    }
    short_date(s->date, text, sizeof text);
    tgg_draw_text(LIST_X + 12, y + 10, 19, fg, TGG_ALIGN_LEFT, text);
    /* The fighters' stock icons, the winner's bright */
    float x = LIST_X + LIST_W - 10;
    for (int p = 3; p >= 0; p--) {
        const replay_player_slot* pl = &s->players[p];
        if (!pl->present) {
            continue;
        }
        x -= 26;
        const int lost = s->winner >= 0 && pl->placement != 0 &&
                         !(s->teams && pl->team == s->players[s->winner].team);
        tgg_draw_image(tgg_image_stock_icon(tgg_character_id(pl->ckind), pl->costume), x, y + 7, 24, 24,
                       lost ? TGG_RGBA(255, 255, 255, 110) : WHITE);
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
        tgg_draw_rect(LIST_X, TOP, LIST_W, ROW_H * 2, PANEL);
        tgg_draw_text(LIST_X + LIST_W / 2, TOP + 26, 20, DIM, TGG_ALIGN_CENTER, "No replays here");
        return;
    }
    for (int i = br.top; i < br.n && i < br.top + LIST_ROWS; i++) {
        draw_row(i, TOP + ROW_H * (float) (i - br.top));
    }
    /* Where the rows shown are in the folder */
    if (br.n > LIST_ROWS) {
        const float h = ROW_H * LIST_ROWS;
        const float bar = h * LIST_ROWS / (float) br.n;
        const float at = (h - bar) * (float) br.top / (float) (br.n - LIST_ROWS);
        tgg_draw_rect(LIST_X + LIST_W + 4, TOP + 2, 4, h - 4, TGG_RGBA(0, 0, 0, 120));
        tgg_draw_rect(LIST_X + LIST_W + 4, TOP + 2 + at, 4, bar - 4 > 4 ? bar - 4 : 4, BLUE);
    }
}

static void draw_preview(void)
{
    const float x = PREVIEW_X, w = PREVIEW_W;
    tgg_draw_rect(x, TOP + 2, w, ROW_H * LIST_ROWS - 4, PANEL);
    const replay_summary* s = br.n > 0 ? list_summary(br.sel) : NULL;
    if (s == NULL) {
        const char* what = br.n > 0 && br.entries[br.sel].is_dir ? "A folder" : "";
        if (br.n > 0 && !br.entries[br.sel].is_dir) {
            what = "Not a replay this game can read";
        }
        tgg_draw_text(x + w / 2, TOP + 150, 20, DIM, TGG_ALIGN_CENTER, what);
        return;
    }

    /* The stage: its icon from the stage select, and its name */
    float y = TOP + 12;
    const tgg_image* icon = tgg_image_stage(tgg_stage_id(s->stage));
    const tgg_image* name = tgg_image_stage_name(tgg_stage_id(s->stage));
    char text[160];
    if (icon != NULL) {
        tgg_draw_rect(x + 11, y - 1, 82, 72, TGG_RGBA(90, 115, 255, 160));
        tgg_draw_image(icon, x + 12, y, 80, 70, WHITE);
    }
    if (name != NULL) {
        tgg_draw_image(name, x + 98, y + 12, 168, 42, GOLD);
    } else {
        tgg_text_fit(text, sizeof text, 20, w - 24, stage_name(s->stage));
        tgg_draw_text(x + 98, y + 24, 20, GOLD, TGG_ALIGN_LEFT, text);
    }
    y += 84;

    /* The players, winner first marked */
    for (int p = 0; p < 4; p++) {
        const replay_player_slot* pl = &s->players[p];
        if (!pl->present) {
            continue;
        }
        tgg_draw_image(tgg_image_stock_icon(tgg_character_id(pl->ckind), pl->costume), x + 12, y + 2, 30, 30, WHITE);
        const int won = s->winner >= 0 &&
                        (p == s->winner || (s->teams && pl->team == s->players[s->winner].team));
        /* Room for "Winner" on the right */
        const float room = won ? w - 150 : w - 62;
        tgg_text_fit(text, sizeof text, 19, room, who(pl));
        tgg_draw_text(x + 50, y, 19, WHITE, TGG_ALIGN_LEFT, text);
        char sub[80];
        if (pl->cpu) {
            snprintf(sub, sizeof sub, "%s (CPU)", fighter_name(pl->ckind));
        } else if (pl->name[0] != '\0' && pl->code[0] != '\0') {
            snprintf(sub, sizeof sub, "%s  %s", fighter_name(pl->ckind), pl->code);
        } else {
            snprintf(sub, sizeof sub, "P%d  %s", p + 1, fighter_name(pl->ckind));
        }
        tgg_text_fit(text, sizeof text, 15, room, sub);
        tgg_draw_text(x + 50, y + 20, 15, DIM, TGG_ALIGN_LEFT, text);
        if (won) {
            tgg_draw_text(x + w - 12, y + 8, 19, GOLD, TGG_ALIGN_RIGHT, "Winner");
        }
        y += 40;
    }

    /* How it went */
    y = TOP + ROW_H * LIST_ROWS - 66;
    tgg_draw_rect(x + 12, y - 6, w - 24, 2, BLUE);
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
    tgg_text_fit(text, sizeof text, 18, w - 24, line);
    tgg_draw_text(x + 12, y, 18, WHITE, TGG_ALIGN_LEFT, text);
    long_date(s->date, text, sizeof text);
    tgg_draw_text(x + 12, y + 25, 15, DIM, TGG_ALIGN_LEFT, text);
    snprintf(line, sizeof line, "Slippi %d.%d.%d", s->version[0], s->version[1], s->version[2]);
    tgg_draw_text(x + w - 12, y + 25, 15, DIM, TGG_ALIGN_RIGHT, line);
}

/* Over the preview while tgg gets what a replay needs */
static void draw_restart(void)
{
    const tgg_restart_status* r = &br.restart;
    const float x = PREVIEW_X, w = PREVIEW_W, y = TOP + 60, h = 170;
    tgg_draw_rect(x, y, w, h, TGG_RGBA(10, 12, 40, 240));
    tgg_draw_rect(x, y, w, 2, BLUE);
    char text[300];
    if (r->state == TGG_RESTART_NEEDS_TRUST) {
        tgg_draw_text(x + 14, y + 14, 19, WHITE, TGG_ALIGN_LEFT, "It was played with mods by");
        tgg_text_fit(text, sizeof text, 19, w - 28, r->trust);
        tgg_draw_text(x + 14, y + 40, 19, GOLD, TGG_ALIGN_LEFT, text);
        tgg_draw_text(x + 14, y + 72, 16, DIM, TGG_ALIGN_LEFT, "Get them to play it back?");
        tgg_draw_text(x + 14, y + h - 30, 16, DIM, TGG_ALIGN_LEFT, "A: Yes, always    B: No");
        return;
    }
    tgg_draw_text(x + 14, y + 14, 19, WHITE, TGG_ALIGN_LEFT, "Getting what it was played with");
    tgg_text_fit(text, sizeof text, 16, w - 28, r->label);
    tgg_draw_text(x + 14, y + 48, 16, DIM, TGG_ALIGN_LEFT, text);
    /* This step's bytes, when tgg knows the size */
    tgg_draw_rect(x + 14, y + 80, w - 28, 10, TGG_RGBA(255, 255, 255, 40));
    if (r->total > 0) {
        const float done = (float) (r->bytes / r->total);
        tgg_draw_rect(x + 14, y + 80, (w - 28) * (done > 1 ? 1 : done), 10, BLUE);
    }
    if (r->steps > 0) {
        snprintf(text, sizeof text, "%d of %d", r->step + 1, r->steps);
        tgg_draw_text(x + w - 14, y + 98, 15, DIM, TGG_ALIGN_RIGHT, text);
    }
    tgg_draw_text(x + 14, y + h - 30, 16, DIM, TGG_ALIGN_LEFT, "The game restarts to play it.  B: Stop");
}

void browser_draw(void)
{
    /* The title, and the folder shown */
    const tgg_rect b = tgg_draw_bounds();
    tgg_draw_rect(b.x, 22, b.w, 52, BAND);
    tgg_draw_text(40, 30, 40, TGG_RGBA(200, 205, 225, 255), TGG_ALIGN_LEFT, "Replays");
    char text[PATH_MAX_LEN];
    tgg_text_fit_end(text, sizeof text, 16, 380, br.folder);
    tgg_draw_text(608, 42, 16, DIM, TGG_ALIGN_RIGHT, text);

    draw_list();
    draw_preview();
    if (br.restarting) {
        draw_restart();
    }

    tgg_draw_rect(b.x, 440, b.w, 40, BAND);
    if (br.message_frames > 0) {
        tgg_text_fit(text, sizeof text, 20, 600, br.message);
        tgg_draw_text(320, 450, 20, GOLD, TGG_ALIGN_CENTER, text);
    } else {
        const int dir = br.n > 0 && br.entries[br.sel].is_dir;
        tgg_draw_text(320, 450, 19, DIM, TGG_ALIGN_CENTER,
                      dir ? "A: Open    Y: Use this folder    X: Order    B: Back"
                          : "A: Play    Y: Use this folder    X: Order    B: Back");
    }
}
