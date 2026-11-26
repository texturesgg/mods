/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Recent room codes: up to RECENT_MAX most recently created or joined 4-letter
 * room codes (with an optional friend name seen in that room), stored in
 * <data folder>/recent-rooms.txt as one "CODE\tLABEL\n" line per entry. */
#include "dc.h"

#include "tgg/files.h"

#include <stdio.h>
#include <string.h>

#define RECENT_FILE "recent-rooms.txt"

static RecentRoom entries[RECENT_MAX];
static int n_entries;
static int loaded;

int dc_is_room_char(char c)
{
    if (c >= 'a' && c <= 'z') {
        c = (char) (c - 32);
    }
    return c >= 'A' && c <= 'Z' && c != 'I' && c != 'L' && c != 'O';
}

static int normalize_code(const char* in, char out[8])
{
    if (in == NULL) {
        return 0;
    }
    int n = 0;
    for (const char* p = in; *p != '\0'; p++) {
        if (*p == ' ' || *p == '-') {
            continue;
        }
        if (n == 4 || !dc_is_room_char(*p)) {
            return 0;
        }
        char c = *p;
        if (c >= 'a' && c <= 'z') {
            c = (char) (c - 32);
        }
        out[n++] = c;
    }
    if (n != 4) {
        return 0;
    }
    out[4] = '\0';
    return 1;
}

static void clean_label(const char* in, char out[32])
{
    out[0] = '\0';
    if (in == NULL) {
        return;
    }
    int n = 0;
    for (const char* p = in; *p != '\0' && n < 28; p++) {
        const unsigned char c = (unsigned char) *p;
        if (c >= 0x20 && c < 0x7f && c != '\t') {
            out[n++] = (char) c;
        }
    }
    while (n > 0 && out[n - 1] == ' ') {
        n--;
    }
    out[n] = '\0';
}

static void recent_path(char* out, size_t cap)
{
    snprintf(out, cap, "%s/%s", tgg_data_dir(), RECENT_FILE);
}

static void recent_save(void)
{
    char path[1024], tmp[1040];
    recent_path(path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE* f = fopen(tmp, "wb");
    if (f == NULL) {
        return;
    }
    int ok = 1;
    for (int i = 0; i < n_entries; i++) {
        if (entries[i].label[0] != '\0') {
            if (fprintf(f, "%s\t%s\n", entries[i].code, entries[i].label) < 0) {
                ok = 0;
            }
        } else {
            if (fprintf(f, "%s\n", entries[i].code) < 0) {
                ok = 0;
            }
        }
    }
    if (fclose(f) != 0 || !ok) {
        remove(tmp);
        return;
    }
    /* Windows' rename won't replace an existing file */
    remove(path);
    rename(tmp, path);
}

void recent_load(void)
{
    if (loaded) {
        return;
    }
    loaded = 1;
    n_entries = 0;
    char path[1024];
    recent_path(path, sizeof path);
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return;
    }
    char line[128];
    while (n_entries < RECENT_MAX && fgets(line, sizeof line, f) != NULL) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        char* tab = strchr(line, '\t');
        const char* raw_label = NULL;
        if (tab != NULL) {
            *tab = '\0';
            raw_label = tab + 1;
        }
        char code[8];
        if (!normalize_code(line, code)) {
            continue;
        }
        int dup = 0;
        for (int i = 0; i < n_entries; i++) {
            if (strcmp(entries[i].code, code) == 0) {
                dup = 1;
                break;
            }
        }
        if (dup) {
            continue;
        }
        snprintf(entries[n_entries].code, sizeof entries[n_entries].code, "%s", code);
        clean_label(raw_label, entries[n_entries].label);
        n_entries++;
    }
    fclose(f);
}

int recent_count(void)
{
    if (!loaded) {
        recent_load();
    }
    return n_entries;
}

const RecentRoom* recent_get(int index)
{
    if (!loaded) {
        recent_load();
    }
    if (index < 0 || index >= n_entries) {
        return NULL;
    }
    return &entries[index];
}

void recent_remember(const char* raw_code, const char* raw_label)
{
    if (!loaded) {
        recent_load();
    }
    char code[8];
    if (!normalize_code(raw_code, code)) {
        return;
    }
    char label[32];
    clean_label(raw_label, label);

    int existing = -1;
    for (int i = 0; i < n_entries; i++) {
        if (strcmp(entries[i].code, code) == 0) {
            existing = i;
            break;
        }
    }
    RecentRoom item;
    memset(&item, 0, sizeof item);
    snprintf(item.code, sizeof item.code, "%s", code);
    if (label[0] != '\0') {
        snprintf(item.label, sizeof item.label, "%s", label);
    } else if (existing >= 0) {
        snprintf(item.label, sizeof item.label, "%s", entries[existing].label);
    }

    if (existing == 0 && strcmp(entries[0].label, item.label) == 0) {
        return; /* already at the top with the same label */
    }
    if (existing >= 0) {
        for (int i = existing; i > 0; i--) {
            entries[i] = entries[i - 1];
        }
    } else {
        if (n_entries < RECENT_MAX) {
            n_entries++;
        }
        for (int i = n_entries - 1; i > 0; i--) {
            entries[i] = entries[i - 1];
        }
    }
    entries[0] = item;
    recent_save();
}
