/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The meeting point (meet.c): playing a friend on another network through a
 * room code on textures.gg. One game makes a room and shows its code, the
 * other joins with it; the meeting point hands each game the other's
 * addresses, and netplay punches through to them. After that it's an
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

enum {
    MEET_OFF = 0,
    MEET_STARTING,   /* getting a ticket and opening the connection */
    MEET_SIGN_IN,    /* not signed in: signin_code to confirm at signin_url */
    MEET_IN_ROOM,    /* in a room: its code and who's in, waiting for the other game */
    MEET_PUNCHING,   /* the meeting point said go: netplay is reaching the other game */
    MEET_CONNECTED,  /* the netplay session is up */
    MEET_FAILED,     /* why says what happened, for the player */
};

#define MEET_MEMBERS 4

typedef struct meet_status {
    int state;
    char name[64];                     /* this player's textures.gg name */
    char code[8];                      /* the room's */
    char members[MEET_MEMBERS][64];    /* who's in it, the host first */
    int n_members;
    int side;                          /* MEET_PUNCHING and after: 0 hosts */
    char signin_code[32], signin_url[256];
    char why[320];
} meet_status;

/* Makes a room and waits in it. 0, or -1 when Online can't start here (no
 * tgg beside the game, say: why says so). */
int meet_make(void);
/* Joins the room with `code`. 0 or -1, as meet_make. */
int meet_join(const char* code);
void meet_poll(meet_status* out);
/* Leaves the room and the meeting point; a netplay session it opened stays
 * up until netplay_close. */
void meet_leave(void);

#endif
