/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Replay recorder: saves each VS match played on the port as a Slippi replay
 * (.slp) that Slippi's tools open.
 *
 * The simulation captures each frame's events (capture.c). Everything that
 * touches the file is presentation, outside the simulation: in the advance
 * event, before each logic frame, the frames tgg_frame_final says are final
 * are written in order (slp.c), so under rollback a frame is written once,
 * from its last run, and never from a guess. The file is finished (the end,
 * the metadata, the raw element's length) once the match has ended and every
 * frame is final, when the match scene has left, or when the game quits.
 * A snapshot load (snapshot_loaded) ends the replay where the match jumped.
 *
 * Matches the replay player plays are skipped, unless the `playback`
 * setting asks for them: the player says so through its API, which this
 * mod asks for with tgg_mod_export, so it doesn't need the player installed.
 */
#include "recorder.h"

#include "tgg/game/bridge.h"
#include "tgg/game/hooks.h"
#include "tgg/content.h"
#include "tgg/control.h"
#include "tgg/events.h"
#include "tgg/files.h"
#include "tgg/internal/companion.h"
#include "tgg/rollback.h"
#include "tgg/settings.h"

#include <melee/gm/forward.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_16F1.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/types.h>
#include <melee/lb/lblanguage.h>
#include <melee/mn/mnname.h>
#include <melee/mn/types.h>
#include <melee/pl/forward.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/random.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#endif

static int enabled;
TGG_SETTING_BOOL_LOCAL(enabled, "record", "Record matches", 1);

static char folder[512];
TGG_SETTING_TEXT_LOCAL(folder, "folder", "Replay folder", "");

static int playback;
TGG_SETTING_BOOL_LOCAL(playback, "playback", "Record replays being watched", 0);

/* The match the simulation set up to record, and the file it goes to; only
 * presentation code uses these */
static RecMatch match;
static int pending;    /* a match was set up to record */
static int match_left; /* its scene left (match_end) */
/* The next match, set up while the last one's file was still open (a
 * sudden death straight after a match, under rollback): it starts once that
 * file is finished, before the new match's first frame */
static RecMatch queued;
static int queued_pending;
static SlpFile* file;
static int next;       /* the next Slippi frame to write */
static char path[1024];
static char start_at[32];

/* The replay player's replay_playing, when it's loaded */
static int (*player_playing)(void);

/* The controllers as the game copied them, which Slippi reads A from */
static HSD_PadStatus* pad_copy;
TGG_GAME_SYMBOL(pad_copy, "HSD_PadCopyStatus");

static void init(void)
{
    tgg_companion_add_cap("replay_record");
    player_playing = (int (*)(void)) tgg_mod_export("tgg.replay-player", "replay_playing@1");
    /* Every match starts fresh, as the player plays the replay */
    tgg_match_start_fresh(1);
}
TGG_INIT(init);

/* The save's unlocks, as the bytes unlocking everything would change (the
 * game's own unlock-everything calls, as tgg.unlocks makes them), with their
 * values. The save is put back as it was. */
static void read_unlocks(RecMatch* m)
{
    GmSaveData* save = &gmMainLib_GetCardData()->save_data;
    static GmSaveData before;
    _Static_assert(sizeof before == REC_SAVE_SIZE, "the save's size");
    memcpy(&before, save, sizeof before);
    save->x186C = 0xFF; /* unlocked features */
    gm_80164F18();      /* characters */
    gm_8016468C();      /* stages */
    gm_8017297C();      /* unlockables */
    gm_801741FC();      /* trophies */
    const uint8_t* now = (const uint8_t*) save;
    const uint8_t* was = (const uint8_t*) &before;
    m->n_unlocks = 0;
    for (int i = 0; i < REC_SAVE_SIZE; i++) {
        if (now[i] != was[i]) {
            m->unlock_at[m->n_unlocks] = (uint16_t) i;
            m->unlock_value[m->n_unlocks] = was[i];
            m->n_unlocks++;
        }
    }
    memcpy(save, &before, sizeof before);
}

/* What the console records of a match's setup, at the point Slippi does: the
 * start of the VS scene's setup, before anything draws from the RNG */
static void on_setup(const tgg_match* setup)
{
    const int busy = file != NULL;
    if (!busy) {
        pending = 0;
        match_left = 0;
    }
    queued_pending = 0;
    if (!enabled || gm_GetCurrentGameMode() != GM_VS ||
        (player_playing != NULL && player_playing() && !playback))
    {
        if (!busy) {
            capture_stop();
        }
        return;
    }
    /* A replay has four ports: a match with more players, or one in a slot
     * past the fourth, isn't recorded */
    int players = 0;
    int past_four = 0;
    for (int i = 0; i < TGG_MATCH_PLAYERS; i++) {
        if (tgg_match_player(setup, i) != TGG_PLAYER_NONE) {
            players++;
            past_four |= i >= 4;
        }
    }
    if (players > 4 || past_four) {
        tgg_log("not recording this match: replays hold 4 players and it has %d", players);
        if (!busy) {
            capture_stop();
        }
        return;
    }
    static RecMatch m;
    memset(&m, 0, sizeof m);
    read_unlocks(&m);
    memcpy(m.info, setup->console, TGG_CONSOLE_MATCH_SIZE);
    StartMeleeData start;
    tgg_match_to_game(setup, &start);
    const StartMeleeData* data = &start;
    for (int i = 0; i < 4; i++) {
        uint8_t* p = m.info + 0x60 + 0x24 * i;
        const PlayerInitData* pl = &data->players[i];
        /* Zelda with A held as the match starts plays as Sheik, which the
         * console records as Sheik */
        if (pl->slot_type == Gm_PKind_Human && p[0] == 0x12 &&
            (pad_copy[i].button & HSD_PAD_A))
        {
            p[0] = 0x13;
        }
        /* A human's name tag, Shift JIS */
        if (pl->slot_type == Gm_PKind_Human && pl->nametag != 0x78) {
            const char* tag = GetNameText(pl->nametag);
            if (tag != NULL) {
                memcpy(m.nametags[i], tag, strnlen(tag, 8));
            }
        }
        m.ucf[i] = tgg_mod_loaded("tgg.ucf") != NULL;
        /* Added content: the replay keeps the numbers, the metadata says
         * what they are */
        if (pl->slot_type != Gm_PKind_NA) {
            const char* id = tgg_content_character_id(p[0]);
            if (id != NULL && strncmp(id, "melee/", 6) != 0) {
                snprintf(m.fighters[i], sizeof m.fighters[i], "%s", id);
            }
            const char* costume = tgg_content_costume(i);
            if (costume != NULL) {
                snprintf(m.costumes[i], sizeof m.costumes[i], "%s", costume);
            }
        }
    }
    /* An added stage, by the number the Game Info block holds */
    const char* stage = tgg_content_stage_id((m.info[0xE] << 8) | m.info[0xF]);
    if (stage != NULL && strncmp(stage, "melee/", 6) != 0) {
        snprintf(m.stage, sizeof m.stage, "%s", stage);
    }
    m.seed = *HSD_RandSeedPtr;
    m.frozen_stadium = tgg_mod_loaded("tgg.frozen-stadium") != NULL;
    m.major_scene = (uint8_t) gm_GetCurrentGameMode();
    m.minor_scene = 2; /* in game */
    m.language = (uint8_t) lbLang_GetSavedLanguage();
    if (busy) {
        queued = m;
        queued_pending = 1;
        return;
    }
    match = m;
    capture_start();
    pending = 1;
}
TGG_ON(match_setup, on_setup);

static void on_match_end(void)
{
    if (pending && !tgg_resim()) {
        match_left = 1;
    }
}
TGG_ON(match_end, on_match_end);

/* Makes `dir` and the folders above it */
static void make_dirs(const char* dir)
{
    char buf[sizeof path];
    snprintf(buf, sizeof buf, "%s", dir);
    for (char* p = buf + 1; *p != '\0'; p++) {
        if (*p != '/' && *p != '\\') {
            continue;
        }
        const char c = *p;
        *p = '\0';
#ifdef _WIN32
        _mkdir(buf);
#else
        mkdir(buf, 0777);
#endif
        *p = c;
    }
#ifdef _WIN32
    _mkdir(buf);
#else
    mkdir(buf, 0777);
#endif
}

static void open_file(void)
{
    char dir[sizeof path - 64];
    if (folder[0] != '\0') {
        snprintf(dir, sizeof dir, "%s", folder);
    } else {
        snprintf(dir, sizeof dir, "%s/replays", tgg_data_dir());
    }
    make_dirs(dir);
    const time_t now = time(NULL);
    struct tm local = *localtime(&now);
    struct tm utc = *gmtime(&now);
    strftime(start_at, sizeof start_at, "%Y-%m-%dT%H:%M:%SZ", &utc);
    char stamp[32];
    strftime(stamp, sizeof stamp, "%Y%m%dT%H%M%S", &local);
    snprintf(path, sizeof path, "%s/Game_%s.slp", dir, stamp);
    /* Two matches in one second: the second gets a number */
    struct stat sb;
    for (int n = 2; stat(path, &sb) == 0 && n < 100; n++) {
        snprintf(path, sizeof path, "%s/Game_%s_%d.slp", dir, stamp, n);
    }
    file = slp_create(path, &match);
    next = REC_FIRST_FRAME;
    if (file == NULL) {
        tgg_log("can't write %s: %s", path, strerror(errno));
        pending = 0;
        return;
    }
    tgg_log("recording %s", path);
}

/* Writes the frames that are final, in order */
static void write_final(void)
{
    const int ready = capture_ready(tgg_frame_final());
    for (; next < ready; next++) {
        const RecFrame* f = capture_frame(next);
        if (f == NULL) {
            tgg_log("frame %d was lost before it was written", next);
            continue;
        }
        slp_frame(file, f);
    }
}

static void emit_replay_saved(const char* file_path, int frames)
{
    char escaped[2048];
    size_t n = 0;
    for (const char* s = file_path; s != NULL && *s != '\0' && n + 8 < sizeof escaped; s++) {
        const unsigned char c = (unsigned char) *s;
        if (c == '"' || c == '\\') {
            escaped[n++] = '\\';
            escaped[n++] = (char) c;
        } else if (c < 0x20) {
            n += (size_t) snprintf(escaped + n, sizeof escaped - n, "\\u%04x", c);
        } else {
            escaped[n++] = (char) c;
        }
    }
    escaped[n] = '\0';
    char data[2200];
    int len = snprintf(data, sizeof data, "{\"path\":\"%s\",\"frames\":%d}", escaped, frames);
    if (len > 0 && (size_t) len < sizeof data) {
        tgg_companion_emit_event("replay_saved", data);
    }
}

static void finish(void)
{
    write_final();
    /* The end only follows the frames before it: when the game quits with
     * frames that aren't final yet, they and the end are left out */
    const RecEnd* end = next == capture_count() ? capture_end() : NULL;
    const int frames = next - REC_FIRST_FRAME;
    if (slp_finish(file, end, start_at) != 0) {
        tgg_log("couldn't finish %s", path);
    } else {
        tgg_log("saved %s, %d frames%s", path, frames,
                end != NULL && end->known ? "" : ", no end");
        emit_replay_saved(path, frames);
    }
    file = NULL;
    pending = 0;
    capture_stop();
}

static void pump(void)
{
    if (pending && file != NULL) {
        write_final();
        /* Done once the end is final: with rollback off it is as soon as it
         * happens, and with rollback on once the scene has left */
        if ((capture_end()->known && tgg_frame_final() == INT_MAX) || match_left ||
            !tgg_in_match())
        {
            finish();
        }
    }
    if (queued_pending && file == NULL) {
        /* The match set up while the last file was open, before its first
         * frame: every run of the match starts from here, as from its setup */
        queued_pending = 0;
        match = queued;
        match_left = 0;
        capture_start();
        pending = 1;
    }
    if (pending && file == NULL) {
        open_file();
    }
}

static void on_advance(tgg_advance* adv)
{
    (void) adv;
    pump();
}
TGG_ON(advance, on_advance);

static void on_quit(void)
{
    if (pending && file != NULL) {
        finish();
    }
}
TGG_ON(quit, on_quit);

/* A snapshot was loaded: the match jumped, so its replay ends here, with the
 * frames before the jump (it couldn't play back past it) */
static void on_state_loaded(void)
{
    if (!pending) {
        return;
    }
    tgg_log("a state was loaded, so this match's replay stops here");
    if (file != NULL) {
        finish();
    } else {
        pending = 0;
        capture_stop();
    }
}
TGG_ON(snapshot_loaded, on_state_loaded);
