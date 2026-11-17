/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The meeting point (meet.c): playing a friend on another network through a
 * room code on textures.gg. One game makes a room and shows its code, the
 * others join with it; netplay is a star through the host, so the meeting
 * point hands the host and each joiner the other's addresses as the joiner
 * arrives, and netplay punches through to them. After that it's an
 * ordinary netplay session: netplay_status follows it.
 *
 *     meet_make();            // or meet_join("ABC234")
 *     ...each frame:
 *     meet_status st;
 *     meet_poll(&st);
 *     MEET_SIGN_IN: show st.signin_code and st.signin_url
 *     MEET_IN_ROOM: show st.code and st.members
 *     MEET_CONNECTED: the session is up (netplay_status), go on to the lobby
 *     MEET_FAILED: show st.why
 */
#ifndef DC_MEET_H
#define DC_MEET_H

#include "netplay.h"

enum {
    MEET_OFF = 0,
    MEET_STARTING,   /* getting a ticket and opening the connection */
    MEET_SIGN_IN,    /* not signed in: signin_code to confirm at signin_url */
    MEET_IN_ROOM,    /* in a room: its code and who's in, waiting for the other game */
    MEET_READY,      /* connected to the meeting point, not in a room or a search */
    MEET_SEARCHING,  /* in a queue (meet_search), waiting to be paired */
    MEET_PUNCHING,   /* the meeting point said go: netplay is reaching the other game */
    MEET_CONNECTED,  /* the netplay session is up */
    MEET_FAILED,     /* why says what happened, for the player */
};

/* Who's in a room, as the room lists them: as many as netplay carries */
#define MEET_MEMBERS NETPLAY_MAX_PLAYERS

typedef struct meet_status {
    int state;
    char name[64];                     /* this player's textures.gg name */
    char code[8];                      /* the room's */
    char members[MEET_MEMBERS][64];    /* who's in it, the host first */
    int n_members;
    int side;                          /* MEET_PUNCHING and after: 0 hosts */
    char signin_code[32], signin_url[256];
    char why[320];
    /* MEET_SEARCHING: the queue's name, how many are searching in it, how long this
     * game has been, and a line about the last try ("Couldn't reach saavy.") */
    char label[64];
    int searching;
    double search_ms;
    char note[200];
    int queue;                         /* the session came from a queue */
    int locked;                        /* the room's host started playing */
    int games;                         /* the most games the room takes */
    /* MEET_PUNCHING and after: the games this one met in this session, by
     * name (a joiner meets only the host; the host meets every joiner) */
    char peers[MEET_MEMBERS][64];
    int n_peers;
    char left[64];                     /* the last member who left the room */
} meet_status;

/* Makes a room and waits in it. 0, or -1 when Online can't start here (no
 * tgg beside the game, say: why says so). */
int meet_make(void);
/* Joins the room with `code`. 0 or -1, as meet_make. */
int meet_join(const char* code);
void meet_poll(meet_status* out);
/* In a room whose netplay session ended (someone left, or a link dropped)
 * while the room is still open: closes the session, gathers this game's
 * addresses again and sends them, so the room meets the games still in it
 * again (MEET_IN_ROOM, then MEET_PUNCHING). 0, or -1 when there's no room to
 * go back to. */
int meet_room_again(void);
/* Leaves the room and the meeting point; a netplay session it opened stays
 * up until netplay_close. */
void meet_leave(void);

/* ---- Find a match: queues of players with the same gameplay mods (meet.c)
 *
 *     meet_online();          // sign in and connect, no room
 *     meet_counts("vs");      // ask how many search in the "vs" queue
 *     meet_count("vs", label, sizeof label);  // the last answer, -1 before one
 *     meet_search("vs");      // wait in it; MEET_SEARCHING, then MEET_PUNCHING
 *                             // and MEET_CONNECTED when paired
 *     meet_search_again();    // after a match: close the session, search again
 *
 * A failed connection searches again by itself, at the front of the queue. */

/* Connects to the meeting point without a room. 0 or -1, as meet_make. */
int meet_online(void);
/* Waits in the queue for `mode` ("vs", or a shared menu entry's id) and this
 * game's mods, connecting first if needed. 0 or -1. */
int meet_search(const char* mode);
/* Leaves the queue; stays connected (MEET_READY). */
void meet_search_stop(void);
/* Ends the session a queue started and searches the same queue again. */
void meet_search_again(void);
/* Asks how many search for `mode` (sent at most every 20 seconds a mode,
 * for up to 4 modes). */
void meet_counts(const char* mode);
/* The last count for `mode`, or -1; its queue's name into `label`. */
int meet_count(const char* mode, char* label, int cap);

#endif
