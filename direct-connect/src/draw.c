/* The screen, in the menus' look: the title in a dark band, rows on dark
 * blue panels, the selected one gold, the buttons' help at the bottom. */
#include "dc.h"

#include "tgg/draw.h"

#include <stdio.h>
#include <string.h>

#define WHITE TGG_RGBA(255, 255, 255, 255)
#define GOLD TGG_RGBA(255, 200, 0, 255)
#define DIM TGG_RGBA(170, 180, 210, 255)
#define INK TGG_RGBA(20, 20, 30, 255)
#define PANEL TGG_RGBA(10, 12, 40, 200)
#define BAND TGG_RGBA(0, 0, 0, 130)
#define RED TGG_RGBA(230, 70, 60, 255)
#define BLUE TGG_RGBA(80, 120, 255, 255)
#define GREEN TGG_RGBA(90, 220, 110, 255)

static void band(const char* title, const char* status)
{
    tgg_draw_rect(-200, 0, 1040, 64, BAND);
    tgg_draw_text(40, 16, 32, WHITE, TGG_ALIGN_LEFT, title);
    if (status != NULL) {
        tgg_draw_text(600, 24, 18, DIM, TGG_ALIGN_RIGHT, status);
    }
}

static void footer(const char* help)
{
    tgg_draw_rect(-200, 440, 1040, 40, BAND);
    tgg_draw_text(320, 450, 18, DIM, TGG_ALIGN_CENTER, help);
    if (dc.message_frames > 0) {
        tgg_draw_rect(60, 396, 520, 34, PANEL);
        tgg_draw_text(320, 404, 18, GOLD, TGG_ALIGN_CENTER, dc.message);
    }
}

static void status_text(char* out, int cap)
{
    netplay_info info;
    netplay_status(&info);
    if (info.rtt_ms > 0) {
        snprintf(out, (size_t) cap, "%s  %.0f ms", info.peer, info.rtt_ms);
    } else {
        snprintf(out, (size_t) cap, "%s", info.peer);
    }
}

static void draw_menu(void)
{
    band("Direct connect", NULL);
    const char* rows[4];
    const int n = menu_rows(rows, 4);
    for (int i = 0; i < n; i++) {
        const float y = 120 + i * 56;
        const int on = i == dc.menu_sel;
        tgg_draw_rect(140, y, 360, 46, on ? GOLD : PANEL);
        char fit[96];
        tgg_text_fit(fit, sizeof fit, 24, 330, rows[i]);
        tgg_draw_text(320, y + 11, 24, on ? INK : WHITE, TGG_ALIGN_CENTER, fit);
    }
    tgg_draw_text(320, 320, 18, DIM, TGG_ALIGN_CENTER,
                  "One player hosts. The other joins with the host's address.");
    footer("A  Choose     B  Back");
}

static void draw_hosting(void)
{
    band("Hosting", NULL);
    tgg_draw_text(320, 96, 26, WHITE, TGG_ALIGN_CENTER, "Waiting for the other player");
    tgg_draw_text(320, 140, 18, DIM, TGG_ALIGN_CENTER, "Tell them one of these to join:");
    char addrs[6][48];
    const int n = local_addresses(addrs, 6);
    float y = 172;
    char line[128];
    const char* name = local_name();
    for (int i = 0; i < n && i < 4; i++) {
        tgg_draw_rect(150, y, 340, 38, PANEL);
        tgg_draw_text(320, y + 8, 24, i == 0 ? GOLD : WHITE, TGG_ALIGN_CENTER, addrs[i]);
        y += 44;
    }
    if (name[0] != '\0') {
        tgg_draw_rect(150, y, 340, 38, PANEL);
        tgg_draw_text(320, y + 8, 24, WHITE, TGG_ALIGN_CENTER, name);
        y += 44;
    }
    snprintf(line, sizeof line, "Port %d", setting_port);
    tgg_draw_text(320, y + 8, 18, DIM, TGG_ALIGN_CENTER, line);
    footer("B  Stop hosting");
}

static void draw_address(void)
{
    band("Join", NULL);
    tgg_draw_text(320, 72, 18, DIM, TGG_ALIGN_CENTER, "The host's address");
    keyboard_draw();
    footer("A  Type    X  Delete    Start  Connect    B  Back");
}

static void draw_connecting(void)
{
    band("Join", NULL);
    netplay_info info;
    netplay_status(&info);
    char line[128];
    snprintf(line, sizeof line, "Connecting to %s", info.peer[0] != '\0' ? info.peer : "...");
    tgg_draw_text(320, 200, 26, WHITE, TGG_ALIGN_CENTER, line);
    tgg_draw_text(320, 240, 18, DIM, TGG_ALIGN_CENTER, "The host has to be on its Hosting screen.");
    footer("B  Cancel");
}

static void draw_failed(void)
{
    band("Direct connect", NULL);
    tgg_draw_rect(60, 170, 520, 100, PANEL);
    char fit[160];
    tgg_text_fit(fit, sizeof fit, 20, 500, dc.why);
    tgg_draw_text(320, 190, 22, GOLD, TGG_ALIGN_CENTER, "Disconnected");
    tgg_draw_text(320, 228, 20, WHITE, TGG_ALIGN_CENTER, fit);
    footer(dc.for_netplay ? "A  Back to your game" : "A  OK");
}

/* ---- The host has other mods */

static void draw_list(float x, const char* title, char lines[][DIFF_TEXT], int n)
{
    tgg_draw_rect(x, 150, 270, 230, PANEL);
    tgg_draw_text(x + 12, 158, 18, DIM, TGG_ALIGN_LEFT, title);
    float y = 186;
    const int shown = n < DIFF_LINES ? n : DIFF_LINES;
    for (int i = 0; i < shown; i++) {
        char fit[DIFF_TEXT];
        tgg_text_fit(fit, sizeof fit, 17, 246, lines[i]);
        tgg_draw_text(x + 12, y, 17, WHITE, TGG_ALIGN_LEFT, fit);
        y += 23;
    }
    if (n > shown) {
        char more[32];
        snprintf(more, sizeof more, "and %d more", n - shown);
        tgg_draw_text(x + 12, y, 17, DIM, TGG_ALIGN_LEFT, more);
    } else if (n == 0) {
        tgg_draw_text(x + 12, y, 17, DIM, TGG_ALIGN_LEFT, "Nothing else");
    }
}

static void draw_different(void)
{
    band("Join", NULL);
    netplay_info info;
    netplay_status(&info);
    tgg_draw_text(320, 80, 24, GOLD, TGG_ALIGN_CENTER, info.why);
    if (dc.bad_profile) {
        tgg_draw_text(320, 200, 18, WHITE, TGG_ALIGN_CENTER, "Can't read the host's list of mods.");
        footer("B  Leave");
        return;
    }
    if (!dc.have_diff) {
        tgg_draw_text(320, 200, 18, DIM, TGG_ALIGN_CENTER, "Checking the host's mods...");
        footer("B  Leave");
        return;
    }
    const Diff* d = &dc.diff;
    if (strcmp(d->game_there, d->game_here) != 0) {
        char line[128];
        snprintf(line, sizeof line, "Game: %s there, %s here", d->game_there, d->game_here);
        tgg_draw_text(320, 116, 18, WHITE, TGG_ALIGN_CENTER, line);
    } else if (d->n_there == 0 && d->n_here == 0) {
        tgg_draw_text(320, 116, 18, WHITE, TGG_ALIGN_CENTER,
                      "Same versions, but the builds differ.");
    }
    draw_list(40, "The host has", dc.diff.there, d->n_there);
    draw_list(330, "This game has", dc.diff.here, d->n_here);
    if (dc.for_netplay) {
        tgg_draw_text(320, 398, 18, DIM, TGG_ALIGN_CENTER,
                      "The host's mods changed. Leave, then join again.");
        footer("B  Leave");
        return;
    }
    footer("A  Get them and restart     B  Leave");
}

/* While tgg gets the host's mods */
static void draw_restarting(void)
{
    band("Join", NULL);
    const tgg_restart_status* r = &dc.restart;
    tgg_draw_rect(60, 150, 520, 170, PANEL);
    tgg_draw_rect(60, 150, 520, 2, BLUE);
    char text[300];
    if (r->state == TGG_RESTART_NEEDS_TRUST) {
        tgg_draw_text(80, 166, 20, WHITE, TGG_ALIGN_LEFT, "The host's mods are by");
        tgg_text_fit(text, sizeof text, 20, 480, r->trust);
        tgg_draw_text(80, 194, 20, GOLD, TGG_ALIGN_LEFT, text);
        tgg_draw_text(80, 228, 18, DIM, TGG_ALIGN_LEFT, "Get them?");
        footer("A  Yes, always     B  No");
        return;
    }
    tgg_draw_text(80, 166, 20, WHITE, TGG_ALIGN_LEFT, "Getting the host's mods");
    tgg_text_fit(text, sizeof text, 18, 480, r->label);
    tgg_draw_text(80, 200, 18, DIM, TGG_ALIGN_LEFT, text);
    /* This step's bytes, when tgg knows the size */
    tgg_draw_rect(80, 234, 480, 10, TGG_RGBA(255, 255, 255, 40));
    if (r->total > 0) {
        const float done = (float) (r->bytes / r->total);
        tgg_draw_rect(80, 234, 480 * (done > 1 ? 1 : done), 10, BLUE);
    }
    if (r->steps > 0) {
        snprintf(text, sizeof text, "%d of %d", r->step + 1, r->steps);
        tgg_draw_text(560, 252, 16, DIM, TGG_ALIGN_RIGHT, text);
    }
    tgg_draw_text(80, 290, 16, DIM, TGG_ALIGN_LEFT, "Then the game restarts and joins the host.");
    footer("B  Stop");
}

/* ---- The lobby */

#define CELL 50.0f
#define GRID_X (320 - GRID_COLS * CELL / 2)
#define GRID_Y 70.0f

static void draw_player(float x, const char* who, tgg_color color, const Pick* p, int waiting)
{
    tgg_draw_rect(x, 250, 200, 144, PANEL);
    tgg_draw_rect(x, 250, 200, 6, color);
    tgg_draw_text(x + 100, 262, 20, color, TGG_ALIGN_CENTER, who);
    if (!p->known) {
        tgg_draw_text(x + 100, 320, 18, DIM, TGG_ALIGN_CENTER, waiting ? "Picking..." : "");
        return;
    }
    tgg_draw_image(tgg_image_stock_icon(p->ckind, p->costume), x + 76, 290, 48, 48,
                   TGG_RGBA(255, 255, 255, 255));
    char name[64];
    tgg_text_fit(name, sizeof name, 18, 190, fighter_name(p->ckind));
    tgg_draw_text(x + 100, 344, 18, WHITE, TGG_ALIGN_CENTER, name);
    if (p->ready) {
        tgg_draw_rect(x + 40, 366, 120, 24, GREEN);
        tgg_draw_text(x + 100, 369, 18, INK, TGG_ALIGN_CENTER, "Ready");
    }
}

static void draw_lobby(void)
{
    const Lobby* l = &dc.lobby;
    netplay_info info;
    netplay_status(&info);
    char status[96];
    status_text(status, sizeof status);
    band("Direct connect", status);

    /* The characters */
    for (int i = 0; i < GRID_CELLS; i++) {
        const int row = i / GRID_COLS, col = i % GRID_COLS;
        const float x = GRID_X + col * CELL + (row == 2 ? CELL / 2 : 0);
        const float y = GRID_Y + row * CELL;
        const int ckind = grid_ckind(i);
        const int on = i == l->cursor && !l->me.ready;
        const int mine = ckind == l->me.ckind;
        tgg_draw_rect(x + 2, y + 2, CELL - 4, CELL - 4,
                      on ? GOLD : mine ? TGG_RGBA(60, 70, 140, 230) : PANEL);
        tgg_draw_image(tgg_image_stock_icon(ckind, mine ? l->me.costume : 0), x + 9, y + 9,
                       CELL - 18, CELL - 18, TGG_RGBA(255, 255, 255, 255));
    }
    if (!l->me.ready) {
        char name[64];
        snprintf(name, sizeof name, "%s", fighter_name(grid_ckind(l->cursor)));
        tgg_draw_text(320, GRID_Y + 3 * CELL + 2, 18, GOLD, TGG_ALIGN_CENTER, name);
    }

    /* The players: the host is port 1, red; the other port 2, blue */
    const int me_first = info.side == 0;
    draw_player(me_first ? 16 : 424, me_first ? "You  P1" : "You  P2", me_first ? RED : BLUE,
                &l->me, 0);
    draw_player(me_first ? 424 : 16, me_first ? "Them  P2" : "Them  P1", me_first ? BLUE : RED,
                &l->them, 1);

    /* The stage and rules, the host's */
    tgg_draw_rect(226, 250, 188, 144, PANEL);
    tgg_draw_text(320, 256, 18, DIM, TGG_ALIGN_CENTER, info.side == 0 ? "Stage  L / R" : "Stage");
    const tgg_image* icon = tgg_image_stage(stage_kind(l->stage));
    if (icon != NULL) {
        tgg_draw_image(icon, 288, 280, 64, 56, TGG_RGBA(255, 255, 255, 255));
    }
    char stage[64];
    tgg_text_fit(stage, sizeof stage, 18, 180, stage_name(l->stage));
    tgg_draw_text(320, 340, 18, WHITE, TGG_ALIGN_CENTER, stage);
    char rules[64];
    if (l->minutes != 0) {
        snprintf(rules, sizeof rules, "%d stocks  %d:00", l->stocks, l->minutes);
    } else {
        snprintf(rules, sizeof rules, "%d stocks", l->stocks);
    }
    tgg_draw_text(320, 366, 18, DIM, TGG_ALIGN_CENTER, rules);

    if (l->me.ready) {
        footer(l->them.ready ? "Starting..." : "Waiting for the other player     B  Leave");
    } else {
        footer("A  Pick    X / Y  Color    Start  Ready    B  Leave");
    }
}

void dc_draw(void)
{
    switch (dc.view) {
    case VIEW_MENU:
        draw_menu();
        break;
    case VIEW_HOSTING:
        draw_hosting();
        break;
    case VIEW_ADDRESS:
        draw_address();
        break;
    case VIEW_CONNECTING:
        draw_connecting();
        break;
    case VIEW_LOBBY:
        draw_lobby();
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
    }
}
