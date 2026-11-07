/* Typing the host's address: an on-screen keyboard for a controller, and the
 * real keyboard too. */
#include "dc.h"

#include "tgg/draw.h"
#include "tgg/input.h"
#include "tgg/screen.h"

#include <stdio.h>
#include <string.h>

#define COLS 10
#define ROWS 4
/* The last row's two wide keys */
#define KEY_DELETE (COLS * ROWS)
#define KEY_CONNECT (COLS * ROWS + 1)
#define KEYS (COLS * ROWS + 2)

static const char layout[ROWS][COLS + 1] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl.",
    "zxcvbnm-:_",
};

static void type(char c)
{
    const size_t len = strlen(dc.address);
    if (len + 1 < sizeof dc.address) {
        dc.address[len] = c;
        dc.address[len + 1] = '\0';
        tgg_menu_sound(TGG_SOUND_MOVE);
    } else {
        tgg_menu_sound(TGG_SOUND_ERROR);
    }
}

static int erase(void)
{
    const size_t len = strlen(dc.address);
    if (len == 0) {
        return 0;
    }
    dc.address[len - 1] = '\0';
    tgg_menu_sound(TGG_SOUND_BACK);
    return 1;
}

/* The real keyboard's letters, digits and the address's punctuation */
static void typed_keys(void)
{
    const int shift = tgg_key_down(TGG_KEY_LSHIFT) || tgg_key_down(TGG_KEY_RSHIFT);
    for (int i = 0; i < 26; i++) {
        if (tgg_key_pressed(TGG_KEY_A + i)) {
            type((char) ('a' + i));
        }
    }
    for (int i = 0; i < 9; i++) {
        if (tgg_key_pressed(TGG_KEY_1 + i)) {
            type((char) ('1' + i));
        }
    }
    if (tgg_key_pressed(TGG_KEY_0)) {
        type('0');
    }
    if (tgg_key_pressed(TGG_KEY_PERIOD)) {
        type('.');
    }
    if (tgg_key_pressed(TGG_KEY_MINUS)) {
        type(shift ? '_' : '-');
    }
    /* The semicolon key, shifted, is the colon on most layouts */
    if (tgg_key_pressed(51)) {
        type(':');
    }
}

int keyboard_frame(void)
{
    typed_keys();
    const uint32_t in = tgg_menu_input();
    /* Enter and Backspace come through the menu bits too: Enter is accept,
     * Backspace is back */
    if (tgg_key_pressed(TGG_KEY_RETURN) || (in & TGG_MENU_START)) {
        return 1;
    }
    if (in & TGG_MENU_BACK) {
        if (!erase()) {
            tgg_menu_sound(TGG_SOUND_BACK);
            return -1;
        }
        return 0;
    }
    int row = dc.key < KEY_DELETE ? dc.key / COLS : ROWS;
    int col = dc.key < KEY_DELETE ? dc.key % COLS : (dc.key == KEY_DELETE ? 2 : 7);
    int moved = 0;
    if (in & TGG_MENU_UP) {
        row = (row + ROWS) % (ROWS + 1);
        moved = 1;
    }
    if (in & TGG_MENU_DOWN) {
        row = (row + 1) % (ROWS + 1);
        moved = 1;
    }
    if (in & TGG_MENU_LEFT) {
        col = row == ROWS ? (col < 5 ? 7 : 2) : (col + COLS - 1) % COLS;
        moved = 1;
    }
    if (in & TGG_MENU_RIGHT) {
        col = row == ROWS ? (col < 5 ? 7 : 2) : (col + 1) % COLS;
        moved = 1;
    }
    if (moved) {
        dc.key = row == ROWS ? (col < 5 ? KEY_DELETE : KEY_CONNECT) : row * COLS + col;
        tgg_menu_sound(TGG_SOUND_MOVE);
    }
    if (in & TGG_MENU_ACCEPT) {
        if (dc.key == KEY_CONNECT) {
            return 1;
        }
        if (dc.key == KEY_DELETE) {
            erase();
        } else if (dc.key >= 0 && dc.key < KEY_DELETE) {
            type(layout[dc.key / COLS][dc.key % COLS]);
        }
    }
    if (in & TGG_MENU_X) {
        erase();
    }
    return 0;
}

#define WHITE TGG_RGBA(255, 255, 255, 255)
#define GOLD TGG_RGBA(255, 200, 0, 255)
#define DIM TGG_RGBA(170, 180, 210, 255)
#define INK TGG_RGBA(20, 20, 30, 255)
#define PANEL TGG_RGBA(10, 12, 40, 200)

void keyboard_draw(void)
{
    /* The address being typed */
    tgg_draw_rect(70, 96, 500, 48, PANEL);
    /* The text, centered, and a caret after it */
    const float width = tgg_text_width(28, dc.address);
    tgg_draw_text(320 - 4, 106, 28, WHITE, TGG_ALIGN_CENTER, dc.address);
    tgg_draw_rect(320 - 4 + width / 2 + 3, 108, 3, 28, GOLD);
    char full[ADDRESS_MAX + 8];
    dc_full_address(dc.address, full, sizeof full);
    if (dc.address[0] != '\0' && strcmp(full, dc.address) != 0) {
        char note[ADDRESS_MAX + 24];
        snprintf(note, sizeof note, "Connects to %s", full);
        tgg_draw_text(320, 150, 18, DIM, TGG_ALIGN_CENTER, note);
    }

    const float key_w = 46, key_h = 42, gap = 4;
    const float x0 = 320 - (COLS * (key_w + gap) - gap) / 2;
    const float y0 = 180;
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            const int k = r * COLS + c;
            const float x = x0 + c * (key_w + gap), y = y0 + r * (key_h + gap);
            const int on = k == dc.key;
            tgg_draw_rect(x, y, key_w, key_h, on ? GOLD : PANEL);
            char label[2] = { layout[r][c], '\0' };
            tgg_draw_text(x + key_w / 2, y + 9, 24, on ? INK : WHITE, TGG_ALIGN_CENTER, label);
        }
    }
    const float y = y0 + ROWS * (key_h + gap);
    const float half = (COLS * (key_w + gap) - gap - gap) / 2;
    const int del = dc.key == KEY_DELETE, go = dc.key == KEY_CONNECT;
    tgg_draw_rect(x0, y, half, key_h, del ? GOLD : PANEL);
    tgg_draw_text(x0 + half / 2, y + 10, 22, del ? INK : WHITE, TGG_ALIGN_CENTER, "Delete");
    tgg_draw_rect(x0 + half + gap, y, half, key_h, go ? GOLD : PANEL);
    tgg_draw_text(x0 + half + gap + half / 2, y + 10, 22, go ? INK : WHITE, TGG_ALIGN_CENTER,
                  "Connect");
}
