/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Skip results: after a VS match the game goes straight back to the
 * character select, without the results screen, as Slippi does.
 *
 * The results screen only shows the match: the records, KO counts, stats and
 * unlock checks are written when VS mode leaves the results state, whatever
 * the screen did. So skipping the screen keeps all of that. VS mode then goes
 * on as after any results screen, to the character select, or to a new
 * challenger or a prize when one is due.
 *
 * Only plain VS mode skips it. Tournament mode and the special melees keep
 * their results screen.
 */
#include "tgg/events.h"
#include "tgg/mode.h"

static void on_results_enter(tgg_results* results)
{
    if (tgg_mode() == TGG_MODE_VS) {
        results->skip = 1;
    }
}
TGG_ON(results_enter, on_results_enter);
