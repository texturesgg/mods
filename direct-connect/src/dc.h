/* Direct connect's parts: the screen and the lobby (connect.c), the
 * on-screen keyboard (keyboard.c), the drawing (draw.c), this machine's
 * addresses (addr.c) and the game's names (names.c). */
#ifndef DIRECT_CONNECT_H
#define DIRECT_CONNECT_H

#include "tgg/mod.h"

#include "netplay.h"

#include <stdint.h>

/* Where the screen is */
enum {
    VIEW_MENU,       /* Host, Join */
    VIEW_HOSTING,    /* waiting for the other player */
    VIEW_ADDRESS,    /* typing the host's address */
    VIEW_CONNECTING, /* saying hello to it */
    VIEW_LOBBY,      /* picking characters and the stage */
    VIEW_FAILED,     /* the session ended: why */
};

#define ADDRESS_MAX 64

/* One player's pick in the lobby */
typedef struct Pick {
    int known;   /* the other side's arrived */
    int ckind;   /* CharacterKind */
    int costume; /* 0 to 5 */
    int ready;   /* Start pressed: locked in */
} Pick;

typedef struct Lobby {
    Pick me, them;
    int cursor;  /* the grid cell */
    int stage;   /* an index into the stage list */
    int stocks, minutes; /* side 0's rules */
    int started; /* netplay_play was called for this match */
    int have_result;
    char result[96]; /* the last match, for the player */
} Lobby;

typedef struct Dc {
    int view;
    int menu_sel;
    char address[ADDRESS_MAX]; /* being typed */
    int key;                   /* the keyboard's selected key */
    char message[160];
    int message_frames;
    char why[160];             /* VIEW_FAILED */
    Lobby lobby;
} Dc;

extern Dc dc;

/* The settings (connect.c) */
extern char setting_peer[ADDRESS_MAX];
extern int setting_port;
extern int setting_stocks;
extern int setting_minutes;
extern int setting_stage;
extern int setting_character;
extern int setting_costume;

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

/* The fighters by CharacterKind, and the stages the host picks from, by
 * index, with their StKind */
#define FIGHTERS(X)                                                            \
    X(0, "Captain Falcon") X(1, "Donkey Kong") X(2, "Fox") X(3, "Mr. Game & Watch")      \
    X(4, "Kirby") X(5, "Bowser") X(6, "Link") X(7, "Luigi") X(8, "Mario") X(9, "Marth")   \
    X(10, "Mewtwo") X(11, "Ness") X(12, "Peach") X(13, "Pikachu") X(14, "Ice Climbers")   \
    X(15, "Jigglypuff") X(16, "Samus") X(17, "Yoshi") X(18, "Zelda") X(19, "Sheik")       \
    X(20, "Falco") X(21, "Young Link") X(22, "Dr. Mario") X(23, "Roy") X(24, "Pichu")     \
    X(25, "Ganondorf")
#define STAGES(X)                                                              \
    X(31, "Battlefield") X(32, "Final Destination") X(28, "Dream Land")                   \
    X(8, "Yoshi's Story") X(2, "Fountain of Dreams") X(3, "Pokemon Stadium")             \
    X(4, "Princess Peach's Castle") X(5, "Kongo Jungle") X(6, "Brinstar")                \
    X(7, "Corneria") X(9, "Onett") X(10, "Mute City") X(11, "Rainbow Cruise")             \
    X(12, "Jungle Japes") X(13, "Great Bay") X(14, "Hyrule Temple")                       \
    X(15, "Brinstar Depths") X(16, "Yoshi's Island") X(17, "Green Greens")               \
    X(18, "Fourside") X(19, "Mushroom Kingdom") X(20, "Mushroom Kingdom II")              \
    X(22, "Venom") X(23, "Poke Floats") X(24, "Big Blue") X(25, "Icicle Mountain")        \
    X(27, "Flat Zone") X(29, "Yoshi's Island (N64)") X(30, "Kongo Jungle (N64)")

/* names.c: the character grid, in the character select's order, and the
 * stages */
#define GRID_COLS 9
#define GRID_CELLS 26
int grid_ckind(int cell);
int grid_cell(int ckind);
const char* fighter_name(int ckind);
int stage_count(void);
int stage_kind(int index);
const char* stage_name(int index);
/* The next costume of `ckind` after `costume`, by `dir` (1 or -1) */
int next_costume(int ckind, int costume, int dir);

#endif
