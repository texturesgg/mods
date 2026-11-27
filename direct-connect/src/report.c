/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Companion online state reporting (tgg/internal/companion.h).
 * Publishes the current Find-a-match or room state to tgg-melee's host
 * each frame; the host deduplicates unchanged snapshots before pushing
 * them to tgg-studio. */
#include "dc.h"
#include "tgg/internal/companion.h"

#include <stdio.h>
#include <string.h>

void report_frame(void)
{
    tgg_companion_online out;
    memset(&out, 0, sizeof out);

    if (queue_companion_state(&out)) {
        tgg_companion_set_online(&out);
        return;
    }

    if (dc.view == VIEW_RESTARTING) {
        out.state = "getting-mods";
        out.room_host = dc.meet.n_members > 0 ? dc.meet.members[0] : "";
        tgg_companion_set_online(&out);
        return;
    }

    if (dc.room || dc.mode == MODE_ROOM || dc.mode == MODE_JOIN_ROOM) {
        char link[96] = "";
        if (dc.meet.code[0] != '\0') {
            snprintf(link, sizeof link, "textures-gg://join/%s", dc.meet.code);
        }
        out.room_code = dc.meet.code;
        out.room_link = link;
        out.room_host = dc.meet.n_members > 0 ? dc.meet.members[0] : "";
        out.room_n_players = dc.meet.n_members < TGG_COMPANION_MAX_PLAYERS
                                 ? dc.meet.n_members
                                 : TGG_COMPANION_MAX_PLAYERS;
        for (int i = 0; i < out.room_n_players; i++) {
            out.room_players[i] = dc.meet.members[i];
        }
        out.room_max = dc.meet.games;
        out.signin_url = dc.meet.signin_url;
        out.signin_code = dc.meet.signin_code;

        if (!dc.room || dc.meet.state == MEET_FAILED) {
            out.state = "ended";
            out.why = dc.meet.why[0] != '\0' ? dc.meet.why
                      : dc.why[0] != '\0'    ? dc.why
                                             : "You left the room.";
        } else if (dc.meet.state == MEET_SIGN_IN) {
            out.state = "signing-in";
        } else if (dc.meet.state == MEET_OFF || dc.meet.state == MEET_STARTING ||
                   dc.meet.code[0] == '\0')
        {
            out.state = "connecting";
        } else {
            out.state = dc.meet.locked ? "room-playing" : "room-waiting";
        }
        tgg_companion_set_online(&out);
        return;
    }

    out.state = "idle";
    tgg_companion_set_online(&out);
}
