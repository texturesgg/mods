/* Replays: a screen that lists a folder's Slippi replays, newest first, with
 * a preview of the selected one, and plays it through the replay player.
 *
 * The folder is the "folder" setting, else the first of Slippi's own replay
 * folders that exists, else the home folder. Folders show above the
 * replays; ".." goes up. Y keeps the folder shown as the setting, X changes
 * the order. The screen's functions are presentation code: what's in the
 * player's folders never reaches a match. */
#include "browser.h"

#include "tgg/files.h"
#include "tgg/input.h"
#include "tgg/screen.h"
#include "tgg/settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char setting_folder[PATH_MAX_LEN];
TGG_SETTING_TEXT_LOCAL(setting_folder, "folder", "Replay folder", "");
int setting_sort;
TGG_SETTING_CHOICE_LOCAL(setting_sort, "sort", "Order", SORT_NEWEST, "Newest first", "Oldest first");

static void say(const char* text)
{
    snprintf(br.message, sizeof br.message, "%s", text);
    br.message_frames = 180;
}

static int readable(const char* folder)
{
    return folder[0] != '\0' && tgg_dir_list(folder, NULL, NULL) >= 0;
}

/* The folder to start in */
static void open_start(void)
{
    if (readable(setting_folder) && list_open(setting_folder) == 0) {
        return;
    }
    const char* slippi[4];
    const int n = tgg_slippi_folders(slippi, 4);
    for (int i = 0; i < n; i++) {
        if (readable(slippi[i]) && list_open(slippi[i]) == 0) {
            return;
        }
    }
    const char* home = getenv("HOME");
    if (home == NULL) {
        home = getenv("USERPROFILE");
    }
    if (home == NULL || list_open(home) != 0) {
        list_open("/");
    }
    say("No Slippi folder found. Find yours, then press Y.");
}

static void enter(int from_match)
{
    if (from_match) {
        /* Back from a replay: the same list, the same row */
        list_open(br.folder);
        return;
    }
    if (br.folder[0] == '\0' || !readable(br.folder)) {
        open_start();
    } else {
        list_open(br.folder);
    }
}

static void move(int by)
{
    if (br.n == 0) {
        return;
    }
    int to = br.sel + by;
    if (by == 1 || by == -1) {
        to = (to + br.n) % br.n;
    } else {
        to = to < 0 ? 0 : to >= br.n ? br.n - 1 : to;
    }
    if (to != br.sel) {
        br.sel = to;
        tgg_menu_sound(TGG_SOUND_MOVE);
    }
}

static void go_up(void)
{
    char parent[PATH_MAX_LEN];
    list_parent(parent, sizeof parent);
    if (parent[0] == '\0') {
        tgg_menu_sound(TGG_SOUND_ERROR);
        return;
    }
    char from[PATH_MAX_LEN];
    const char* slash = strrchr(br.folder, '/');
    snprintf(from, sizeof from, "%s", slash != NULL ? slash + 1 : br.folder);
    if (list_open(parent) == 0) {
        tgg_menu_sound(TGG_SOUND_BACK);
        for (int i = 0; i < br.n; i++) {
            if (strcmp(br.entries[i].name, from) == 0) {
                br.sel = i;
            }
        }
    }
}

static void choose(void)
{
    if (br.n == 0) {
        return;
    }
    if (strcmp(br.entries[br.sel].name, "..") == 0) {
        go_up();
        return;
    }
    char path[PATH_MAX_LEN + 256];
    list_path(br.sel, path, sizeof path);
    if (br.entries[br.sel].is_dir) {
        if (list_open(path) == 0) {
            tgg_menu_sound(TGG_SOUND_ACCEPT);
        } else {
            tgg_menu_sound(TGG_SOUND_ERROR);
            say("Can't open that folder.");
        }
        return;
    }
    const char* why = NULL;
    if (replay_play(path, &why) == 0) {
        tgg_menu_sound(TGG_SOUND_ACCEPT);
    } else {
        tgg_menu_sound(TGG_SOUND_ERROR);
        char text[160];
        snprintf(text, sizeof text, "Can't play it: %s.", why != NULL ? why : "unknown");
        say(text);
    }
}

static void frame(void)
{
    if (br.message_frames > 0) {
        br.message_frames--;
    }
    const uint32_t in = tgg_menu_input();
    if (in & TGG_MENU_BACK) {
        tgg_menu_sound(TGG_SOUND_BACK);
        tgg_screen_close();
        return;
    }
    if (in & TGG_MENU_UP) {
        move(-1);
    }
    if (in & TGG_MENU_DOWN) {
        move(1);
    }
    if (in & (TGG_MENU_LEFT | TGG_MENU_L)) {
        move(-LIST_ROWS);
    }
    if (in & (TGG_MENU_RIGHT | TGG_MENU_R)) {
        move(LIST_ROWS);
    }
    if (in & (TGG_MENU_ACCEPT | TGG_MENU_START)) {
        choose();
    }
    if (in & TGG_MENU_X) {
        setting_sort = setting_sort == SORT_NEWEST ? SORT_OLDEST : SORT_NEWEST;
        tgg_setting_store(&setting_sort);
        list_sort();
        tgg_menu_sound(TGG_SOUND_MOVE);
    }
    if (in & TGG_MENU_Y) {
        snprintf(setting_folder, sizeof setting_folder, "%s", br.folder);
        if (tgg_setting_store(setting_folder) == 0) {
            tgg_menu_sound(TGG_SOUND_ACCEPT);
            say("Replays will open here from now on.");
        } else {
            tgg_menu_sound(TGG_SOUND_ERROR);
        }
    }
}

static const tgg_screen browser = {
    .title = "Replays",
    .enter = enter,
    .frame = frame,
    .draw = browser_draw,
};
TGG_SCREEN("browser", browser);
