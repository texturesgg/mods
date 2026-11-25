/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Shared UI tokens, rounded surfaces, and drawn GameCube button pills built
 * purely on tgg/draw.h. Mirrors tgg-melee's host/ui.h so official mods and the
 * port's Mods page share one cohesive look.
 */
#ifndef TGG_MOD_UI_H
#define TGG_MOD_UI_H

#include "tgg/draw.h"

#include <stddef.h>

/* ---- Design tokens ---------------------------------------------------------- */

/* Surfaces (deep Melee space-navy dark mode) */
#define TGG_UI_BG_BAND       TGG_RGBA(7, 10, 26, 218)
#define TGG_UI_BG_PANEL      TGG_RGBA(12, 17, 42, 218)
#define TGG_UI_BG_PANEL_DEEP TGG_RGBA(9, 13, 32, 240)
#define TGG_UI_BG_ROW        TGG_RGBA(20, 28, 66, 204)
#define TGG_UI_BG_ROW_FOCUS  TGG_RGBA(246, 200, 56, 255)
#define TGG_UI_BG_CHIP       TGG_RGBA(26, 36, 84, 224)
#define TGG_UI_BG_TRACK      TGG_RGBA(255, 255, 255, 34)
#define TGG_UI_BG_FILL       TGG_RGBA(108, 148, 255, 235)
#define TGG_UI_SHADOW        TGG_RGBA(0, 0, 0, 130)

/* Perimeter borders and subtle dividers (never colored edge strips) */
#define TGG_UI_BORDER_SUBTLE TGG_RGBA(106, 134, 226, 105)
#define TGG_UI_BORDER_ROW    TGG_RGBA(96, 124, 214, 78)
#define TGG_UI_BORDER_BRIGHT TGG_RGBA(154, 182, 255, 190)
#define TGG_UI_BORDER_FOCUS  TGG_RGBA(255, 242, 182, 240)
#define TGG_UI_DIVIDER       TGG_RGBA(128, 152, 230, 68)

/* Typography */
#define TGG_UI_TEXT_PRIMARY  TGG_RGBA(244, 247, 255, 255)
#define TGG_UI_TEXT_TITLE    TGG_RGBA(232, 237, 252, 255)
#define TGG_UI_TEXT_MUTED    TGG_RGBA(168, 180, 214, 255)
#define TGG_UI_TEXT_GOLD     TGG_RGBA(255, 210, 68, 255)
#define TGG_UI_TEXT_INK      TGG_RGBA(16, 19, 34, 255)
#define TGG_UI_TEXT_GOOD     TGG_RGBA(116, 234, 144, 255)
#define TGG_UI_TEXT_BAD      TGG_RGBA(255, 114, 102, 255)

/* GameCube button palette */
#define TGG_UI_GC_A_FILL     TGG_RGBA(44, 206, 158, 255)
#define TGG_UI_GC_A_RIM      TGG_RGBA(126, 240, 204, 255)
#define TGG_UI_GC_A_INK      TGG_RGBA(10, 42, 34, 255)
#define TGG_UI_GC_B_FILL     TGG_RGBA(224, 62, 68, 255)
#define TGG_UI_GC_B_RIM      TGG_RGBA(252, 136, 140, 255)
#define TGG_UI_GC_GREY_FILL  TGG_RGBA(186, 194, 214, 255)
#define TGG_UI_GC_GREY_RIM   TGG_RGBA(228, 234, 248, 255)
#define TGG_UI_GC_Z_FILL     TGG_RGBA(110, 92, 224, 255)
#define TGG_UI_GC_Z_RIM      TGG_RGBA(170, 158, 250, 255)
#define TGG_UI_GC_DPAD_BASE  TGG_RGBA(48, 56, 86, 235)
#define TGG_UI_GC_DPAD_ARM   TGG_RGBA(112, 122, 154, 255)
#define TGG_UI_GC_DPAD_LIT   TGG_RGBA(236, 240, 252, 255)

/* Layout and geometry tokens */
#define TGG_UI_HEADER_H      60.0f
#define TGG_UI_FOOTER_Y      440.0f
#define TGG_UI_FOOTER_H      40.0f
#define TGG_UI_RADIUS_PANEL  8.0f
#define TGG_UI_RADIUS_ROW    6.0f
#define TGG_UI_RADIUS_CHIP   5.0f
#define TGG_UI_PAD_X         14.0f
#define TGG_UI_PAD_Y         10.0f
#define TGG_UI_HINT_GAP      20.0f
#define TGG_UI_HINT_ICON_GAP 7.0f

/* ---- Rounded geometry ------------------------------------------------------- */

static inline void tgg_ui_rounded_rect(float x, float y, float w, float h, float r,
                                       tgg_color fill, tgg_color border, float border_w)
{
    if (w <= 0.0f || h <= 0.0f) {
        return;
    }
    const float max_r = (w < h ? w : h) * 0.5f;
    if (r < 0.0f) {
        r = 0.0f;
    } else if (r > max_r) {
        r = max_r;
    }
    if (r < 0.75f) {
        if (fill.a > 0) {
            tgg_draw_rect(x, y, w, h, fill);
        }
        if (border.a > 0 && border_w > 0.0f) {
            tgg_draw_rect_outline(x, y, w, h, border_w, border);
        }
        return;
    }
    static const float c[4] = { 1.0f, 0.8660254f, 0.5f, 0.0f };
    static const float s[4] = { 0.0f, 0.5f, 0.8660254f, 1.0f };
    float pts[32];
    int n = 0;
    for (int i = 0; i < 4; i++) {
        pts[n++] = (x + w - r) + r * s[i];
        pts[n++] = (y + r) - r * c[i];
    }
    for (int i = 0; i < 4; i++) {
        pts[n++] = (x + w - r) + r * c[i];
        pts[n++] = (y + h - r) + r * s[i];
    }
    for (int i = 0; i < 4; i++) {
        pts[n++] = (x + r) - r * s[i];
        pts[n++] = (y + h - r) + r * c[i];
    }
    for (int i = 0; i < 4; i++) {
        pts[n++] = (x + r) - r * c[i];
        pts[n++] = (y + r) - r * s[i];
    }
    if (fill.a > 0) {
        tgg_draw_polygon(pts, 16, fill);
    }
    if (border.a > 0 && border_w > 0.0f) {
        tgg_draw_polygon_outline(pts, 16, border_w, border);
    }
}

/* ---- Surfaces --------------------------------------------------------------- */

static inline void tgg_ui_header(const char* title, const char* subtitle)
{
    const tgg_rect b = tgg_draw_bounds();
    tgg_draw_rect(b.x, 0.0f, b.w, TGG_UI_HEADER_H, TGG_UI_BG_BAND);
    tgg_draw_rect(b.x, TGG_UI_HEADER_H - 1.0f, b.w, 1.0f, TGG_UI_DIVIDER);
    if (title != NULL) {
        tgg_draw_text(36.0f, 14.0f, 34.0f, TGG_UI_TEXT_TITLE, TGG_ALIGN_LEFT, title);
    }
    if (subtitle != NULL && subtitle[0] != '\0') {
        tgg_draw_text(604.0f, 22.0f, 16.0f, TGG_UI_TEXT_MUTED, TGG_ALIGN_RIGHT, subtitle);
    }
}

static inline void tgg_ui_footer_band(void)
{
    const tgg_rect b = tgg_draw_bounds();
    tgg_draw_rect(b.x, TGG_UI_FOOTER_Y, b.w, TGG_UI_FOOTER_H, TGG_UI_BG_BAND);
    tgg_draw_rect(b.x, TGG_UI_FOOTER_Y, b.w, 1.0f, TGG_UI_DIVIDER);
}

static inline void tgg_ui_panel(float x, float y, float w, float h)
{
    tgg_ui_rounded_rect(x, y, w, h, TGG_UI_RADIUS_PANEL, TGG_UI_BG_PANEL, TGG_UI_BORDER_SUBTLE,
                        1.25f);
}

static inline void tgg_ui_modal(float x, float y, float w, float h)
{
    tgg_ui_rounded_rect(x + 3.0f, y + 5.0f, w, h, TGG_UI_RADIUS_PANEL + 1.0f, TGG_UI_SHADOW,
                        TGG_RGBA(0, 0, 0, 0), 0.0f);
    tgg_ui_rounded_rect(x, y, w, h, TGG_UI_RADIUS_PANEL, TGG_UI_BG_PANEL_DEEP, TGG_UI_BORDER_BRIGHT,
                        1.5f);
}

static inline void tgg_ui_row(float x, float y, float w, float h, int focused)
{
    tgg_ui_rounded_rect(x, y, w, h, TGG_UI_RADIUS_ROW,
                        focused ? TGG_UI_BG_ROW_FOCUS : TGG_UI_BG_ROW,
                        focused ? TGG_UI_BORDER_FOCUS : TGG_UI_BORDER_ROW,
                        focused ? 1.5f : 1.0f);
}

static inline void tgg_ui_chip(float x, float y, float w, float h, tgg_color fill, tgg_color border)
{
    tgg_ui_rounded_rect(x, y, w, h, TGG_UI_RADIUS_CHIP, fill, border, 1.0f);
}

static inline void tgg_ui_scrollbar(float x, float y, float h, int top, int visible, int total)
{
    if (total <= visible || visible <= 0 || h <= 4.0f) {
        return;
    }
    const float w = 5.0f;
    tgg_ui_rounded_rect(x, y, w, h, 2.5f, TGG_UI_BG_TRACK, TGG_RGBA(0, 0, 0, 0), 0.0f);
    float thumb = h * (float) visible / (float) total;
    if (thumb < 18.0f) {
        thumb = 18.0f;
    }
    if (thumb > h) {
        thumb = h;
    }
    const float span = h - thumb;
    const float at = span * (float) top / (float) (total - visible);
    tgg_ui_rounded_rect(x, y + at, w, thumb, 2.5f, TGG_UI_BG_FILL, TGG_RGBA(0, 0, 0, 0), 0.0f);
}

static inline void tgg_ui_progress(float x, float y, float w, float h, float frac)
{
    if (frac < 0.0f) {
        frac = 0.0f;
    } else if (frac > 1.0f) {
        frac = 1.0f;
    }
    const float r = h * 0.5f;
    tgg_ui_rounded_rect(x, y, w, h, r, TGG_UI_BG_TRACK, TGG_UI_BORDER_ROW, 1.0f);
    if (frac > 0.0f) {
        float fw = w * frac;
        if (fw < h && fw > 0.0f) {
            fw = h;
        }
        tgg_ui_rounded_rect(x, y, fw, h, r, TGG_UI_BG_FILL, TGG_RGBA(0, 0, 0, 0), 0.0f);
    }
}

/* ---- Drawn GameCube button pills -------------------------------------------- */

typedef enum tgg_ui_btn {
    TGG_UI_BTN_A = 0,
    TGG_UI_BTN_B,
    TGG_UI_BTN_X,
    TGG_UI_BTN_Y,
    TGG_UI_BTN_Z,
    TGG_UI_BTN_L,
    TGG_UI_BTN_R,
    TGG_UI_BTN_START,
    TGG_UI_BTN_LR,
    TGG_UI_BTN_DPAD_LEFT,
    TGG_UI_BTN_QUIT_COMBO,
} tgg_ui_btn;

typedef struct tgg_ui_hint {
    tgg_ui_btn btn;
    const char* label;
} tgg_ui_hint;

static inline float tgg_ui_btn_width(tgg_ui_btn btn, float h)
{
    switch (btn) {
    case TGG_UI_BTN_A:
    case TGG_UI_BTN_B:
    case TGG_UI_BTN_LR:
    case TGG_UI_BTN_DPAD_LEFT:
        return h;
    case TGG_UI_BTN_X:
        return h * 0.92f;
    case TGG_UI_BTN_Y:
    case TGG_UI_BTN_Z:
    case TGG_UI_BTN_L:
    case TGG_UI_BTN_R:
        return h * 1.28f;
    case TGG_UI_BTN_START:
        return tgg_draw_text_width(h * 0.62f, "START") + 11.0f;
    case TGG_UI_BTN_QUIT_COMBO:
        return tgg_draw_text_width(h * 0.62f, "L+R+A+START") + 12.0f;
    }
    return h;
}

static inline float tgg_ui_draw_btn(tgg_ui_btn btn, float x, float cy, float h)
{
    const float w = tgg_ui_btn_width(btn, h);
    const float y = cy - h * 0.5f;
    const float r = h * 0.5f;
    const float cx = x + w * 0.5f;
    const float ch = h * 0.72f;
    const float ty = cy - ch * 0.52f;

    switch (btn) {
    case TGG_UI_BTN_A:
        tgg_draw_circle(cx, cy, r, TGG_UI_GC_A_FILL);
        tgg_draw_circle_outline(cx, cy, r - 0.5f, 1.1f, TGG_UI_GC_A_RIM);
        tgg_draw_text(cx, ty, ch, TGG_UI_GC_A_INK, TGG_ALIGN_CENTER, "A");
        break;
    case TGG_UI_BTN_B:
        tgg_draw_circle(cx, cy, r * 0.94f, TGG_UI_GC_B_FILL);
        tgg_draw_circle_outline(cx, cy, r * 0.94f - 0.5f, 1.1f, TGG_UI_GC_B_RIM);
        tgg_draw_text(cx, ty, ch, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER, "B");
        break;
    case TGG_UI_BTN_X:
        tgg_ui_rounded_rect(x, y, w, h, w * 0.48f, TGG_UI_GC_GREY_FILL, TGG_UI_GC_GREY_RIM, 1.1f);
        tgg_draw_text(cx, ty, ch, TGG_UI_TEXT_INK, TGG_ALIGN_CENTER, "X");
        break;
    case TGG_UI_BTN_Y:
        tgg_ui_rounded_rect(x, y + h * 0.08f, w, h * 0.84f, h * 0.42f, TGG_UI_GC_GREY_FILL,
                            TGG_UI_GC_GREY_RIM, 1.1f);
        tgg_draw_text(cx, ty, ch, TGG_UI_TEXT_INK, TGG_ALIGN_CENTER, "Y");
        break;
    case TGG_UI_BTN_Z:
        tgg_ui_rounded_rect(x, y + h * 0.08f, w, h * 0.84f, h * 0.42f, TGG_UI_GC_Z_FILL,
                            TGG_UI_GC_Z_RIM, 1.1f);
        tgg_draw_text(cx, ty, ch, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER, "Z");
        break;
    case TGG_UI_BTN_L:
    case TGG_UI_BTN_R:
        tgg_ui_rounded_rect(x, y + h * 0.08f, w, h * 0.84f, h * 0.36f, TGG_UI_GC_GREY_FILL,
                            TGG_UI_GC_GREY_RIM, 1.1f);
        tgg_draw_text(cx, ty, ch, TGG_UI_TEXT_INK, TGG_ALIGN_CENTER,
                      btn == TGG_UI_BTN_L ? "L" : "R");
        break;
    case TGG_UI_BTN_START: {
        const float sh = h * 0.62f;
        tgg_ui_rounded_rect(x, y + h * 0.06f, w, h * 0.88f, h * 0.44f, TGG_UI_GC_GREY_FILL,
                            TGG_UI_GC_GREY_RIM, 1.1f);
        tgg_draw_text(cx, cy - sh * 0.52f, sh, TGG_UI_TEXT_INK, TGG_ALIGN_CENTER, "START");
        break;
    }
    case TGG_UI_BTN_QUIT_COMBO: {
        const float sh = h * 0.62f;
        tgg_ui_rounded_rect(x, y + h * 0.06f, w, h * 0.88f, h * 0.44f, TGG_UI_BG_CHIP,
                            TGG_UI_BORDER_BRIGHT, 1.1f);
        tgg_draw_text(cx, cy - sh * 0.52f, sh, TGG_UI_TEXT_PRIMARY, TGG_ALIGN_CENTER,
                      "L+R+A+START");
        break;
    }
    case TGG_UI_BTN_LR:
    case TGG_UI_BTN_DPAD_LEFT: {
        tgg_draw_circle(cx, cy, r, TGG_UI_GC_DPAD_BASE);
        tgg_draw_circle_outline(cx, cy, r - 0.5f, 1.0f, TGG_UI_BORDER_SUBTLE);
        const float arm = r * 0.76f;
        const float thick = r * 0.44f;
        const float half = thick * 0.5f;
        tgg_draw_rect(cx - half, cy - arm, thick, arm * 2.0f, TGG_UI_GC_DPAD_ARM);
        if (btn == TGG_UI_BTN_LR) {
            tgg_draw_rect(cx - arm, cy - half, arm * 2.0f, thick, TGG_UI_GC_DPAD_LIT);
        } else {
            tgg_draw_rect(cx - arm, cy - half, arm * 2.0f, thick, TGG_UI_GC_DPAD_ARM);
            tgg_draw_rect(cx - arm, cy - half, arm + half * 0.4f, thick, TGG_UI_GC_DPAD_LIT);
        }
        break;
    }
    }
    return w;
}

static inline float tgg_ui_hint_bar_width(const tgg_ui_hint* hints, int n, float text_size)
{
    const float pill_h = text_size + 2.0f;
    float total = 0.0f;
    for (int i = 0; i < n; i++) {
        if (i > 0) {
            total += TGG_UI_HINT_GAP;
        }
        total += tgg_ui_btn_width(hints[i].btn, pill_h);
        if (hints[i].label != NULL && hints[i].label[0] != '\0') {
            total += TGG_UI_HINT_ICON_GAP + tgg_draw_text_width(text_size, hints[i].label);
        }
    }
    return total;
}

static inline void tgg_ui_hint_bar(const tgg_ui_hint* hints, int n, float cx, float cy,
                                   float text_size)
{
    const float pill_h = text_size + 2.0f;
    float x = cx - tgg_ui_hint_bar_width(hints, n, text_size) * 0.5f;
    for (int i = 0; i < n; i++) {
        if (i > 0) {
            x += TGG_UI_HINT_GAP;
        }
        x += tgg_ui_draw_btn(hints[i].btn, x, cy, pill_h);
        if (hints[i].label != NULL && hints[i].label[0] != '\0') {
            x += TGG_UI_HINT_ICON_GAP;
            x += tgg_draw_text(x, cy - text_size * 0.52f, text_size, TGG_UI_TEXT_MUTED,
                               TGG_ALIGN_LEFT, hints[i].label);
        }
    }
}

static inline void tgg_ui_footer_hints(const tgg_ui_hint* hints, int n)
{
    tgg_ui_footer_band();
    tgg_ui_hint_bar(hints, n, 320.0f, TGG_UI_FOOTER_Y + TGG_UI_FOOTER_H * 0.5f, 17.0f);
}

#endif
