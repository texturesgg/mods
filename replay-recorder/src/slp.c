/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Writing a Slippi replay (.slp), as project-slippi's SPEC.md lays it out:
 * a UBJSON object of two elements. `raw` is an array of bytes, the events
 * the console sends, its length written last (0 while the match runs, as
 * Slippi's own files are). `metadata` is plain UBJSON: when it started, its
 * last frame, each player's characters, and `tgg`, the port's mod set, which
 * Slippi's tools ignore. */
#include "recorder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct SlpFile {
    FILE* file;
    long raw_start; /* the raw element's first byte */
    int last_frame;
    int frames;
    int port_used[4];
    /* Post-frame updates by port and internal character, for metadata */
    int characters[4][256];
    /* Added content by port (RecMatch), and each port's character */
    uint8_t ckind[4];
    char fighters[4][200];
    char costumes[4][200];
};

static void be16(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 8);
    p[1] = (uint8_t) v;
}

static void be32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

/* The events this file can hold, and their sizes (Event Payloads) */
static const struct {
    uint8_t cmd;
    uint16_t size;
} payloads[] = {
    { CMD_GAME_START, REC_GAME_START_SIZE }, { CMD_PRE, REC_PRE_SIZE },
    { CMD_POST, REC_POST_SIZE },             { CMD_GAME_END, REC_GAME_END_SIZE },
    { CMD_FRAME_START, REC_FRAME_START_SIZE }, { CMD_ITEM, REC_ITEM_SIZE },
    { CMD_BOOKEND, REC_BOOKEND_SIZE },       { CMD_FOD, REC_FOD_SIZE },
    { CMD_WHISPY, REC_WHISPY_SIZE },         { CMD_STADIUM, REC_STADIUM_SIZE },
};
#define N_PAYLOADS (sizeof payloads / sizeof payloads[0])

SlpFile* slp_create(const char* path, const RecMatch* m)
{
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        return NULL;
    }
    SlpFile* f = calloc(1, sizeof *f);
    f->file = file;
    for (int port = 0; port < 4; port++) {
        f->ckind[port] = m->info[0x60 + 0x24 * port];
        memcpy(f->fighters[port], m->fighters[port], sizeof f->fighters[port]);
        memcpy(f->costumes[port], m->costumes[port], sizeof f->costumes[port]);
    }
    f->last_frame = REC_FIRST_FRAME - 1;

    /* {U\3raw[$U#l<length> */
    static const uint8_t head[] = { '{', 'U', 3, 'r', 'a', 'w', '[', '$', 'U', '#', 'l', 0, 0, 0, 0 };
    fwrite(head, 1, sizeof head, file);
    f->raw_start = (long) sizeof head;

    uint8_t table[2 + 3 * N_PAYLOADS];
    table[0] = 0x35;
    table[1] = (uint8_t) (1 + 3 * N_PAYLOADS);
    for (size_t i = 0; i < N_PAYLOADS; i++) {
        table[2 + 3 * i] = payloads[i].cmd;
        be16(table + 3 + 3 * i, payloads[i].size);
    }
    fwrite(table, 1, sizeof table, file);

    /* Game Start */
    static uint8_t gs[1 + REC_GAME_START_SIZE];
    memset(gs, 0, sizeof gs);
    gs[0] = CMD_GAME_START;
    gs[1] = REC_VERSION_MAJOR;
    gs[2] = REC_VERSION_MINOR;
    memcpy(gs + 0x5, m->info, sizeof m->info);
    be32(gs + 0x13D, m->seed);
    for (int i = 0; i < 4; i++) {
        /* Dashback and shield drop fixes */
        be32(gs + 0x141 + 8 * i, m->ucf[i]);
        be32(gs + 0x145 + 8 * i, m->ucf[i]);
        memcpy(gs + 0x161 + 0x10 * i, m->nametags[i], 16);
    }
    gs[0x1A1] = 0; /* PAL */
    gs[0x1A2] = m->frozen_stadium;
    gs[0x1A3] = m->minor_scene;
    gs[0x1A4] = m->major_scene;
    /* Display names, connect codes, Slippi UIDs, the session and game
     * number are Slippi Online's: zero */
    gs[0x2BD] = m->language;
    fwrite(gs, 1, sizeof gs, file);
    return f;
}

void slp_frame(SlpFile* f, const RecFrame* frame)
{
    fwrite(frame->bytes, 1, (size_t) frame->len, f->file);
    for (int at = 0; at < frame->len;) {
        const uint8_t cmd = frame->bytes[at];
        size_t size = 0;
        for (size_t i = 0; i < N_PAYLOADS; i++) {
            if (payloads[i].cmd == cmd) {
                size = payloads[i].size;
            }
        }
        if (cmd == CMD_POST && frame->bytes[at + 6] == 0 && frame->bytes[at + 5] < 4) {
            const int port = frame->bytes[at + 5];
            f->port_used[port] = 1;
            f->characters[port][frame->bytes[at + 7]]++;
        }
        at += 1 + (int) size;
    }
    /* Frame Bookend: every frame written is final */
    uint8_t end[1 + REC_BOOKEND_SIZE];
    end[0] = CMD_BOOKEND;
    be32(end + 1, (uint32_t) frame->index);
    be32(end + 5, (uint32_t) frame->index);
    fwrite(end, 1, sizeof end, f->file);
    f->last_frame = frame->index;
    f->frames++;
}

/* UBJSON, as much as the metadata needs */
static void key(FILE* out, const char* k)
{
    const size_t n = strlen(k);
    fputc('U', out);
    fputc((int) n, out);
    fwrite(k, 1, n, out);
}

static void string(FILE* out, const char* s)
{
    const size_t n = strlen(s);
    fputc('S', out);
    if (n < 256) {
        fputc('U', out);
        fputc((int) n, out);
    } else {
        uint8_t len[4];
        be32(len, (uint32_t) n);
        fputc('l', out);
        fwrite(len, 1, 4, out);
    }
    fwrite(s, 1, n, out);
}

static void int32(FILE* out, int32_t v)
{
    uint8_t b[4];
    be32(b, (uint32_t) v);
    fputc('l', out);
    fwrite(b, 1, 4, out);
}

int slp_finish(SlpFile* f, const RecEnd* end, const char* start_at)
{
    FILE* out = f->file;
    if (end != NULL && end->known) {
        uint8_t ge[1 + REC_GAME_END_SIZE];
        ge[0] = CMD_GAME_END;
        ge[1] = end->method;
        ge[2] = (uint8_t) end->lras;
        for (int i = 0; i < 4; i++) {
            ge[3 + i] = (uint8_t) end->placements[i];
        }
        fwrite(ge, 1, sizeof ge, out);
    }
    const long raw_end = ftell(out);

    key(out, "metadata");
    fputc('{', out);
    key(out, "startAt");
    string(out, start_at);
    key(out, "lastFrame");
    int32(out, f->last_frame);
    key(out, "players");
    fputc('{', out);
    for (int port = 0; port < 4; port++) {
        if (!f->port_used[port]) {
            continue;
        }
        const char name[2] = { (char) ('0' + port), '\0' };
        key(out, name);
        fputc('{', out);
        key(out, "characters");
        fputc('{', out);
        for (int c = 0; c < 256; c++) {
            if (f->characters[port][c] != 0) {
                char id[4];
                snprintf(id, sizeof id, "%d", c);
                key(out, id);
                int32(out, f->characters[port][c]);
            }
        }
        fputc('}', out);
        fputc('}', out);
    }
    fputc('}', out);
    key(out, "playedOn");
    string(out, "tgg-melee");
    /* The port's own: the game build and the mods that decide how it plays */
    key(out, "tgg");
    fputc('{', out);
    key(out, "game");
    string(out, tgg_game_build());
    key(out, "modSetId");
    string(out, tgg_mod_set_id());
    key(out, "mods");
    fputc('[', out);
    const char* names = tgg_mod_set_names();
    while (names != NULL && *names != '\0') {
        const char* sep = strstr(names, ", ");
        const size_t n = sep != NULL ? (size_t) (sep - names) : strlen(names);
        char one[160];
        snprintf(one, sizeof one, "%.*s", (int) n, names);
        string(out, one);
        names = sep != NULL ? sep + 2 : NULL;
    }
    fputc(']', out);
    /* The exact mods and settings, which a replay player resolves to play it
     * back with (core's docs/mod-profiles.md) */
    key(out, "profile");
    string(out, tgg_mod_set_profile());
    /* Fighters and costumes mods add: the numbers in the replay are this
     * game's, and these say what they stand for. A player checks them. */
    int added = 0;
    for (int port = 0; port < 4; port++) {
        added |= f->fighters[port][0] != '\0' || f->costumes[port][0] != '\0';
    }
    if (added) {
        key(out, "content");
        fputc('{', out);
        key(out, "fighters");
        fputc('{', out);
        for (int port = 0; port < 4; port++) {
            int seen = 0;
            for (int q = 0; q < port; q++) {
                seen |= f->ckind[q] == f->ckind[port] && f->fighters[q][0] != '\0';
            }
            if (f->fighters[port][0] != '\0' && !seen) {
                char number[4];
                snprintf(number, sizeof number, "%d", f->ckind[port]);
                key(out, number);
                string(out, f->fighters[port]);
            }
        }
        fputc('}', out);
        key(out, "costumes");
        fputc('{', out);
        for (int port = 0; port < 4; port++) {
            if (f->costumes[port][0] != '\0') {
                const char name[2] = { (char) ('0' + port), '\0' };
                key(out, name);
                string(out, f->costumes[port]);
            }
        }
        fputc('}', out);
        fputc('}', out);
    }
    fputc('}', out);
    fputc('}', out);
    fputc('}', out);

    /* The raw element's length */
    uint8_t len[4];
    be32(len, (uint32_t) (raw_end - f->raw_start));
    fseek(out, f->raw_start - 4, SEEK_SET);
    fwrite(len, 1, 4, out);
    const int ok = ferror(out) == 0;
    const int closed = fclose(out) == 0;
    free(f);
    return ok && closed ? 0 : -1;
}
