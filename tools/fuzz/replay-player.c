/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Fuzzes tgg.replay-player's readers of a Slippi replay (.slp), which can
 * come from anyone: each input is written to a file and read the way a
 * replay is played (slp_read) and listed (replay_read_summary). */
#include "../../replay-player/src/slp.h"
#include "port.h"
#include "replay_player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char path[64];

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (path[0] == '\0') {
        snprintf(path, sizeof path, "/tmp/fuzz-slp-%d.slp", (int) getpid());
    }
    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        abort();
    }
    fwrite(data, 1, size, f);
    fclose(f);

    static Slp slp;
    slp_read(path, &slp);
    /* Touch what the player reads: every frame's input and the gecko list */
    volatile uint32_t sink = 0;
    for (int i = 0; i < slp.frame_count; i++) {
        sink ^= slp.frames[i].seed ^ slp.frames[i].input[i & 3].buttons;
    }
    for (size_t i = 0; i < slp.gecko_len; i++) {
        sink ^= slp.gecko[i];
    }
    free(slp.frames);
    free(slp.gecko);

    replay_summary summary;
    replay_read_summary(path, &summary);
    return 0;
}

/* The API the mod offers, which its header records; the driver doesn't play */
int replay_play(const char* path, const char** why)
{
    (void) path, (void) why;
    return -1;
}

int replay_playing(void)
{
    return 0;
}
