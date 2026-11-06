/* Preloaded Stadium transformations: Pokémon Stadium picks its next
 * transformation, and loads its file, as soon as it's back to the neutral
 * stage, instead of when the neutral stage's timer runs out. When the timer
 * runs out the transformation starts that same frame, with no frame spent
 * waiting for the file. Matches Slippi's console code (slippi-ssbm-asm
 * Common/Preload Stadium Transformations), which runs when the console's
 * Slippi setup turns it on.
 *
 * grStadium_801D4548 runs the transformations, one step a frame:
 * - on a step-0 frame (between transformations, after the game's own check
 *   for a frozen stage), with nothing preloaded, it picks the kind the way the
 *   game does when the timer runs out, and loads its file;
 * - when the timer runs out on the neutral stage, the game's pick is that
 *   kind and draws nothing from the RNG; the game doesn't load the file again,
 *   and goes straight from step 1 to step 2 (the file is in) that frame;
 * - back on the neutral stage (step 6 of a return to it), the next one isn't
 *   picked yet.
 */
#include "tgg/mod.h"

#include <melee/gr/ground.h>
#include <melee/gr/grpstadium.h>
#include <melee/gr/stage.h>
#include <melee/gr/types.h>
#include <melee/lb/lbfile.h>
#include <sysdolphin/baselib/random.h>

static const int kinds[] = { 3, 4, 6, 9 };
static const char* const files[] = { "GrPs1.dat", "GrPs2.dat", "GrPs4.dat", "GrPs3.dat" };

static bool preloaded;
static int preloaded_kind;

/* While grStadium_801D4548 runs: its stage and its step when it started */
static Ground* deciding;
static int step_before;
static bool picked;

static void loaded(int req, uintptr_t args, void* buf, bool cancel)
{
    (void) req;
    (void) args;
    (void) buf;
    (void) cancel;
}

static void preload(Ground* gp)
{
    int r;
    do {
        r = HSD_Randi(4);
    } while (gp->u.stadium.xE2 == kinds[r]);
    preloaded_kind = kinds[r];
    lbFile_80016580(files[r], gp->u.stadium.xCC, &gp->u.stadium.xC8, loaded, 0);
    preloaded = true;
}

static void stage_init(Ground_GObj* gobj)
{
    (void) gobj;
    preloaded = false;
}
TGG_AFTER(grStadium_801D13E0, stage_init);

static void before_decide(Ground_GObj* gobj)
{
    deciding = gobj->user_data;
    step_before = deciding->u.stadium.xDC;
    picked = false;
}
TGG_BEFORE(grStadium_801D4548, before_decide);

/* grStadium_801D4548 calls this after the game's frozen-stage check, right
 * before it runs the step */
static StKind stage_kind(void)
{
    const StKind kind = TGG_ORIGINAL(Stage_80225194)();
    if (deciding != NULL && kind != 0xF0 && deciding->u.stadium.xDC == 0 && !preloaded) {
        preload(deciding);
    }
    return kind;
}
TGG_REPLACE(Stage_80225194, stage_kind);

/* The pick when the timer runs out: the preloaded kind */
static s32 pick_index(s32 max)
{
    if (deciding != NULL && preloaded && deciding->u.stadium.xDC == 0 &&
        deciding->u.stadium.xDE == 5 && max == 4)
    {
        for (int i = 0; i < 4; i++) {
            if (kinds[i] == preloaded_kind) {
                picked = true;
                return i;
            }
        }
    }
    return TGG_ORIGINAL(HSD_Randi)(max);
}
TGG_REPLACE(HSD_Randi, pick_index);

/* The game's own load of the picked file: already loaded */
static void load(const char* basename, void* dst, size_t* size, HSD_DevComCallback cb,
                 uintptr_t args)
{
    if (picked) {
        return;
    }
    TGG_ORIGINAL(lbFile_80016580)(basename, dst, size, cb, args);
}
TGG_REPLACE(lbFile_80016580, load);

static void after_decide(Ground_GObj* gobj)
{
    Ground* gp = gobj->user_data;
    if (picked && gp->u.stadium.xDC == 1) {
        /* The game marked the file as loading; it's in */
        gp->u.stadium.xC4_b1 = false;
        if (grStadium_801D42B8()) {
            gp->u.stadium.xDC = 2;
        }
    }
    if (step_before == 6 && gp->u.stadium.xDE == 5) {
        preloaded = false;
    }
    deciding = NULL;
    picked = false;
}
TGG_AFTER(grStadium_801D4548, after_decide);
