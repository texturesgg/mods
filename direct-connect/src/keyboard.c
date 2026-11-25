/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Typing the host's address: an on-screen keyboard for a controller, and the
 * real keyboard too. */
#include "dc.h"
#include "ui.h"

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
        tgg_sound_menu(TGG_SOUND_MOVE);
    } else {
        tgg_sound_menu(TGG_SOUND_ERROR);
    }
}

static int erase(void)
{
    const size_t len = strlen(dc.address);
    if (len == 0) {
        return 0;
    }
    dc.address[len - 1] = '\0';
    tgg_sound_menu(TGG_SOUND_BACK);
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
            tgg_sound_menu(TGG_SOUND_BACK);
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
        tgg_sound_menu(TGG_SOUND_MOVE);
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

void keyboard_draw(void)
{
    /* The address being typed */
    tgg_ui_modal(70.0f, 94.0f, 500.0f, 50.0f);
    /* The text, centered, and a caret after it */
    const float width = tgg_draw_text_width(28.0f, dc.address);
    tgg_draw_text(316.0f, 105.0f, 28.0f, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER, dc.address);
    tgg_draw_rect(316.0f + width * 0.5f + 3.0f, 107.0f, 3.0f, 26.0f, TGG_UI_TEXT_GOLD);
    char full[ADDRESS_MAX + 8];
    dc_full_address(dc.address, full, sizeof full);
    if (dc.address[0] != '\0' && strcmp(full, dc.address) != 0) {
        char note[ADDRESS_MAX + 24];
        snprintf(note, sizeof note, "Connects to %s", full);
        tgg_draw_text(320.0f, 151.0f, 17.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER, note);
    }

    const float key_w = 46.0f, key_h = 41.0f, gap = 5.0f;
    const float x0 = 320.0f - (COLS * (key_w + gap) - gap) * 0.5f;
    const float y0 = 178.0f;
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            const int k = r * COLS + c;
            const float x = x0 + (float) c * (key_w + gap);
            const float y = y0 + (float) r * (key_h + gap);
            const int on = k == dc.key;
            tgg_ui_row(x, y, key_w, key_h, on);
            char label[2] = { layout[r][c], '\0' };
            tgg_draw_text(x + key_w * 0.5f, y + 9.0f, 23.0f,
                          on ? TGG_UI_TEXT_INK : TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER, label);
        }
    }
    const float y = y0 + (float) ROWS * (key_h + gap);
    const float half = (COLS * (key_w + gap) - gap - gap) * 0.5f;
    const int del = dc.key == KEY_DELETE, go = dc.key == KEY_CONNECT;
    tgg_ui_row(x0, y, half, key_h, del);
    tgg_draw_text(x0 + half * 0.5f, y + 10.0f, 21.0f,
                  del ? TGG_UI_TEXT_INK : TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER, "Delete");
    tgg_ui_row(x0 + half + gap, y, half, key_h, go);
    tgg_draw_text(x0 + half + gap + half * 0.5f, y + 10.0f, 21.0f,
                  go ? TGG_UI_TEXT_INK : TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER, "Connect");
}
