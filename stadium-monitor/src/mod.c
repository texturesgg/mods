/* Camera-free Stadium monitor: Pokémon Stadium's big screen leaves its close-up
 * of a fighter when the fighter's position leaves fixed bounds (x -120 to 120,
 * y -20 to 80), instead of when the close-up would leave the camera's view.
 * Leaving the close-up draws from the RNG, so with the game's own rule the
 * camera, which each player's game draws differently, can change the
 * simulation. Matches Slippi's code (slippi-ssbm-asm
 * Common/PSCameraIndependentMonitor), which runs in every Slippi game.
 *
 * grStadium_801D32D0 places the close-up and answers whether it fits on
 * screen; the close-up still goes where the game puts it, and the answer comes
 * from the bounds.
 */
#include "tgg/mod.h"

#include <melee/ft/types.h>
#include <melee/gr/grpstadium.h>
#include <melee/gr/types.h>
#include <melee/pl/player.h>

static bool close_up_fits(Ground_GObj* gobj)
{
    TGG_ORIGINAL(grStadium_801D32D0)(gobj);
    const Ground* gp = gobj->user_data;
    const Fighter* fp = Player_GetEntity(gp->u.display.xEE)->user_data;
    const float x = fp->cur_pos.x;
    const float y = fp->cur_pos.y;
    return !(x < -120.0f || x > 120.0f || y > 80.0f || y < -20.0f);
}
TGG_REPLACE(grStadium_801D32D0, close_up_fits);
