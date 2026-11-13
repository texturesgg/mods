/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Reading a Slippi replay: the raw stream of events in a UBJSON wrapper. Each
 * event is a command byte and a payload whose size the first event (0x35)
 * lists, so events this reader doesn't use are skipped by size, and fields a
 * newer Slippi added are only read when the payload is long enough. */
#include "slp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float slp_bef(const uint8_t* p)
{
    const uint32_t bits = slp_be32(p);
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

/* A field at `offset` (the command byte is offset 0) of `len` bytes is in an
 * event whose payload is `size` bytes. */
#define HAS(size, offset, len) ((offset) + (len) <= (size) + 1)

static SlpFrame* frame_at(Slp* slp, int32_t frame)
{
    const int i = frame - SLP_FIRST_FRAME;
    if (i < 0) {
        return NULL;
    }
    if (i >= slp->frame_count) {
        int count = slp->frame_count == 0 ? 4096 : slp->frame_count;
        while (count <= i) {
            count *= 2;
        }
        SlpFrame* grown = realloc(slp->frames, (size_t) count * sizeof *grown);
        if (grown == NULL) {
            return NULL;
        }
        memset(grown + slp->frame_count, 0, (size_t) (count - slp->frame_count) * sizeof *grown);
        slp->frames = grown;
        slp->frame_count = count;
    }
    return &slp->frames[i];
}

static void read_pre(Slp* slp, const uint8_t* e, uint16_t size)
{
    const int port = e[5];
    SlpFrame* f = frame_at(slp, (int32_t) slp_be32(e + 1));
    /* A follower (Nana) plays from her leader's input */
    if (f == NULL || port > 3 || e[6] != 0) {
        return;
    }
    SlpInput* in = &f->input[port];
    memset(in, 0, sizeof *in);
    in->have = 1;
    in->stick_x = slp_bef(e + 0x19);
    in->stick_y = slp_bef(e + 0x1D);
    in->cstick_x = slp_bef(e + 0x21);
    in->cstick_y = slp_bef(e + 0x25);
    in->buttons = slp_be32(e + 0x2D);
    in->physical = slp_be16(e + 0x31);
    in->l = slp_bef(e + 0x33);
    in->r = slp_bef(e + 0x37);
    in->raw_x = HAS(size, 0x3B, 1) ? (int8_t) e[0x3B] : (int8_t) lrintf(in->stick_x * 80);
    if (HAS(size, 0x40, 1)) {
        in->has_raw_y = 1;
        in->raw_y = (int8_t) e[0x40];
    }
    if (HAS(size, 0x42, 1)) {
        in->has_raw_c = 1;
        in->raw_cx = (int8_t) e[0x41];
        in->raw_cy = (int8_t) e[0x42];
    }
}

/* ---- The metadata's content map: {"fighters": {"34": id}, "costumes": {"0": id},
 * "stages": {"329": id}}
 * inside "tgg", in UBJSON */

typedef struct Ubj {
    const uint8_t* p;
    const uint8_t* end;
} Ubj;

/* A key or a string's text (after its 'S'): a length, then the bytes */
static int ubj_text(Ubj* u, char* out, size_t cap)
{
    if (u->p >= u->end) {
        return -1;
    }
    size_t n;
    const uint8_t type = *u->p++;
    if (type == 'U' && u->p < u->end) {
        n = *u->p++;
    } else if (type == 'l' && u->p + 4 <= u->end) {
        n = slp_be32(u->p);
        u->p += 4;
    } else {
        return -1;
    }
    if (u->p + n > u->end) {
        return -1;
    }
    const size_t copy = n < cap ? n : cap - 1;
    memcpy(out, u->p, copy);
    out[copy] = '\0';
    u->p += n;
    return 0;
}

/* An object of strings: calls fn with each key and value */
static int ubj_strings(Ubj* u, void (*fn)(const char* key, const char* value, Slp* slp), Slp* slp)
{
    if (u->p >= u->end || *u->p++ != '{') {
        return -1;
    }
    while (u->p < u->end && *u->p != '}') {
        char key[16], value[200];
        if (ubj_text(u, key, sizeof key) != 0 || u->p >= u->end || *u->p++ != 'S' ||
            ubj_text(u, value, sizeof value) != 0)
        {
            return -1;
        }
        fn(key, value, slp);
    }
    if (u->p >= u->end) {
        return -1;
    }
    u->p++;
    return 0;
}

static void content_fighter(const char* key, const char* value, Slp* slp)
{
    if (slp->n_fighters < 4) {
        slp->fighters[slp->n_fighters].number = atoi(key);
        snprintf(slp->fighters[slp->n_fighters].id, sizeof slp->fighters[0].id, "%s", value);
        slp->n_fighters++;
    }
}

static void content_costume(const char* key, const char* value, Slp* slp)
{
    const int port = atoi(key);
    if (port >= 0 && port < 4) {
        snprintf(slp->costumes[port], sizeof slp->costumes[port], "%s", value);
    }
}

static void content_stage(const char* key, const char* value, Slp* slp)
{
    (void) key; /* the replay's own stage number, which the Game Info block has */
    snprintf(slp->stage, sizeof slp->stage, "%s", value);
}

static void read_content(const uint8_t* meta, size_t len, Slp* slp)
{
    static const uint8_t key[] = { 'U', 7, 'c', 'o', 'n', 't', 'e', 'n', 't', '{' };
    for (size_t i = 0; i + sizeof key <= len; i++) {
        if (memcmp(meta + i, key, sizeof key) != 0) {
            continue;
        }
        Ubj u = { meta + i + sizeof key, meta + len };
        while (u.p < u.end && *u.p != '}') {
            char name[16];
            if (ubj_text(&u, name, sizeof name) != 0) {
                return;
            }
            const int r = strcmp(name, "fighters") == 0 ? ubj_strings(&u, content_fighter, slp)
                          : strcmp(name, "costumes") == 0 ? ubj_strings(&u, content_costume, slp)
                          : strcmp(name, "stages") == 0   ? ubj_strings(&u, content_stage, slp)
                                                          : -1;
            if (r != 0) {
                return;
            }
        }
        return;
    }
}

const char* slp_read(const char* path, Slp* slp)
{
    memset(slp, 0, sizeof *slp);
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return "can't open it";
    }
    fseek(file, 0, SEEK_END);
    const long len = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t* data = len > 0 ? malloc((size_t) len) : NULL;
    if (data == NULL || fread(data, 1, (size_t) len, file) != (size_t) len) {
        fclose(file);
        free(data);
        return "can't read it";
    }
    fclose(file);

    static const uint8_t head[] = { '{', 'U', 3, 'r', 'a', 'w', '[', '$', 'U', '#', 'l' };
    if (len < 17 || memcmp(data, head, sizeof head) != 0 || data[15] != 0x35) {
        free(data);
        return "not a Slippi replay";
    }
    size_t end = 15 + slp_be32(data + 11);
    if (end == 15 || end > (size_t) len) {
        /* A replay still being written has no length yet */
        end = (size_t) len;
    }
    size_t at = 15;
    uint16_t sizes[256] = { 0 };
    const uint8_t table_len = data[at + 1];
    for (size_t i = 0; i + 3 <= (size_t) table_len - 1; i += 3) {
        sizes[data[at + 2 + i]] = slp_be16(data + at + 3 + i);
    }
    at += 1 + table_len;

    int have_start = 0;
    int last_post = SLP_FIRST_FRAME - 1;
    while (at < end) {
        const uint8_t cmd = data[at];
        const uint16_t size = sizes[cmd];
        if (size == 0 || at + 1 + size > end) {
            break;
        }
        const uint8_t* e = data + at;
        switch (cmd) {
        case 0x36: /* Game Start */
            have_start = 1;
            memcpy(slp->version, e + 1, 3);
            memcpy(slp->info, e + 5, sizeof slp->info);
            slp->start_seed = slp_be32(e + 0x13D);
            slp->major_scene = HAS(size, 0x1A4, 1) ? e[0x1A4] : 0;
            break;
        case 0x3A: { /* Frame Start */
            SlpFrame* f = frame_at(slp, (int32_t) slp_be32(e + 1));
            if (f != NULL) {
                f->has_seed = 1;
                f->seed = slp_be32(e + 5);
            }
            break;
        }
        case 0x37: /* Pre-Frame Update */
            read_pre(slp, e, size);
            break;
        case 0x38: { /* Post-Frame Update */
            const int32_t frame = (int32_t) slp_be32(e + 1);
            if (e[5] <= 3 && e[6] == 0 && frame > last_post) {
                last_post = frame;
            }
            break;
        }
        case 0x41: /* Pokemon Stadium transformation */
            slp->transformations++;
            break;
        case 0x10: /* Message Splitter: the gecko code list, in parts */
            if (size >= 0x204 && e[0x203] == 0x3D) {
                const uint16_t part = slp_be16(e + 0x201);
                uint8_t* grown = realloc(slp->gecko, slp->gecko_len + part);
                if (grown != NULL) {
                    memcpy(grown + slp->gecko_len, e + 1, part);
                    slp->gecko = grown;
                    slp->gecko_len += part;
                }
            }
            break;
        default:
            break;
        }
        at += 1 + size;
    }
    if (end < (size_t) len) {
        read_content(data + end, (size_t) len - end, slp);
    }
    free(data);
    if (!have_start) {
        return "it has no Game Start";
    }
    /* Up to the last frame the console recorded the fighters after */
    const int count = last_post - SLP_FIRST_FRAME + 1;
    slp->frame_count = count < slp->frame_count ? count : slp->frame_count;
    if (slp->frame_count <= 0) {
        return "it has no frames";
    }
    return NULL;
}
