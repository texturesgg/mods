/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The playback controls, from the viewer's keyboard and controllers
 * (tgg/input.h): what the person watching presses, whatever the replay
 * plays on the game's ports. */
#include "player.h"

#include "tgg/input.h"

Keys keys_pressed(void)
{
    Keys k = { 0 };
    uint32_t pad = 0;
    for (int port = 0; port < 4; port++) {
        tgg_viewer_pad p;
        if (tgg_viewer_pad_read(port, &p)) {
            pad |= p.pressed;
        }
    }
    k.pause = tgg_key_pressed(TGG_KEY_SPACE) || (pad & TGG_BUTTON_START);
    k.step = tgg_key_pressed(TGG_KEY_PERIOD) || tgg_key_pressed(TGG_KEY_RIGHT) ||
             (pad & TGG_BUTTON_RIGHT);
    k.faster = tgg_key_pressed(TGG_KEY_EQUALS) || tgg_key_pressed(TGG_KEY_UP) || (pad & TGG_BUTTON_UP);
    k.slower = tgg_key_pressed(TGG_KEY_MINUS) || tgg_key_pressed(TGG_KEY_DOWN) ||
               (pad & TGG_BUTTON_DOWN);
    k.normal = tgg_key_pressed(TGG_KEY_0) || (pad & TGG_BUTTON_Z);
    k.leave = tgg_key_pressed(TGG_KEY_ESCAPE) || (pad & TGG_BUTTON_B);
    return k;
}
