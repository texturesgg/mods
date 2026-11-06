/* Replay player: plays a Slippi replay's match in the port.
 *
 * TGG_REPLAY_PLAYER_FILE names the replay (.slp). At startup the mod reads it,
 * logs the Slippi rules its console ran (the rule mods to turn on), and boots
 * the game straight into its match with tgg_boot_match. Then, as Slippi's own
 * playback does:
 *
 * - the match is set up from the RNG seed the replay started with (a before
 *   hook on fn_8016E730, the VS match setup, before anything draws from it);
 * - each logic frame, in the pad_read event, every port gets the inputs the
 *   replay recorded for that frame, and the RNG seed is set to the replay's;
 * - a pause on the console leaves no frames in the replay, so while the match
 *   is paused every port lets go until the pause can end, then the next
 *   recorded frame presses Start again.
 *
 * Space pauses, . (or the right arrow) steps a frame while paused, = and -
 * (or up and down) double and halve the speed, 0 goes back to normal
 * (keys.c). The match pauses when the replay ends.
 */
#include "player.h"

#include "tgg/control.h"
#include "tgg/events.h"

#include <melee/gm/gmscene.h>
#include <melee/gm/gmvs.h>
#include <melee/mn/types.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/random.h>

#include <math.h>
#include <stdlib.h>

/* VsSceneController::pause_timer: frames after a pause before it can end */
#define PAUSE_TICKS 10

/* The replay, read once in init and only read after */
static Slp slp;
static int loaded;

/* What the simulation counts from frame to frame */
static struct {
    int set_up;      /* the replay's match was set up */
    int started;     /* its first frame was read */
    int frame;       /* the replay frame being played, from SLP_FIRST_FRAME */
    int pause_ticks; /* logic frames the match has been paused for */
    int ended;       /* past the replay's last frame */
} st;
TGG_STATE(st);

static void init(void)
{
    const char* path = getenv("TGG_REPLAY_PLAYER_FILE");
    if (path == NULL || path[0] == '\0') {
        tgg_log("set TGG_REPLAY_PLAYER_FILE to a replay (.slp) to play it");
        return;
    }
    const char* why = slp_read(path, &slp);
    if (why != NULL) {
        tgg_log("%s: %s", path, why);
        return;
    }
    loaded = 1;
    tgg_log("playing %s", path);
    player_log_rules(&slp);

    static StartMeleeData match;
    player_match(&slp, &match);
    tgg_boot_match(&match);
    keys_open();
}
TGG_INIT(init);

/* The VS match setup, before the stage and the players draw from the RNG */
static void before_setup(StartMeleeData* data)
{
    (void) data;
    if (loaded && !st.set_up) {
        st.set_up = 1;
        *HSD_RandSeedPtr = slp.start_seed;
    }
}
TGG_BEFORE(fn_8016E730, before_setup);

/* Sets one port to what the replay recorded: the processed state the game
 * uses, and the raw report UCF reads its sticks from (rebuilt from the
 * processed values for what older replays don't have, exact after the game's
 * clamp). */
static void play_input(HSD_PadStatus* mp, PADStatus* raw, const SlpInput* in)
{
    const PadLibData* lib = &HSD_PadLibData;
    const float stick_scale = lib->scale_stick;
    const float lr_scale = lib->scale_analogLR;

    raw->err = 0;
    raw->button = in->physical;
    raw->stickX = in->raw_x;
    raw->stickY = in->has_raw_y ? in->raw_y : (s8) lrintf(in->stick_y * stick_scale);
    raw->substickX = in->has_raw_c ? in->raw_cx : (s8) lrintf(in->cstick_x * stick_scale);
    raw->substickY = in->has_raw_c ? in->raw_cy : (s8) lrintf(in->cstick_y * stick_scale);
    raw->triggerLeft = (u8) lrintf(in->l * lr_scale);
    raw->triggerRight = (u8) lrintf(in->r * lr_scale);
    raw->analogA = 0;
    raw->analogB = 0;

    mp->err = 0;
    mp->button = in->buttons;
    mp->stickX = (s8) lrintf(in->stick_x * stick_scale);
    mp->stickY = (s8) lrintf(in->stick_y * stick_scale);
    mp->subStickX = (s8) lrintf(in->cstick_x * stick_scale);
    mp->subStickY = (s8) lrintf(in->cstick_y * stick_scale);
    mp->analogL = raw->triggerLeft;
    mp->analogR = raw->triggerRight;
    mp->analogA = 0;
    mp->analogB = 0;
    mp->nml_stickX = in->stick_x;
    mp->nml_stickY = in->stick_y;
    mp->nml_subStickX = in->cstick_x;
    mp->nml_subStickY = in->cstick_y;
    mp->nml_analogL = in->l;
    mp->nml_analogR = in->r;
    mp->nml_analogA = 0;
    mp->nml_analogB = 0;
}

static void on_pads(tgg_pads* pads)
{
    if (!loaded || !st.set_up || st.ended || !tgg_in_match()) {
        return;
    }
    /* The frame before this one: the replay's frame count moves on unless
     * the match was paused through it */
    if (!st.started) {
        st.started = 1;
        st.frame = SLP_FIRST_FRAME;
    } else if (gm_GetDbPauseFlag(1)) {
        st.pause_ticks++;
    } else {
        st.pause_ticks = 0;
        st.frame++;
    }
    const int i = st.frame - SLP_FIRST_FRAME;
    if (i >= slp.frame_count) {
        st.ended = 1;
        return;
    }
    const SlpFrame* f = &slp.frames[i];
    if (f->has_seed) {
        *HSD_RandSeedPtr = f->seed;
    }
    /* Paused: every port lets go until the pause can end (the game takes no
     * unpause for PAUSE_TICKS frames), then this frame's inputs press Start
     * again to unpause, or L, R, A and Start to end the match */
    static const SlpInput released = { .have = 1 };
    const int hold = st.pause_ticks > 0 && st.pause_ticks <= PAUSE_TICKS;
    for (int port = 0; port < 4; port++) {
        if (f->input[port].have) {
            play_input(&pads->status[port], &pads->raw[port], hold ? &released : &f->input[port]);
        }
    }
}
TGG_ON(pad_read, on_pads);

/* The playback keys, and the pause at the end, once per displayed frame */
static void on_frame(void)
{
    if (!loaded) {
        return;
    }
    static int ended;
    if (st.ended && !ended) {
        ended = 1;
        tgg_pause(1);
        tgg_log("the replay ended at frame %d", st.frame - 1);
    }
    const Keys k = keys_pressed();
    if (k.pause) {
        tgg_pause(!tgg_paused());
        tgg_log(tgg_paused() ? "paused at frame %d" : "playing from frame %d", st.frame);
    }
    if (k.step) {
        tgg_step(1);
    }
    if (k.faster || k.slower || k.normal) {
        tgg_set_speed(k.normal ? 1.0f : tgg_speed() * (k.faster ? 2.0f : 0.5f));
        tgg_log("speed %gx", (double) tgg_speed());
    }
}
TGG_ON(frame, on_frame);
