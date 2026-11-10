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
    band("Online", NULL);
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
                  "One player hosts. Up to three join with the host's address.");
    footer("A  Choose     B  Back");
}

static void draw_hosting(void)
{
    band("Hosting", NULL);
    netplay_info info;
    netplay_status(&info);
    char count[64];
    if (info.players < 2) {
        snprintf(count, sizeof count, "Waiting for players");
    } else {
        snprintf(count, sizeof count, "%d players in", info.players);
    }
    tgg_draw_text(320, 96, 26, WHITE, TGG_ALIGN_CENTER, count);
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
    if (info.locked) {
        footer("Start  Play    B  Leave");
    } else if (info.players >= 2) {
        footer("Start  Play    B  Stop hosting");
    } else {
        footer("B  Stop hosting");
    }
}

static void draw_waiting(void)
{
    band("Online", NULL);
    netplay_info info;
    netplay_status(&info);
    char line[128];
    snprintf(line, sizeof line, "Connected to %s", info.peer);
    tgg_draw_text(320, 180, 22, DIM, TGG_ALIGN_CENTER, line);
    tgg_draw_text(320, 220, 28, WHITE, TGG_ALIGN_CENTER,
                  info.locked ? "The host is choosing" : "Waiting for the host to start");
    if (info.players >= 2) {
        snprintf(line, sizeof line, "%d players in", info.players);
        tgg_draw_text(320, 268, 18, DIM, TGG_ALIGN_CENTER, line);
    }
    footer("B  Leave");
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
    band("Online", NULL);
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
    case VIEW_WAITING:
        draw_waiting();
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
