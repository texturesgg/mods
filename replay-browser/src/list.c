/* The folder being browsed: its subfolders and its replays, sorted, each
 * replay's summary read from the replay player the first time it's shown. */
#include "browser.h"

#include "tgg/files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Browser br;

typedef struct {
    Entry* entries;
    int n, cap;
} Building;

/* "Game_20260814T225818.slp": a date that sorts like replay_summary::when */
static long long name_date(const char* name)
{
    const char* g = strstr(name, "Game_");
    int t[6];
    if (g == NULL ||
        sscanf(g, "Game_%4d%2d%2dT%2d%2d%2d", &t[0], &t[1], &t[2], &t[3], &t[4], &t[5]) != 6) {
        return 0;
    }
    return ((((long long) t[0] * 13 + t[1]) * 32 + t[2]) * 24 + t[3]) * 3600LL + t[4] * 60LL + t[5];
}

static void add(const tgg_dir_entry* e, void* user)
{
    Building* b = user;
    if (!e->is_dir && !tgg_has_suffix(e->name, ".slp")) {
        return;
    }
    if (b->n == b->cap) {
        b->cap = b->cap ? b->cap * 2 : 64;
        Entry* grown = realloc(b->entries, (size_t) b->cap * sizeof *grown);
        if (grown == NULL) {
            return;
        }
        b->entries = grown;
    }
    Entry* x = &b->entries[b->n++];
    memset(x, 0, sizeof *x);
    snprintf(x->name, sizeof x->name, "%s", e->name);
    x->is_dir = e->is_dir;
    x->mtime = e->mtime;
    x->when = e->is_dir ? 0 : name_date(e->name);
    if (x->when == 0) {
        /* Seconds since 1970 sort apart from dates in names; within one
         * folder Slippi names them all the same way */
        x->when = e->mtime;
    }
}

static int compare(const void* pa, const void* pb)
{
    const Entry* a = pa;
    const Entry* b = pb;
    if (a->is_dir != b->is_dir) {
        return a->is_dir ? -1 : 1;
    }
    if (a->is_dir) {
        return strcmp(a->name, b->name);
    }
    if (a->when != b->when) {
        const int newer_first = a->when > b->when ? -1 : 1;
        return setting_sort == SORT_NEWEST ? newer_first : -newer_first;
    }
    return strcmp(a->name, b->name);
}

void list_sort(void)
{
    char keep[256] = "";
    if (br.sel >= 0 && br.sel < br.n) {
        snprintf(keep, sizeof keep, "%s", br.entries[br.sel].name);
    }
    qsort(br.entries, (size_t) br.n, sizeof *br.entries, compare);
    for (int i = 0; i < br.n; i++) {
        if (strcmp(br.entries[i].name, keep) == 0) {
            br.sel = i;
        }
    }
}

int list_open(const char* path)
{
    char folder[PATH_MAX_LEN];
    snprintf(folder, sizeof folder, "%s", path);
    Building b = { 0 };
    if (tgg_dir_list(folder, add, &b) < 0) {
        free(b.entries);
        return -1;
    }
    /* The same folder again keeps its row */
    char keep[256] = "";
    if (strcmp(folder, br.folder) == 0 && br.sel >= 0 && br.sel < br.n) {
        snprintf(keep, sizeof keep, "%s", br.entries[br.sel].name);
    }
    free(br.entries);
    snprintf(br.folder, sizeof br.folder, "%s", folder);
    /* ".." first, to go up */
    char parent[PATH_MAX_LEN];
    list_parent(parent, sizeof parent);
    if (parent[0] != '\0') {
        const tgg_dir_entry up = { "..", 1, 0, 0 };
        add(&up, &b);
    }
    br.entries = b.entries;
    br.n = b.n;
    br.sel = 0;
    br.top = 0;
    list_sort();
    for (int i = 0; i < br.n; i++) {
        if (strcmp(br.entries[i].name, keep) == 0) {
            br.sel = i;
        }
    }
    /* The newest replay, not the first folder, when there are replays */
    if (keep[0] == '\0') {
        for (int i = 0; i < br.n; i++) {
            if (!br.entries[i].is_dir) {
                br.sel = i;
                break;
            }
        }
    }
    return 0;
}

void list_path(int i, char* out, int cap)
{
    const size_t n = strlen(br.folder);
    const int slash = n > 0 && (br.folder[n - 1] == '/' || br.folder[n - 1] == '\\');
    snprintf(out, (size_t) cap, "%s%s%s", br.folder, slash ? "" : "/", br.entries[i].name);
}

void list_parent(char* out, int cap)
{
    snprintf(out, (size_t) cap, "%s", br.folder);
    size_t n = strlen(out);
    while (n > 1 && (out[n - 1] == '/' || out[n - 1] == '\\')) {
        out[--n] = '\0';
    }
    char* slash = strrchr(out, '/');
    char* back = strrchr(out, '\\');
    if (back != NULL && (slash == NULL || back > slash)) {
        slash = back;
    }
    if (slash == NULL) {
        out[0] = '\0';
    } else if (slash == out) {
        out[1] = '\0'; /* the root */
        if (n == 1) {
            out[0] = '\0';
        }
    } else {
        *slash = '\0';
    }
}

const replay_summary* list_summary(int i)
{
    if (i < 0 || i >= br.n || br.entries[i].is_dir) {
        return NULL;
    }
    Entry* e = &br.entries[i];
    if (e->read == 0) {
        char path[PATH_MAX_LEN + 256];
        list_path(i, path, sizeof path);
        e->read = replay_read_summary(path, &e->sum) == 0 ? 1 : -1;
    }
    return e->read == 1 ? &e->sum : NULL;
}
