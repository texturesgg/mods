/* Skip results: after a VS match the game goes straight back to the
 * character select, without the results screen, as Slippi does.
 *
 * The results screen only shows the match: the records, KO counts, stats and
 * unlock checks are written when VS mode leaves the results state
 * (gmVsMelee_ExitResults, gm_801623A4 in gmvsmode.c), whatever the screen
 * did. So this keeps the state and leaves out the screen: the results
 * scene's setup builds nothing, and the scene ends on its first frame. VS
 * mode then goes on as after any results screen, to the character select,
 * or to a new challenger or a prize when one is due.
 *
 * Only plain VS mode skips it. Tournament mode and the special melees keep
 * their results screen.
 */
#include "tgg/events.h"

#include <melee/gm/forward.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmresult.h>
#include <melee/gm/gmscene.h>

/* The results scene started and builds nothing: end it on its next frame
 * (the scene loop clears an end asked for before it starts) */
static bool ending;

static void results_enter(void* data)
{
    if (gm_GetCurrentGameMode() != GM_VS) {
        TGG_ORIGINAL(gm_Scene_Results_OnEnter)(data);
        return;
    }
    ending = true;
}
TGG_REPLACE(gm_Scene_Results_OnEnter, results_enter);

static void on_tick(void)
{
    if (ending) {
        ending = false;
        gm_801A4B60(); /* end the scene */
    }
}
TGG_ON(tick, on_tick);
