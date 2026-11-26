/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The replay browser's parts: the folder and its replays (list.c), the
 * screen's input (browser.c), its drawing (draw.c), and the game's names
 * for fighters and stages (names.c). */
#ifndef REPLAY_BROWSER_H
#define REPLAY_BROWSER_H

#include "tgg/mod.h"
#include "tgg/restart.h"

#include "replay_player.h"

#include <stdint.h>

#define PATH_MAX_LEN 1024

/* One row of the list: a folder or a replay. */
typedef struct Entry {
    char name[256];
    int is_dir;
    int64_t mtime;
    long long when;     /* the sort key: the date in a replay's name, else its mtime */
    int read;           /* the summary was read: 1 read, -1 not a replay */
    replay_summary sum;
} Entry;

enum {
    FILTER_ALL = 0,
    FILTER_ONLINE = 1,
    FILTER_LOCAL = 2,
};

enum {
    FOCUS_LIST = 0,
    FOCUS_FILTERS = 1,
    FOCUS_DELETE_BTN = 2,
};

typedef struct Browser {
    char folder[PATH_MAX_LEN];
    Entry* entries;
    int n;
    /* Filtered view: indices into `entries`. sel and top index into `view`. */
    int* view;
    int n_view;
    int view_cap;
    int total_replays;
    int shown_replays;
    int sel;
    int top;
    /* Quick filters and progressive summary indexing */
    int filter_mode;
    int filter_fighter; /* -1 for any, else CharacterKind */
    int filter_stage;   /* -1 for any, else StKind */
    int fighters[32];
    int n_fighters;
    int stages[40];
    int n_stages;
    int scan_idx;
    /* Focus and delete confirmation */
    int focus;
    int filter_col;
    int confirm_delete;
    int confirm_yes;
    char message[320]; /* shown in the footer for a while */
    int message_frames;
    /* A replay that needs other mods: tgg is getting them (tgg/restart.h) */
    int restarting;
    char pending[PATH_MAX_LEN + 256];
    tgg_restart_status restart;
} Browser;

extern Browser br;

/* The settings (browser.c) */
extern char setting_folder[PATH_MAX_LEN];
extern int setting_sort;
enum { SORT_NEWEST, SORT_OLDEST };

/* Lists `folder` into br, sorted, keeping the selected name if it's still
 * there. Returns -1 when the folder can't be read. */
int list_open(const char* folder);
/* Sorts br's entries again (folders first, then replays by date) and
 * rebuilds the filtered view. */
void list_sort(void);
/* Rebuilds br.view from br.entries under the current filters. */
void list_rebuild_view(const char* keep_name);
/* Progressively scans up to `budget` unread replays for filter facets and
 * matching. Returns nonzero while more replays remain unscanned. */
int list_scan_step(int budget);
/* Nonzero when any quick filter is active. */
int list_has_filters(void);
/* Resets all quick filters and rebuilds the view. */
void list_clear_filters(void);
/* Selected entry index in br.entries, or -1 when the view is empty. */
int list_selected_entry(void);
/* Deletes the currently selected replay from disk and updates br. Returns 0
 * on success, -1 on failure. */
int list_delete_selected(char* deleted_name, int cap);
/* br.entries[i]'s summary, read the first time it's asked for. */
const replay_summary* list_summary(int i);
/* The full path of br.entries[i]. */
void list_path(int i, char* out, int cap);
/* The parent of br.folder, or "" at the top. */
void list_parent(char* out, int cap);

/* Draws the screen (draw.c). */
void browser_draw(void);
/* How many rows the list shows. */
#define LIST_ROWS 8

/* The game's names (names.c). */
const char* fighter_name(int ckind);
const char* stage_name(int stkind);

#endif
