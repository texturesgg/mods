/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Direct connect's parts: the screen (connect.c), the on-screen keyboard
 * (keyboard.c), the drawing (draw.c), this machine's addresses (addr.c) and
 * comparing the host's mods with this game's (profile.c). */
#ifndef DIRECT_CONNECT_H
#define DIRECT_CONNECT_H

#include "tgg/mod.h"
#include "tgg/restart.h"
#include "tgg/screen.h"

#include "netplay.h"
#include "meet.h"

#include <stdint.h>

/* What this game is doing online */
enum {
    MODE_NONE,
    MODE_ROOM,      /* made a room */
    MODE_HOST,      /* hosting by address */
    MODE_JOIN_ROOM, /* joined a room */
    MODE_JOIN,      /* joined by address */
};

/* Where the screen is */
enum {
    VIEW_ADDRESS,    /* typing the host's address or a room's code */
    VIEW_CONNECTING, /* saying hello to it */
    VIEW_FAILED,     /* the session ended: why */
    VIEW_DIFFERENT,  /* the host has other mods: what differs, and get them? */
    VIEW_RESTARTING, /* tgg is getting the host's mods (tgg/restart.h) */
    VIEW_ROOM,       /* a room on textures.gg: signing in, its code, connecting */
    VIEW_PLAY,       /* scripted room matches (roomplay.c) */
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
    char address[ADDRESS_MAX]; /* being typed */
    int typing_code;           /* VIEW_ADDRESS types a room code, not an address */
    int room;                  /* the session came through a room (meet.c) */
    int mode;                  /* MODE_*: what this game is doing online */
    char note[320];            /* the submenu's line: what went wrong */
    meet_status meet;          /* the room, as last polled */
    int key;                   /* the keyboard's selected key */
    char message[320];
    int message_frames;
    char why[320];             /* VIEW_FAILED */
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
    /* The room met again after a session ended (meet_room_again) */
    int reformed;
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

/* queue.c: Find a match. queue_open opens its screen from Online;
 * queue_autostart from TGG_DIRECT_CONNECT=search[:<mode>[:<character>[:<costume>]]] */
void queue_open(void);
int queue_autostart(const char* spec); /* 1 once its screen is up */
int queue_shown(void);
/* In a queue's search or match, the screen shown or not */
int queue_busy(void);

/* addr.c: this machine's addresses to tell the other player, the tailnet's
 * first; and its name */
int local_addresses(char out[][48], int max);
const char* local_name(void);

/* played.c: the Played with file. played_got takes a "tgg-name" message (1),
 * played_frame follows the session each frame */
int played_got(const char* text, int from);
void played_frame(void);
/* Closes the netplay session, its Played with line first */
void dc_netplay_close(void);

/* report.c: the room report for an app (TGG_DIRECT_CONNECT_REPORT), each frame */
void report_frame(void);

/* roomplay.c: scripted room matches for tests */
int roomplay_wanted(void);
int roomplay_done(void);
void roomplay_reset(void);
int roomplay_got(const char* text, int from);
void roomplay_frame(void);
void roomplay_enter(int from_match);

#endif
