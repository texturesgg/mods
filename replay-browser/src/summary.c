/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A replay's summary (replay_read_summary), as the browser lists it: its setup from the Game Start
 * event at the start of the file, how it ended from the Game End event at
 * the end of the raw stream, and its date and length from the metadata after
 * it. Only those parts of the file are read. The offsets are slippi-wiki
 * SPEC.md's, counted from each event's command byte. */
#include "replay_player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Shift-JIS to ASCII, for names: the half-width characters as they are,
 * full-width letters, digits and a few signs as their ASCII forms, and
 * anything else as '?' */
static void sjis_ascii(const uint8_t* in, size_t len, char* out, size_t cap)
{
    size_t n = 0;
    for (size_t i = 0; i < len && in[i] != 0 && n + 1 < cap;) {
        const uint8_t c = in[i];
        if (c < 0x80) {
            out[n++] = (char) c;
            i++;
            continue;
        }
        if (i + 1 >= len) {
            break;
        }
        const uint8_t d = in[i + 1];
        char a = '?';
        if (c == 0x82 && d >= 0x4F && d <= 0x58) {
            a = (char) ('0' + d - 0x4F);
        } else if (c == 0x82 && d >= 0x60 && d <= 0x79) {
            a = (char) ('A' + d - 0x60);
        } else if (c == 0x82 && d >= 0x81 && d <= 0x9A) {
            a = (char) ('a' + d - 0x81);
        } else if (c == 0x81) {
            static const struct {
                uint8_t d;
                char a;
            } signs[] = { { 0x40, ' ' }, { 0x94, '#' }, { 0x44, '.' }, { 0x5B, '-' },
                          { 0x7C, '-' }, { 0x49, '!' }, { 0x48, '?' }, { 0x51, '_' },
                          { 0x46, ':' }, { 0x95, '&' }, { 0x5E, '/' }, { 0x97, '@' } };
            for (size_t k = 0; k < sizeof signs / sizeof *signs; k++) {
                if (signs[k].d == d) {
                    a = signs[k].a;
                }
            }
        }
        out[n++] = a;
        i += 2;
    }
    while (n > 0 && out[n - 1] == ' ') {
        n--;
    }
    out[n] = '\0';
}

/* A UBJSON key's value in the metadata: an int32 ('l') or a string ('S') */
static const uint8_t* ubjson_find(const uint8_t* p, size_t len, const char* key)
{
    const size_t k = strlen(key);
    for (size_t i = 0; i + 2 + k < len; i++) {
        if (p[i] == 'U' && p[i + 1] == k && memcmp(p + i + 2, key, k) == 0) {
            return p + i + 2 + k;
        }
    }
    return NULL;
}

/* "Game_20260814T225818.slp": the date the console's clock gave the file */
static int date_from_name(const char* path, int t[6])
{
    const char* base = strrchr(path, '/');
    base = base != NULL ? base + 1 : path;
    const char* g = strstr(base, "Game_");
    return g != NULL &&
           sscanf(g, "Game_%4d%2d%2dT%2d%2d%2d", &t[0], &t[1], &t[2], &t[3], &t[4], &t[5]) == 6;
}

int replay_read_summary(const char* path, replay_summary* out)
{
    memset(out, 0, sizeof *out);
    out->frames = -1;
    out->quitter = -1;
    out->winner = -1;
    for (int p = 0; p < 4; p++) {
        out->players[p].placement = -1;
    }
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    /* The setup comes before the first frame */
    static uint8_t head[256 * 1024];
    const size_t got = fread(head, 1, sizeof head, f);
    static const uint8_t magic[] = { '{', 'U', 3, 'r', 'a', 'w', '[', '$', 'U', '#', 'l' };
    if (got < 0x40 || memcmp(head, magic, sizeof magic) != 0 || head[15] != 0x35) {
        fclose(f);
        return -1;
    }
    const uint32_t raw_len = slp_be32(head + 11);
    uint16_t sizes[256] = { 0 };
    const uint8_t table_len = head[16];
    for (size_t i = 0; i + 3 <= (size_t) table_len - 1 && 17 + i + 2 < got; i += 3) {
        sizes[head[17 + i]] = slp_be16(head + 18 + i);
    }
    const size_t start = 15 + 1 + table_len;
    const uint16_t gs = sizes[0x36];
    if (gs < 0x13D || start + 1 + gs > got || head[start] != 0x36) {
        fclose(f);
        return -1;
    }
    const uint8_t* e = head + start;
    out->version[0] = e[1];
    out->version[1] = e[2];
    out->version[2] = e[3];
    const uint8_t* info = e + 5;
    out->stage = slp_be16(info + 0xE);
    out->teams = info[0x8] != 0;
    out->online = gs >= 0x1A4 && e[0x1A4] == 8;
    for (int p = 0; p < 4; p++) {
        const uint8_t* pl = info + 0x60 + 0x24 * p;
        replay_player_slot* s = &out->players[p];
        s->present = pl[1] != 3;
        s->ckind = pl[0];
        s->cpu = pl[1] == 1;
        s->costume = pl[3];
        s->team = pl[9];
        if (gs >= 0x1A5 + 0x1F * 4 - 1) {
            sjis_ascii(e + 0x1A5 + 0x1F * p, 0x1F, s->name, sizeof s->name);
        }
        if (gs >= 0x221 + 0xA * 4 - 1) {
            sjis_ascii(e + 0x221 + 0xA * p, 0xA, s->code, sizeof s->code);
        }
        if (s->name[0] == '\0' && gs >= 0x161 + 0x10 * 4 - 1) {
            /* The name tag: eight Shift-JIS characters, two bytes each */
            sjis_ascii(e + 0x161 + 0x10 * p, 0x10, s->name, sizeof s->name);
        }
    }


    /* The Game End event closes the raw stream */
    const uint16_t ge = sizes[0x39];
    if (raw_len != 0 && ge >= 2 && fseek(f, (long) (15 + raw_len - (1 + ge)), SEEK_SET) == 0) {
        uint8_t end[16];
        if (ge + 1 <= sizeof end && fread(end, 1, (size_t) ge + 1, f) == (size_t) ge + 1 &&
            end[0] == 0x39)
        {
            out->end = end[1];
            out->quitter = ge >= 2 ? (int8_t) end[2] : -1;
            if (ge >= 6) {
                for (int p = 0; p < 4; p++) {
                    out->players[p].placement = (int8_t) end[3 + p];
                    if (out->players[p].placement == 0) {
                        out->winner = p;
                    }
                }
            }
        }
    }

    /* The metadata: the start time and the last frame */
    int t[6] = { 0 };
    int have_date = date_from_name(path, t);
    if (raw_len != 0 && fseek(f, (long) (15 + raw_len), SEEK_SET) == 0) {
        uint8_t meta[2048];
        const size_t n = fread(meta, 1, sizeof meta, f);
        const uint8_t* v = ubjson_find(meta, n, "lastFrame");
        if (v != NULL && v + 5 <= meta + n && v[0] == 'l') {
            out->frames = (int32_t) slp_be32(v + 1) + 124;
        }
        v = ubjson_find(meta, n, "startAt");
        if (!have_date && v != NULL && v + 3 <= meta + n && v[0] == 'S' && v[1] == 'U') {
            char text[40];
            const size_t len = v[2] < sizeof text - 1 ? v[2] : sizeof text - 1;
            if (v + 3 + len <= meta + n) {
                memcpy(text, v + 3, len);
                text[len] = '\0';
                have_date = sscanf(text, "%4d-%2d-%2dT%2d:%2d:%2d", &t[0], &t[1], &t[2], &t[3],
                                   &t[4], &t[5]) == 6;
            }
        }
    }
    fclose(f);
    if (have_date) {
        snprintf(out->date, sizeof out->date, "%04d-%02d-%02d %02d:%02d", t[0], t[1], t[2], t[3],
                 t[4]);
        out->when = ((((long long) t[0] * 13 + t[1]) * 32 + t[2]) * 24 + t[3]) * 3600LL +
                    t[4] * 60LL + t[5];
    }
    return 0;
}
