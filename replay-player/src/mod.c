/* Replay player: plays a Slippi replay's match in the port.
 *
 * Two ways in. Another mod's screen calls replay_play (include/
 * replay_player.h), and the match starts from that screen, which comes back
 * when the match ends. Or TGG_REPLAY_PLAYER_FILE names a replay (.slp), and
 * the game boots straight into its match with tgg_boot_match. Either way the
 * mod reads the replay, logs the Slippi rules its console ran (the rule mods
 * to turn on), then, as Slippi's own playback does:
 *
 * - the match is set up from the RNG seed the replay started with (the
 *   match_setup event, before anything draws from it);
 * - each logic frame, in the pad_read event, every port gets the inputs the
 *   replay recorded for that frame, and the RNG seed is set to the replay's;
 * - after each logic frame (tick_end), the replay's frame moves on, unless
 *   the match was paused through it: a pause on the console leaves no frames
 *   in the replay, so while the match is paused every port lets go until the
 *   pause can end, then the next recorded frame presses Start again.
 *
 * The viewer's keys and controllers (keys.c) pause, step a frame, change the
 * speed and, for a replay a screen started, leave it. Booted from the
 * environment, the match pauses when the replay ends, unless the game was
 * restarted to play it (tgg/restart.h): then the player's own game comes back,
 * as it does when they leave. Started from a screen, the screen comes back.
 */
#include "player.h"
#include "replay_player.h"

#include "tgg/control.h"
#include "tgg/draw.h"
#include "tgg/events.h"
#include "tgg/restart.h"
#include "tgg/screen.h"

#include <melee/gm/gmscene.h>
#include <melee/gm/gmvs.h>
#include <melee/mn/types.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/random.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* VsSceneController::pause_timer: frames after a pause before it can end */
#define PAUSE_TICKS 10
/* Displayed frames a replay a screen started waits past its end for the
 * game to end the match itself */
#define END_WAIT 120

/* The replay, read outside a match and only read during one */
static Slp slp;
static int loaded;
/* It came from a screen (replay_play), which comes back after */
static int from_screen;

/* What the simulation counts from frame to frame */
static struct {
    int set_up;      /* the replay's match was set up */
    int started;     /* its first frame was read */
    int frame;       /* the replay frame being played, from SLP_FIRST_FRAME */
    int pause_ticks; /* logic frames the match has been paused for */
    int ended;       /* past the replay's last frame */
} st;
TGG_STATE(st);

/* What the viewer sees and changed, outside the simulation */
static struct {
    int shown_frames; /* displayed frames since the replay started */
    int ended_frames; /* displayed frames since it ended */
    int changed;      /* the viewer paused or changed the speed */
    int was_in_match;
} view;

static const char* load(const char* path)
{
    free(slp.frames);
    free(slp.gecko);
    memset(&slp, 0, sizeof slp);
    loaded = 0;
    memset(&st, 0, sizeof st);
    memset(&view, 0, sizeof view);
    const char* why = slp_read(path, &slp);
    if (why != NULL) {
        tgg_log("%s: %s", path, why);
        return why;
    }
    loaded = 1;
    tgg_log("playing %s", path);
    player_log_rules(&slp);
    return NULL;
}

static void init(void)
{
    const char* path = getenv("TGG_REPLAY_PLAYER_FILE");
    if (path == NULL || path[0] == '\0') {
        return;
    }
    if (load(path) != NULL) {
        return;
    }
    static StartMeleeData match;
    player_match(&slp, &match);
    tgg_boot_match(&match);
}
TGG_INIT(init);

int replay_play(const char* path, const char** why)
{
    const char* unused;
    why = why != NULL ? why : &unused;
    if (tgg_screen_shown() == NULL || tgg_in_match()) {
        *why = "a replay plays from a screen";
        return -1;
    }
    *why = load(path);
    if (*why != NULL) {
        return -1;
    }
    static StartMeleeData match;
    player_match(&slp, &match);
    if (tgg_screen_play_match(&match) != 0) {
        loaded = 0;
        *why = "a replay plays from a screen";
        return -1;
    }
    from_screen = 1;
    return 0;
}

int replay_playing(void)
{
    return loaded;
}

/* The match's setup, before the stage and the players draw from the RNG */
static void on_setup(const StartMeleeData* data)
{
    (void) data;
    if (loaded && !st.set_up) {
        st.set_up = 1;
        *HSD_RandSeedPtr = slp.start_seed;
    }
}
TGG_ON(match_setup, on_setup);

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
    if (!st.started) {
        st.started = 1;
        st.frame = SLP_FIRST_FRAME;
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

/* After the frame: the replay's frame moves on unless the match was paused
 * through it */
static void on_tick_end(void)
{
    if (!st.started || st.ended || !tgg_in_match()) {
        return;
    }
    if (gm_GetDbPauseFlag(1)) {
        st.pause_ticks++;
    } else {
        st.pause_ticks = 0;
        st.frame++;
    }
}
TGG_ON(tick_end, on_tick_end);

/* A replay a screen started is over once its match is: the next match is
 * nobody's replay */
static void on_match_end(void)
{
    if (from_screen && !tgg_resim()) {
        loaded = 0;
        from_screen = 0;
    }
}
TGG_ON(match_end, on_match_end);

/* Pause and speed are the viewer's, for this replay only */
static void reset_controls(void)
{
    if (view.changed) {
        view.changed = 0;
        tgg_pause(0);
        tgg_set_speed(1.0f);
    }
}

/* The controls, and the replay's end, once per displayed frame */
static void on_frame(void)
{
    const int in_match = tgg_in_match();
    if (!in_match) {
        if (view.was_in_match) {
            reset_controls();
            /* The match of a replay the game restarted to play is over (it
             * can end before the replay's last frame): back to the player's
             * own game, when this one is a restart */
            if (loaded && !from_screen) {
                tgg_restart_back();
            }
        }
        view.was_in_match = 0;
        return;
    }
    view.was_in_match = 1;
    if (!loaded) {
        return;
    }
    view.shown_frames++;
    if (st.ended) {
        view.ended_frames++;
        if (from_screen) {
            /* The game ends the match itself after GAME!; a replay cut short
             * (a disconnect) ends it here */
            if (view.ended_frames == END_WAIT) {
                reset_controls();
                tgg_end_match();
            }
        } else {
            if (view.ended_frames == 1) {
                tgg_pause(1);
                view.changed = 1;
                tgg_log("the replay ended at frame %d", st.frame - 1);
            }
            /* A game restarted to play this replay goes back to the player's
             * own; any other stays paused on the last frame */
            if (view.ended_frames == END_WAIT) {
                tgg_restart_back();
            }
        }
    }
    const Keys k = keys_pressed();
    if (k.leave && from_screen) {
        reset_controls();
        tgg_end_match();
        return;
    }
    if (k.leave) {
        tgg_restart_back();
    }
    if (k.pause) {
        tgg_pause(!tgg_paused());
        view.changed = 1;
        tgg_log(tgg_paused() ? "paused at frame %d" : "playing from frame %d",
                tgg_paused() ? st.frame - 1 : st.frame);
    }
    if (k.step) {
        tgg_step(1);
    }
    if (k.faster || k.slower || k.normal) {
        tgg_set_speed(k.normal ? 1.0f : tgg_speed() * (k.faster ? 2.0f : 0.5f));
        view.changed = 1;
        tgg_log("speed %gx", (double) tgg_speed());
    }
}
TGG_ON(frame, on_frame);

/* What the viewer changed, in the corner; the controls, at first */
static void on_hud(void)
{
    if (!loaded || !tgg_in_match()) {
        return;
    }
    char text[48];
    text[0] = '\0';
    if (tgg_paused()) {
        snprintf(text, sizeof text, "Paused");
    } else if (tgg_speed() != 1.0f) {
        snprintf(text, sizeof text, tgg_speed() > 1 ? "%gx" : "1/%gx",
                 (double) (tgg_speed() > 1 ? tgg_speed() : 1 / tgg_speed()));
    }
    const tgg_rect b = tgg_draw_bounds();
    if (text[0] != '\0') {
        const float w = tgg_text_width(22, text) + 20;
        tgg_draw_rect(b.x + 16, 16, w, 30, TGG_RGBA(0, 0, 0, 150));
        tgg_draw_text(b.x + 26, 20, 22, TGG_RGBA(255, 255, 255, 255), TGG_ALIGN_LEFT, text);
    }
    if (from_screen && view.shown_frames < 240) {
        const char* help = "Start: Pause    Up/Down: Speed    B: Back";
        const float w = tgg_text_width(18, help) + 24;
        tgg_draw_rect(320 - w / 2, 4, w, 26, TGG_RGBA(0, 0, 0, 150));
        tgg_draw_text(320, 8, 18, TGG_RGBA(220, 225, 240, 255), TGG_ALIGN_CENTER, help);
    }
}
TGG_ON(hud_draw, on_hud);
