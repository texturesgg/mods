/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tgg.skins: loads the skins the player picked, from <data>/skins.json, which
 * tgg-studio and `tgg skin` write:
 *
 *   {"format": 1,
 *    "slots": {"PlFxOr.dat": {"path": "/home/me/…/9b88….dat",
 *                             "sha256": "9b88…", "name": "Dark Fox"}}}
 *
 * Each slot names its skin's file. A costume is added to the disc with
 * tgg_files_add and handed to the game on costume_file; a menu or trophy file
 * replaces the disc's with tgg_files_replace. In a netplay match the game
 * itself only takes a costume that changes nothing but looks, and loads the
 * normal one otherwise, so a skin never changes who someone can play.
 */
#include "tgg/events.h"
#include "tgg/files.h"
#include "tgg/mod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_SLOTS 512

typedef struct slot {
    char file[32];      /* the disc file it replaces: "PlFxOr.dat" */
    char sha256[65];
    char path[1024];    /* the skin's file */
    char added[64];     /* where tgg_files_add put it, or "" */
} slot;

static slot slots[MAX_SLOTS];
static int slot_count;

/* ---- A small JSON reader for skins.json ------------------------------------ */

typedef struct json {
    const char* p;
    const char* end;
} json;

static void skip_space(json* j)
{
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) {
        j->p++;
    }
}

/* A string into out (cap bytes, cut short if longer); 0 when it isn't one */
static int read_string(json* j, char* out, size_t cap)
{
    skip_space(j);
    if (j->p >= j->end || *j->p != '"') {
        return 0;
    }
    j->p++;
    size_t n = 0;
    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\' && j->p < j->end) {
            c = *j->p++;
            switch (c) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'u': {
                /* Only what a path or a name needs: code points below 0x800 */
                unsigned code = 0;
                for (int i = 0; i < 4 && j->p < j->end; i++) {
                    char h = *j->p++;
                    code = code * 16 + (unsigned) (h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
                }
                if (code >= 0x80 && n + 2 < cap) {
                    out[n++] = (char) (code >= 0x800 ? '?' : 0xC0 | (code >> 6));
                    c = (char) (0x80 | (code & 0x3F));
                } else {
                    c = (char) code;
                }
                break;
            }
            default: break; /* \" \\ \/ */
            }
        }
        if (out != NULL && n + 1 < cap) {
            out[n++] = c;
        }
    }
    if (out != NULL && cap > 0) {
        out[n] = '\0';
    }
    if (j->p >= j->end) {
        return 0;
    }
    j->p++;
    return 1;
}

/* Skip any one value */
static int skip_value(json* j)
{
    skip_space(j);
    if (j->p >= j->end) {
        return 0;
    }
    if (*j->p == '"') {
        return read_string(j, NULL, 0);
    }
    if (*j->p == '{' || *j->p == '[') {
        const char close = *j->p == '{' ? '}' : ']';
        j->p++;
        skip_space(j);
        if (j->p < j->end && *j->p == close) {
            j->p++;
            return 1;
        }
        for (;;) {
            if (close == '}') {
                if (!read_string(j, NULL, 0)) {
                    return 0;
                }
                skip_space(j);
                if (j->p >= j->end || *j->p++ != ':') {
                    return 0;
                }
            }
            if (!skip_value(j)) {
                return 0;
            }
            skip_space(j);
            if (j->p < j->end && *j->p == ',') {
                j->p++;
                continue;
            }
            if (j->p < j->end && *j->p == close) {
                j->p++;
                return 1;
            }
            return 0;
        }
    }
    while (j->p < j->end && *j->p != ',' && *j->p != '}' && *j->p != ']') {
        j->p++; /* a number, true, false or null */
    }
    return 1;
}

/* Call fn for each key of an object, positioned at its value; fn reads or
 * skips the value */
static int each_key(json* j, int (*fn)(json* j, const char* key, void* user), void* user)
{
    skip_space(j);
    if (j->p >= j->end || *j->p != '{') {
        return 0;
    }
    j->p++;
    skip_space(j);
    if (j->p < j->end && *j->p == '}') {
        j->p++;
        return 1;
    }
    for (;;) {
        char key[128];
        if (!read_string(j, key, sizeof key)) {
            return 0;
        }
        skip_space(j);
        if (j->p >= j->end || *j->p++ != ':') {
            return 0;
        }
        if (!fn(j, key, user)) {
            return 0;
        }
        skip_space(j);
        if (j->p < j->end && *j->p == ',') {
            j->p++;
            continue;
        }
        if (j->p < j->end && *j->p == '}') {
            j->p++;
            return 1;
        }
        return 0;
    }
}

static int slot_field(json* j, const char* key, void* user)
{
    slot* s = user;
    if (strcmp(key, "sha256") == 0) {
        return read_string(j, s->sha256, sizeof s->sha256);
    }
    if (strcmp(key, "path") == 0) {
        return read_string(j, s->path, sizeof s->path);
    }
    return skip_value(j);
}

static int slots_field(json* j, const char* key, void* user)
{
    (void) user;
    if (slot_count >= MAX_SLOTS || strlen(key) >= sizeof slots[0].file) {
        return skip_value(j);
    }
    slot* s = &slots[slot_count];
    memset(s, 0, sizeof *s);
    snprintf(s->file, sizeof s->file, "%s", key);
    if (!each_key(j, slot_field, s)) {
        return 0;
    }
    if (s->path[0] != '\0') {
        slot_count++;
    }
    return 1;
}

static int top_field(json* j, const char* key, void* user)
{
    (void) user;
    if (strcmp(key, "slots") == 0) {
        return each_key(j, slots_field, NULL);
    }
    return skip_value(j);
}

/* ---- Loading ----------------------------------------------------------------- */

static int is_menu_file(const char* name)
{
    return strncasecmp(name, "Mn", 2) == 0 || strncasecmp(name, "Ty", 2) == 0;
}

static int is_costume_file(const char* name)
{
    /* PlFxOr.dat: Pl, the fighter, the color, an archive ending */
    const size_t n = strlen(name);
    return n == 10 && strncasecmp(name, "Pl", 2) == 0 &&
           (strcasecmp(name + 6, ".dat") == 0 || strcasecmp(name + 6, ".usd") == 0);
}

static void start(void)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/skins.json", tgg_data_dir());
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return; /* no skins picked */
    }
    static char text[256 * 1024];
    const size_t size = fread(text, 1, sizeof text, f);
    fclose(f);
    json j = { text, text + size };
    if (!each_key(&j, top_field, NULL)) {
        tgg_log("%s isn't a skins file this version reads; no skins loaded", path);
        slot_count = 0;
        return;
    }
    int loaded = 0;
    for (int i = 0; i < slot_count; i++) {
        slot* s = &slots[i];
        if (is_menu_file(s->file)) {
            loaded += tgg_files_replace(s->file, s->path) == 0;
        } else if (is_costume_file(s->file)) {
            if (tgg_files_add(s->path, s->file, s->added, sizeof s->added) == 0) {
                loaded++;
            } else {
                s->added[0] = '\0';
            }
        } else {
            tgg_log("%s: stages, effects and fighter files aren't loaded yet", s->file);
        }
    }
    tgg_log("%d of %d skins loaded", loaded, slot_count);
}
TGG_INIT(start);

static void on_costume(tgg_costume_file* c)
{
    for (int i = 0; i < slot_count; i++) {
        if (slots[i].added[0] != '\0' && strcasecmp(slots[i].file, c->disc_file) == 0) {
            c->file = slots[i].added;
            return;
        }
    }
}
TGG_ON(costume_file, on_costume);
