/* Direct connect's parts: the screen (connect.c), the on-screen keyboard
 * (keyboard.c), the drawing (draw.c), this machine's addresses (addr.c) and
 * comparing the host's mods with this game's (profile.c). */
#ifndef DIRECT_CONNECT_H
#define DIRECT_CONNECT_H

#include "tgg/mod.h"
#include "tgg/restart.h"
#include "tgg/screen.h"

#include "netplay.h"

#include <stdint.h>

/* Where the screen is */
enum {
    VIEW_MENU,       /* Host, Join */
    VIEW_HOSTING,    /* waiting for players to join */
    VIEW_ADDRESS,    /* typing the host's address */
    VIEW_CONNECTING, /* saying hello to it */
    VIEW_WAITING,    /* joined: waiting for the host */
    VIEW_FAILED,     /* the session ended: why */
    VIEW_DIFFERENT,  /* the host has other mods: what differs, and get them? */
    VIEW_RESTARTING, /* tgg is getting the host's mods (tgg/restart.h) */
};

/* What differs between the host's mods and this game's (profile.c) */
#define DIFF_LINES 8
#define DIFF_TEXT 64
typedef struct Diff {
    char game_there[48], game_here[48]; /* the game versions */
    char there[DIFF_LINES][DIFF_TEXT];  /* what the host has that this game doesn't */
    int n_there;                        /* may be more than DIFF_LINES */
    char here[DIFF_LINES][DIFF_TEXT];   /* and the reverse */
    int n_here;
} Diff;

/* Compares two profiles (tgg_mod_set_profile's JSON). -1 if one can't be
 * read. */
int profile_diff(const char* host_json, const char* own_json, Diff* d);

#define ADDRESS_MAX 64

typedef struct Dc {
    int view;
    int menu_sel;
    char address[ADDRESS_MAX]; /* being typed */
    int key;                   /* the keyboard's selected key */
    char message[256];
    int message_frames;
    char why[160];             /* VIEW_FAILED */
    int failed_frames;         /* VIEW_FAILED: how long it's shown */

    char joined[ADDRESS_MAX + 8]; /* the host's full address, as last joined */
    /* VIEW_DIFFERENT */
    int have_diff;
    int bad_profile; /* the host's profile couldn't be read */
    Diff diff;
    /* VIEW_RESTARTING */
    tgg_restart_status restart;
    /* This game was restarted with the host's mods to join it: leaving
     * netplay goes back to the player's own game */
    int for_netplay;
} Dc;

extern Dc dc;

/* The settings (connect.c) */
extern char setting_peer[ADDRESS_MAX];
extern int setting_port;

/* The address as typed, with the default port when it has none */
void dc_full_address(const char* typed, char* out, int cap);

/* keyboard.c: the on-screen keyboard for the address. Returns 1 when the
 * player asks to connect, -1 to go back, else 0. */
int keyboard_frame(void);
void keyboard_draw(void);

/* draw.c */
void dc_draw(void);
/* The menu's rows: Host, Join <last address>, Join another address */
int menu_rows(const char** rows, int max);

/* addr.c: this machine's addresses to tell the other player, the tailnet's
 * first; and its name */
int local_addresses(char out[][48], int max);
const char* local_name(void);

#endif
