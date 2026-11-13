/* SPDX-License-Identifier: GPL-3.0-or-later */
/* What the player reads from a Slippi replay (.slp): the match's setup, the
 * gecko codes the console ran, and each frame's inputs and RNG seed. The
 * format is project-slippi's slippi-wiki SPEC.md. */
#ifndef REPLAY_PLAYER_SLP_H
#define REPLAY_PLAYER_SLP_H

#include <stddef.h>
#include <stdint.h>

/* The frame number of a replay's first frame. Slippi counts from -123, so
 * frame 0 is the first one players can act on. */
#define SLP_FIRST_FRAME (-123)

/* One port's pre-frame update: the inputs the game used that frame. */
typedef struct SlpInput {
    uint8_t have;
    uint8_t has_raw_y, has_raw_c;
    int8_t raw_x, raw_y, raw_cx, raw_cy;
    uint16_t physical; /* the controller's buttons, as PADStatus has them */
    uint32_t buttons;  /* the processed buttons, as HSD_PadStatus has them */
    float stick_x, stick_y, cstick_x, cstick_y;
    float l, r;
} SlpInput;

typedef struct SlpFrame {
    uint8_t has_seed;
    uint32_t seed;
    SlpInput input[4]; /* by port; a follower (Nana) plays from her leader's */
} SlpFrame;

typedef struct Slp {
    uint8_t version[3];
    uint8_t info[312];   /* the Game Info block */
    uint32_t start_seed; /* the RNG seed the match was set up with */
    uint8_t major_scene; /* 8 is Slippi Online */
    int transformations; /* Pokemon Stadium transformation events */
    SlpFrame* frames;    /* from SLP_FIRST_FRAME */
    int frame_count;     /* up to the last frame with a post-frame update */
    uint8_t* gecko;      /* the gecko code list the console ran */
    size_t gecko_len;
    /* The metadata's content map (the port's recorder): the fighter a number
     * stands for, and each port's added costume; "" for none */
    struct {
        int number;
        char id[200];
    } fighters[4];
    int n_fighters;
    char costumes[4][200];
} Slp;

/* Reads `path` into `slp`. Returns NULL, or what's wrong with the file. */
const char* slp_read(const char* path, Slp* slp);

static inline uint16_t slp_be16(const uint8_t* p)
{
    return (uint16_t) (p[0] << 8 | p[1]);
}

static inline uint32_t slp_be32(const uint8_t* p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

#endif
