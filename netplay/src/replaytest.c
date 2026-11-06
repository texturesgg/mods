/* The replay test: TGG_NETPLAY_REPLAY=<file.slp>. Two games play a Slippi
 * replay's match against each other over the network, each playing one
 * side's recorded inputs, so both should end where the console did (the
 * port's replay check, TGG_REPLAY with TGG_REPLAY_CHECK_ONLY=1, compares).
 *
 * A replay of an online match has a frame for every logic frame the match
 * ran (online matches never pause), so match frame n is the replay's frame
 * SLP_FIRST_FRAME + n. Each frame's RNG seed is set to the one the replay
 * recorded, as Slippi's own netplay keeps the two consoles' seeds together.
 * The input playing turns a recorded frame into what the game holds the same
 * way the replay player does. */
#include "netplay.h"
#include "slp.h"

#include "tgg/control.h"

#include <melee/mn/types.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/random.h>

#include <math.h>
#include <string.h>

static Slp slp;
static int ports[2];
static uint32_t hash;

/* Slippi Online's pauser check (Online/Core/InitPause.asm): the match itself
 * never pauses */
static int online_pauser(void)
{
    return -1;
}

int replaytest_open(const char* path)
{
    const char* why = slp_read(path, &slp);
    if (why != NULL) {
        tgg_log("%s: %s", path, why);
        return 0;
    }
    int n = 0;
    for (int port = 0; port < 4; port++) {
        if (slp.frame_count > 0 && slp.frames[slp.frame_count - 1].input[port].have) {
            if (n < 2) {
                ports[n] = port;
            }
            n++;
        }
    }
    if (n != 2) {
        tgg_log("%s: the replay test plays two players, and this replay has %d", path, n);
        return 0;
    }
    /* FNV-1a over the Game Info block and the frame count: both sides must
     * play the same replay */
    hash = 0x811c9dc5u;
    for (size_t i = 0; i < sizeof slp.info; i++) {
        hash = (hash ^ slp.info[i]) * 0x01000193u;
    }
    hash = (hash ^ (uint32_t) slp.frame_count) * 0x01000193u;

    static StartMeleeData match;
    tgg_match_from_console(&match, slp.info);
    if (slp.major_scene == 8) {
        match.rules.disable_pausing = 1;
        match.rules.check_for_pauser_override = online_pauser;
    }
    tgg_boot_match(&match);
    if (slp.major_scene != 8) {
        tgg_log("an offline replay: if its match paused, the frames after the pause won't line up");
    }
    tgg_log("replay test: %s, Slippi %u.%u.%u, stage %u, %d frames, ports %d and %d", path,
            slp.version[0], slp.version[1], slp.version[2], slp_be16(slp.info + 0xE),
            slp.frame_count, ports[0] + 1, ports[1] + 1);
    return 1;
}

int replaytest_port(int side)
{
    return ports[side];
}

uint32_t replaytest_start_seed(void)
{
    return slp.start_seed;
}

uint32_t replaytest_hash(void)
{
    return hash;
}

static const SlpFrame* frame_at(int frame)
{
    const int i = frame;
    return i >= 0 && i < slp.frame_count ? &slp.frames[i] : NULL;
}

int replaytest_ended(int frame)
{
    return frame >= slp.frame_count;
}

WireInput replaytest_input(int port, int frame)
{
    const SlpFrame* f = frame_at(frame);
    if (f == NULL || !f->input[port].have) {
        return wire_neutral();
    }
    const SlpInput* in = &f->input[port];
    const PadLibData* lib = &HSD_PadLibData;
    const float stick_scale = lib->scale_stick;
    const float lr_scale = lib->scale_analogLR;
    HSD_PadStatus s;
    PADStatus raw;
    memset(&s, 0, sizeof s);
    memset(&raw, 0, sizeof raw);
    raw.button = in->physical;
    raw.stickX = in->raw_x;
    raw.stickY = in->has_raw_y ? in->raw_y : (s8) lrintf(in->stick_y * stick_scale);
    raw.substickX = in->has_raw_c ? in->raw_cx : (s8) lrintf(in->cstick_x * stick_scale);
    raw.substickY = in->has_raw_c ? in->raw_cy : (s8) lrintf(in->cstick_y * stick_scale);
    raw.triggerLeft = (u8) lrintf(in->l * lr_scale);
    raw.triggerRight = (u8) lrintf(in->r * lr_scale);
    s.button = in->buttons;
    s.stickX = (s8) lrintf(in->stick_x * stick_scale);
    s.stickY = (s8) lrintf(in->stick_y * stick_scale);
    s.subStickX = (s8) lrintf(in->cstick_x * stick_scale);
    s.subStickY = (s8) lrintf(in->cstick_y * stick_scale);
    s.analogL = raw.triggerLeft;
    s.analogR = raw.triggerRight;
    s.nml_stickX = in->stick_x;
    s.nml_stickY = in->stick_y;
    s.nml_subStickX = in->cstick_x;
    s.nml_subStickY = in->cstick_y;
    s.nml_analogL = in->l;
    s.nml_analogR = in->r;
    return wire_from_pad(&s, &raw);
}

void replaytest_seed(int frame)
{
    const SlpFrame* f = frame_at(frame);
    if (f != NULL && f->has_seed) {
        *HSD_RandSeedPtr = f->seed;
    }
}
