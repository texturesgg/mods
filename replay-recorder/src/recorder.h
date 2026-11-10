/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The replay recorder's parts: what the simulation captures each frame
 * (capture.c), the file it becomes (slp.c), and the mod that ties them
 * together (mod.c). */
#ifndef REPLAY_RECORDER_H
#define REPLAY_RECORDER_H

#include "tgg/mod.h"

#include <stddef.h>
#include <stdint.h>

typedef struct StartMeleeData StartMeleeData;

/* Slippi counts a match's frames from -123: frame 0 is the first one the
 * players can act on. */
#define REC_FIRST_FRAME (-123)

/* The Slippi version the files declare, and the payload sizes it has (the
 * command byte not counted). */
#define REC_VERSION_MAJOR 3
#define REC_VERSION_MINOR 18
#define REC_GAME_START_SIZE 760
#define REC_PRE_SIZE 66
#define REC_POST_SIZE 84
#define REC_GAME_END_SIZE 6
#define REC_FRAME_START_SIZE 12
#define REC_ITEM_SIZE 44
#define REC_BOOKEND_SIZE 8
#define REC_FOD_SIZE 9
#define REC_WHISPY_SIZE 5
#define REC_STADIUM_SIZE 8
/* Slippi records at most this many items a frame */
#define REC_MAX_ITEMS 15

enum {
    CMD_GAME_START = 0x36,
    CMD_PRE = 0x37,
    CMD_POST = 0x38,
    CMD_GAME_END = 0x39,
    CMD_FRAME_START = 0x3A,
    CMD_ITEM = 0x3B,
    CMD_BOOKEND = 0x3C,
    CMD_FOD = 0x3F,
    CMD_WHISPY = 0x40,
    CMD_STADIUM = 0x41,
};

/* One frame's events, in the order the console sends them, without the
 * bookend: frame start, each fighter's pre-frame update, each one's
 * post-frame update, the stage's events, the items. Up to 8 fighters
 * (Ice Climbers are two) and 15 items. */
#define REC_FRAME_BYTES 2304

typedef struct RecFrame {
    int index;       /* the Slippi frame, from REC_FIRST_FRAME */
    int match_frame; /* tgg_frame() of the run that made it, -1 without rollback */
    int len;
    uint8_t bytes[REC_FRAME_BYTES];
} RecFrame;

/* A match being recorded, as the simulation set it up */
typedef struct RecMatch {
    uint8_t info[0x138]; /* the Game Info block: the console's StartMeleeData */
    uint32_t seed;       /* the RNG seed the match was set up with */
    uint8_t nametags[4][16];
    uint8_t ucf[4];      /* each port's controller fix: 1 UCF, 0 none */
    uint8_t frozen_stadium;
    uint8_t major_scene, minor_scene;
    uint8_t language;
} RecMatch;

/* How it ended */
typedef struct RecEnd {
    int known;
    uint8_t method; /* 1 time, 2 game, 7 no contest */
    int8_t lras;    /* the port that quit with L+R+A+Start, or -1 */
    int8_t placements[4];
} RecEnd;

/* capture.c: the simulation's side */
void capture_start(void);
/* Stops recording: the match isn't one to record. */
void capture_stop(void);
/* The frames ready to write: up to (not counting) the Slippi frame
 * returned, given the newest final frame (tgg_frame_final). */
int capture_ready(int final);
/* The frame `index`, or NULL when it isn't kept. */
const RecFrame* capture_frame(int index);
/* The Slippi frames captured so far, by the latest run. */
int capture_count(void);
/* How the match ended, once it has. */
const RecEnd* capture_end(void);

/* slp.c: the file */
typedef struct SlpFile SlpFile;
/* Creates `path` and writes everything up to the frames. NULL on failure. */
SlpFile* slp_create(const char* path, const RecMatch* match);
void slp_frame(SlpFile* f, const RecFrame* frame);
/* Writes the end (when known), the metadata and the raw element's length,
 * and closes the file. `start_at` is ISO 8601 in UTC. */
int slp_finish(SlpFile* f, const RecEnd* end, const char* start_at);

#endif
