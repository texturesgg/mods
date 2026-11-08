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

typedef struct Browser {
    char folder[PATH_MAX_LEN];
    Entry* entries;
    int n;
    int sel;
    int top;
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
/* Sorts br's entries again (folders first, then replays by date). */
void list_sort(void);
/* br.entries[i]'s summary, read the first time it's asked for. */
const replay_summary* list_summary(int i);
/* The full path of br.entries[i]. */
void list_path(int i, char* out, int cap);
/* The parent of br.folder, or "" at the top. */
void list_parent(char* out, int cap);

/* Draws the screen (draw.c). */
void browser_draw(void);
/* How many rows the list shows. */
#define LIST_ROWS 9

/* The game's names (names.c). */
const char* fighter_name(int ckind);
const char* stage_name(int stkind);

#endif
