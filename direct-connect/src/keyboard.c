/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Typing a host address or a 4-letter room code: an on-screen keyboard for a
 * controller, real keyboard input, and quick-select chips for recent rooms. */
#include "dc.h"
#include "ui.h"

#include "tgg/draw.h"
#include "tgg/input.h"
#include "tgg/screen.h"

#include <stdio.h>
#include <string.h>

/* ---- Host address keyboard (10x4 + Delete / Connect) */

#define COLS 10
#define ROWS 4
#define KEY_DELETE (COLS * ROWS)
#define KEY_CONNECT (COLS * ROWS + 1)

static const char addr_layout[ROWS][COLS + 1] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl.",
    "zxcvbnm-:_",
};

/* ---- Room code keyboard (8x3 valid letters without I/L/O + Clear, Delete / Join) */

#define CODE_COLS 8
#define CODE_ROWS 3
#define CODE_KEY_CLEAR (CODE_COLS * CODE_ROWS - 1) /* row 2, col 7 */
#define CODE_KEY_DELETE (CODE_COLS * CODE_ROWS)
#define CODE_KEY_JOIN (CODE_COLS * CODE_ROWS + 1)
#define RECENT_VISIBLE 4

static const char code_layout[CODE_ROWS][CODE_COLS + 1] = {
    "QWERTYUP",
    "ASDFGHJK",
    "ZXCVBNM ",
};

static void type_addr(char c)
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

static void type_code_char(char c)
{
    if (c >= 'a' && c <= 'z') {
        c = (char) (c - 32);
    }
    if (!dc_is_room_char(c)) {
        dc.code_hint_frames = 150;
        tgg_sound_menu(TGG_SOUND_ERROR);
        return;
    }
    const size_t len = strlen(dc.address);
    if (len < 4) {
        dc.address[len] = c;
        dc.address[len + 1] = '\0';
        dc.code_hint_frames = 0;
        dc.recent_focus = 0;
        if (len + 1 == 4) {
            dc.key = CODE_KEY_JOIN;
        }
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
static void typed_addr_keys(void)
{
    const int shift = tgg_key_down(TGG_KEY_LSHIFT) || tgg_key_down(TGG_KEY_RSHIFT);
    for (int i = 0; i < 26; i++) {
        if (tgg_key_pressed(TGG_KEY_A + i)) {
            type_addr((char) ('a' + i));
        }
    }
    for (int i = 0; i < 9; i++) {
        if (tgg_key_pressed(TGG_KEY_1 + i)) {
            type_addr((char) ('1' + i));
        }
    }
    if (tgg_key_pressed(TGG_KEY_0)) {
        type_addr('0');
    }
    if (tgg_key_pressed(TGG_KEY_PERIOD)) {
        type_addr('.');
    }
    if (tgg_key_pressed(TGG_KEY_MINUS)) {
        type_addr(shift ? '_' : '-');
    }
    /* The semicolon key, shifted, is the colon on most layouts */
    if (tgg_key_pressed(51)) {
        type_addr(':');
    }
}

static void typed_code_keys(void)
{
    for (int i = 0; i < 26; i++) {
        if (tgg_key_pressed(TGG_KEY_A + i)) {
            type_code_char((char) ('A' + i));
        }
    }
}

static int visible_recent(void)
{
    const int n = recent_count();
    return n > RECENT_VISIBLE ? RECENT_VISIBLE : n;
}

static int code_frame(void)
{
    if (dc.code_hint_frames > 0) {
        dc.code_hint_frames--;
    }
    typed_code_keys();
    const uint32_t in = tgg_menu_input();
    const int n_recent = visible_recent();
    if (n_recent == 0) {
        dc.recent_focus = 0;
    } else if (dc.recent_idx >= n_recent) {
        dc.recent_idx = n_recent - 1;
    }

    if (tgg_key_pressed(TGG_KEY_RETURN) || (in & TGG_MENU_START)) {
        if (dc.address[0] == '\0' && dc.recent_focus && n_recent > 0) {
            const RecentRoom* r = recent_get(dc.recent_idx);
            if (r != NULL) {
                snprintf(dc.address, sizeof dc.address, "%s", r->code);
            }
        }
        return 1;
    }
    if (in & TGG_MENU_BACK) {
        if (!erase()) {
            tgg_sound_menu(TGG_SOUND_BACK);
            return -1;
        }
        return 0;
    }
    if (in & TGG_MENU_X) {
        erase();
        return 0;
    }

    if (dc.recent_focus) {
        if (in & TGG_MENU_LEFT) {
            dc.recent_idx = (dc.recent_idx + n_recent - 1) % n_recent;
            tgg_sound_menu(TGG_SOUND_MOVE);
        } else if (in & TGG_MENU_RIGHT) {
            dc.recent_idx = (dc.recent_idx + 1) % n_recent;
            tgg_sound_menu(TGG_SOUND_MOVE);
        } else if (in & TGG_MENU_DOWN) {
            dc.recent_focus = 0;
            const int col = (dc.recent_idx * CODE_COLS) / n_recent;
            dc.key = col;
            tgg_sound_menu(TGG_SOUND_MOVE);
        } else if (in & TGG_MENU_UP) {
            dc.recent_focus = 0;
            dc.key = dc.recent_idx * 2 < n_recent ? CODE_KEY_DELETE : CODE_KEY_JOIN;
            tgg_sound_menu(TGG_SOUND_MOVE);
        } else if (in & TGG_MENU_ACCEPT) {
            const RecentRoom* r = recent_get(dc.recent_idx);
            if (r != NULL) {
                snprintf(dc.address, sizeof dc.address, "%s", r->code);
                return 1;
            }
        }
        return 0;
    }

    int row = dc.key < CODE_KEY_DELETE ? dc.key / CODE_COLS : CODE_ROWS;
    int col = dc.key < CODE_KEY_DELETE ? dc.key % CODE_COLS : (dc.key == CODE_KEY_DELETE ? 2 : 6);
    int moved = 0;
    if (in & TGG_MENU_UP) {
        if (row == 0 && n_recent > 0) {
            dc.recent_focus = 1;
            dc.recent_idx = (col * n_recent) / CODE_COLS;
            tgg_sound_menu(TGG_SOUND_MOVE);
            return 0;
        }
        row = (row + CODE_ROWS) % (CODE_ROWS + 1);
        moved = 1;
    }
    if (in & TGG_MENU_DOWN) {
        if (row == CODE_ROWS && n_recent > 0) {
            dc.recent_focus = 1;
            dc.recent_idx = dc.key == CODE_KEY_DELETE ? 0 : n_recent - 1;
            tgg_sound_menu(TGG_SOUND_MOVE);
            return 0;
        }
        row = (row + 1) % (CODE_ROWS + 1);
        moved = 1;
    }
    if (in & TGG_MENU_LEFT) {
        col = row == CODE_ROWS ? (col < 4 ? 6 : 2) : (col + CODE_COLS - 1) % CODE_COLS;
        moved = 1;
    }
    if (in & TGG_MENU_RIGHT) {
        col = row == CODE_ROWS ? (col < 4 ? 6 : 2) : (col + 1) % CODE_COLS;
        moved = 1;
    }
    if (moved) {
        dc.key = row == CODE_ROWS ? (col < 4 ? CODE_KEY_DELETE : CODE_KEY_JOIN)
                                  : row * CODE_COLS + col;
        tgg_sound_menu(TGG_SOUND_MOVE);
    }
    if (in & TGG_MENU_ACCEPT) {
        if (dc.key == CODE_KEY_JOIN) {
            return 1;
        }
        if (dc.key == CODE_KEY_DELETE) {
            erase();
        } else if (dc.key == CODE_KEY_CLEAR) {
            if (dc.address[0] != '\0') {
                dc.address[0] = '\0';
                tgg_sound_menu(TGG_SOUND_BACK);
            }
        } else if (dc.key >= 0 && dc.key < CODE_KEY_CLEAR) {
            type_code_char(code_layout[dc.key / CODE_COLS][dc.key % CODE_COLS]);
        }
    }
    return 0;
}

static int addr_frame(void)
{
    typed_addr_keys();
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
            type_addr(addr_layout[dc.key / COLS][dc.key % COLS]);
        }
    }
    if (in & TGG_MENU_X) {
        erase();
    }
    return 0;
}

int keyboard_frame(void)
{
    return dc.typing_code ? code_frame() : addr_frame();
}

static void draw_code_keyboard(void)
{
    const size_t len = strlen(dc.address);
    const int n_recent = visible_recent();

    /* Four room-code character slots */
    const float slot_w = 56.0f, slot_h = 50.0f, slot_gap = 10.0f;
    const float slots_w = 4.0f * slot_w + 3.0f * slot_gap;
    const float sx0 = 320.0f - slots_w * 0.5f;
    const float sy = 88.0f;
    for (int i = 0; i < 4; i++) {
        const float x = sx0 + (float) i * (slot_w + slot_gap);
        const int filled = (size_t) i < len;
        const int active = !dc.recent_focus && (size_t) i == len;
        tgg_ui_rounded_rect(x, sy, slot_w, slot_h, 8.0f,
                            filled ? TGG_UI_BG_PANEL_DEEP : TGG_UI_BG_PANEL,
                            active ? TGG_UI_TEXT_GOLD
                                   : (filled ? TGG_UI_BORDER_BRIGHT : TGG_UI_BORDER_SUBTLE),
                            active ? 1.75f : 1.25f);
        if (filled) {
            char ch[2] = { dc.address[i], '\0' };
            tgg_draw_text(x + slot_w * 0.5f, sy + 7.0f, 34.0f, TGG_UI_TEXT_GOLD,
                          TGG_ALIGN_CENTER, ch);
        } else if (active) {
            tgg_draw_rect(x + 16.0f, sy + slot_h - 10.0f, slot_w - 32.0f, 2.5f, TGG_UI_TEXT_GOLD);
        }
    }

    if (dc.code_hint_frames > 0) {
        tgg_draw_text(320.0f, 144.0f, 14.0f, TGG_UI_TEXT_GOLD, TGG_ALIGN_CENTER,
                      "Room codes do not use I, L, or O.");
    }

    const float key_w = 54.0f, key_h = 35.0f, gap = 5.0f;
    const float grid_w = CODE_COLS * (key_w + gap) - gap;
    const float x0 = 320.0f - grid_w * 0.5f;

    /* Quick-select recent rooms strip above the keyboard */
    float y0 = 184.0f;
    if (n_recent > 0) {
        tgg_draw_text(x0, 165.0f, 13.5f, TGG_UI_TEXT_MUTED, TGG_ALIGN_LEFT, "Recent rooms");
        const float chip_gap = 8.0f;
        const float chip_w = (grid_w - chip_gap * (float) (RECENT_VISIBLE - 1)) /
                             (float) RECENT_VISIBLE;
        const float chip_h = 34.0f;
        const float cy = 184.0f;
        for (int i = 0; i < n_recent; i++) {
            const RecentRoom* r = recent_get(i);
            const float cx = x0 + (float) i * (chip_w + chip_gap);
            const int on = dc.recent_focus && dc.recent_idx == i;
            tgg_ui_row(cx, cy, chip_w, chip_h, on);
            if (r->label[0] != '\0') {
                char fit[32];
                tgg_draw_text(cx + chip_w * 0.5f, cy + 2.0f, 15.5f,
                              on ? TGG_UI_TEXT_INK : TGG_UI_TEXT_GOLD, TGG_ALIGN_CENTER, r->code);
                tgg_draw_text_fit(fit, sizeof fit, 11.5f, chip_w - 12.0f, r->label);
                tgg_draw_text(cx + chip_w * 0.5f, cy + 19.0f, 11.5f,
                              on ? TGG_UI_TEXT_INK : TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER, fit);
            } else {
                tgg_draw_text(cx + chip_w * 0.5f, cy + 7.5f, 18.0f,
                              on ? TGG_UI_TEXT_INK : TGG_UI_TEXT_GOLD, TGG_ALIGN_CENTER, r->code);
            }
        }
        y0 = 232.0f;
    }

    for (int r = 0; r < CODE_ROWS; r++) {
        for (int c = 0; c < CODE_COLS; c++) {
            const int k = r * CODE_COLS + c;
            const float x = x0 + (float) c * (key_w + gap);
            const float y = y0 + (float) r * (key_h + gap);
            const int on = !dc.recent_focus && k == dc.key;
            tgg_ui_row(x, y, key_w, key_h, on);
            if (k == CODE_KEY_CLEAR) {
                tgg_draw_text(x + key_w * 0.5f, y + 9.0f, 15.0f,
                              on ? TGG_UI_TEXT_INK : TGG_UI_TEXT_MUTED, TGG_ALIGN_CENTER, "Clear");
            } else {
                char label[2] = { code_layout[r][c], '\0' };
                tgg_draw_text(x + key_w * 0.5f, y + 6.5f, 21.0f,
                              on ? TGG_UI_TEXT_INK : TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER, label);
            }
        }
    }

    const float y = y0 + (float) CODE_ROWS * (key_h + gap);
    const float half = (grid_w - gap) * 0.5f;
    const int del = !dc.recent_focus && dc.key == CODE_KEY_DELETE;
    const int go = !dc.recent_focus && dc.key == CODE_KEY_JOIN;
    const int ready = len == 4;
    tgg_ui_row(x0, y, half, key_h, del);
    tgg_draw_text(x0 + half * 0.5f, y + 8.0f, 18.0f,
                  del ? TGG_UI_TEXT_INK : TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER, "Delete");
    tgg_ui_row(x0 + half + gap, y, half, key_h, go);
    tgg_draw_text(x0 + half + gap + half * 0.5f, y + 8.0f, 18.0f,
                  go ? TGG_UI_TEXT_INK : (ready ? TGG_UI_TEXT_GOLD : TGG_UI_TEXT_MUTED),
                  TGG_ALIGN_CENTER, "Join room");
}

static void draw_addr_keyboard(void)
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
            char label[2] = { addr_layout[r][c], '\0' };
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

void keyboard_draw(void)
{
    if (dc.typing_code) {
        draw_code_keyboard();
    } else {
        draw_addr_keyboard();
    }
}
