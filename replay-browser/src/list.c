/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The folder being browsed: its subfolders and its replays, sorted, each
 * replay's summary read from the replay player the first time it's shown. */
#include "browser.h"

#include "tgg/files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Browser br = {
    .filter_fighter = -1,
    .filter_stage = -1,
};

typedef struct {
    Entry* entries;
    int n, cap;
} Building;

static void facet_add_fighter(int ckind)
{
    if (ckind < 0 || br.n_fighters >= (int) (sizeof br.fighters / sizeof *br.fighters)) {
        return;
    }
    for (int i = 0; i < br.n_fighters; i++) {
        if (br.fighters[i] == ckind) {
            return;
        }
    }
    const char* name = fighter_name(ckind);
    int pos = br.n_fighters;
    while (pos > 0 && strcmp(fighter_name(br.fighters[pos - 1]), name) > 0) {
        br.fighters[pos] = br.fighters[pos - 1];
        pos--;
    }
    br.fighters[pos] = ckind;
    br.n_fighters++;
}

static void facet_add_stage(int stkind)
{
    if (stkind < 0 || br.n_stages >= (int) (sizeof br.stages / sizeof *br.stages)) {
        return;
    }
    for (int i = 0; i < br.n_stages; i++) {
        if (br.stages[i] == stkind) {
            return;
        }
    }
    const char* name = stage_name(stkind);
    int pos = br.n_stages;
    while (pos > 0 && strcmp(stage_name(br.stages[pos - 1]), name) > 0) {
        br.stages[pos] = br.stages[pos - 1];
        pos--;
    }
    br.stages[pos] = stkind;
    br.n_stages++;
}

static void facet_record(const replay_summary* s)
{
    if (s == NULL) {
        return;
    }
    facet_add_stage(s->stage);
    for (int p = 0; p < 4; p++) {
        if (s->players[p].present) {
            facet_add_fighter(s->players[p].ckind);
        }
    }
}

static void facets_rebuild(void)
{
    br.n_fighters = 0;
    br.n_stages = 0;
    for (int i = 0; i < br.n; i++) {
        if (!br.entries[i].is_dir && br.entries[i].read == 1) {
            facet_record(&br.entries[i].sum);
        }
    }
    /* If the active filter's fighter or stage disappeared (e.g. after delete), clear it */
    if (br.filter_fighter >= 0) {
        int found = 0;
        for (int i = 0; i < br.n_fighters; i++) {
            if (br.fighters[i] == br.filter_fighter) {
                found = 1;
                break;
            }
        }
        if (!found && br.scan_idx >= br.n) {
            br.filter_fighter = -1;
        }
    }
    if (br.filter_stage >= 0) {
        int found = 0;
        for (int i = 0; i < br.n_stages; i++) {
            if (br.stages[i] == br.filter_stage) {
                found = 1;
                break;
            }
        }
        if (!found && br.scan_idx >= br.n) {
            br.filter_stage = -1;
        }
    }
}

int list_has_filters(void)
{
    return br.filter_mode != FILTER_ALL || br.filter_fighter >= 0 || br.filter_stage >= 0;
}

static int matches_filters(const Entry* e)
{
    if (!list_has_filters()) {
        return 1;
    }
    if (e->is_dir) {
        return strcmp(e->name, "..") == 0;
    }
    if (e->read != 1) {
        return 0;
    }
    const replay_summary* s = &e->sum;
    if (br.filter_mode == FILTER_ONLINE && !s->online) {
        return 0;
    }
    if (br.filter_mode == FILTER_LOCAL && s->online) {
        return 0;
    }
    if (br.filter_stage >= 0 && s->stage != br.filter_stage) {
        return 0;
    }
    if (br.filter_fighter >= 0) {
        int has_fighter = 0;
        for (int p = 0; p < 4; p++) {
            if (s->players[p].present && s->players[p].ckind == br.filter_fighter) {
                has_fighter = 1;
                break;
            }
        }
        if (!has_fighter) {
            return 0;
        }
    }
    return 1;
}

int list_selected_entry(void)
{
    if (br.n_view <= 0 || br.sel < 0 || br.sel >= br.n_view) {
        return -1;
    }
    return br.view[br.sel];
}

void list_rebuild_view(const char* keep_name)
{
    char keep[256] = "";
    if (keep_name != NULL) {
        snprintf(keep, sizeof keep, "%s", keep_name);
    } else {
        const int cur = list_selected_entry();
        if (cur >= 0) {
            snprintf(keep, sizeof keep, "%s", br.entries[cur].name);
        }
    }

    if (br.n > br.view_cap) {
        int new_cap = br.view_cap ? br.view_cap : 64;
        while (new_cap < br.n) {
            new_cap *= 2;
        }
        int* grown = realloc(br.view, (size_t) new_cap * sizeof *grown);
        if (grown != NULL) {
            br.view = grown;
            br.view_cap = new_cap;
        }
    }

    br.n_view = 0;
    br.shown_replays = 0;
    for (int i = 0; i < br.n && br.n_view < br.view_cap; i++) {
        if (!matches_filters(&br.entries[i])) {
            continue;
        }
        br.view[br.n_view++] = i;
        if (!br.entries[i].is_dir) {
            br.shown_replays++;
        }
    }

    int found = -1;
    if (keep[0] != '\0') {
        for (int i = 0; i < br.n_view; i++) {
            if (strcmp(br.entries[br.view[i]].name, keep) == 0) {
                found = i;
                break;
            }
        }
    }
    if (found >= 0) {
        br.sel = found;
    } else if (br.n_view == 0) {
        br.sel = 0;
        br.top = 0;
    } else {
        if (br.sel >= br.n_view) {
            br.sel = br.n_view - 1;
        }
        if (br.sel < 0) {
            br.sel = 0;
        }
        /* Prefer landing on the first matching replay rather than ".." when a
         * filter just changed what's visible */
        if (list_has_filters() && br.entries[br.view[br.sel]].is_dir) {
            for (int i = 0; i < br.n_view; i++) {
                if (!br.entries[br.view[i]].is_dir) {
                    br.sel = i;
                    break;
                }
            }
        }
    }
    if (br.top > br.sel) {
        br.top = br.sel;
    }
    if (br.n_view <= LIST_ROWS) {
        br.top = 0;
    } else if (br.top > br.n_view - LIST_ROWS) {
        br.top = br.n_view - LIST_ROWS;
    }
}

void list_clear_filters(void)
{
    br.filter_mode = FILTER_ALL;
    br.filter_fighter = -1;
    br.filter_stage = -1;
    if (br.filter_col > 2) {
        br.filter_col = 0;
    }
    list_rebuild_view(NULL);
}

int list_scan_step(int budget)
{
    int scanned = 0;
    int changed = 0;
    while (br.scan_idx < br.n && scanned < budget) {
        const int idx = br.scan_idx++;
        if (br.entries[idx].is_dir) {
            continue;
        }
        if (br.entries[idx].read == 0) {
            const replay_summary* s = list_summary(idx);
            if (s != NULL) {
                changed = 1;
            }
            scanned++;
        } else if (br.entries[idx].read == 1) {
            facet_record(&br.entries[idx].sum);
        }
    }
    if (changed && list_has_filters()) {
        list_rebuild_view(NULL);
    }
    return br.scan_idx < br.n;
}

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
    const int cur = list_selected_entry();
    if (cur >= 0) {
        snprintf(keep, sizeof keep, "%s", br.entries[cur].name);
    }
    qsort(br.entries, (size_t) br.n, sizeof *br.entries, compare);
    if (br.scan_idx < br.n) {
        br.scan_idx = 0;
    }
    list_rebuild_view(keep[0] != '\0' ? keep : NULL);
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
    const int same_folder = strcmp(folder, br.folder) == 0;
    /* The same folder again keeps its row and active filters */
    char keep[256] = "";
    if (same_folder) {
        const int cur = list_selected_entry();
        if (cur >= 0) {
            snprintf(keep, sizeof keep, "%s", br.entries[cur].name);
        }
    } else {
        br.filter_mode = FILTER_ALL;
        br.filter_fighter = -1;
        br.filter_stage = -1;
        br.filter_col = 0;
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
    br.total_replays = 0;
    for (int i = 0; i < br.n; i++) {
        if (!br.entries[i].is_dir) {
            br.total_replays++;
        }
    }
    br.n_fighters = 0;
    br.n_stages = 0;
    br.scan_idx = 0;
    br.sel = 0;
    br.top = 0;
    br.focus = FOCUS_LIST;
    br.confirm_delete = 0;
    br.confirm_yes = 0;
    qsort(br.entries, (size_t) br.n, sizeof *br.entries, compare);
    list_rebuild_view(keep[0] != '\0' ? keep : NULL);
    /* The newest replay, not the first folder, when there are replays */
    if (keep[0] == '\0') {
        for (int i = 0; i < br.n_view; i++) {
            if (!br.entries[br.view[i]].is_dir) {
                br.sel = i;
                break;
            }
        }
    }
    return 0;
}

int list_delete_selected(char* deleted_name, int cap)
{
    const int idx = list_selected_entry();
    if (idx < 0 || br.entries[idx].is_dir) {
        return -1;
    }
    char path[PATH_MAX_LEN + 256];
    list_path(idx, path, sizeof path);
    if (remove(path) != 0) {
        return -1;
    }
    if (deleted_name != NULL && cap > 0) {
        snprintf(deleted_name, (size_t) cap, "%s", br.entries[idx].name);
    }
    if (idx + 1 < br.n) {
        memmove(&br.entries[idx], &br.entries[idx + 1],
                (size_t) (br.n - idx - 1) * sizeof *br.entries);
    }
    br.n--;
    if (br.total_replays > 0) {
        br.total_replays--;
    }
    if (br.scan_idx > idx) {
        br.scan_idx--;
    }
    facets_rebuild();
    /* Keep cursor at the same slot in the view (now the next replay) */
    list_rebuild_view("");
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
        if (e->read == 1) {
            facet_record(&e->sum);
        }
    }
    return e->read == 1 ? &e->sum : NULL;
}
