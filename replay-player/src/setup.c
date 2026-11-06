/* The match a replay recorded, as the game sets one up (StartMeleeData), and
 * the Slippi rules its console ran. */
#include "player.h"

#include "tgg/control.h"

#include <melee/mn/types.h>

#include <stdio.h>
#include <string.h>

/* Slippi Online pauses on the client only (Online/Core/InitPause.asm): its
 * pauser check shows the pause screen to the local player and always returns
 * -1, so the match itself never pauses. */
static int online_pauser(void)
{
    return -1;
}

void player_match(const Slp* slp, StartMeleeData* start)
{
    /* The Game Info block is the console's StartMeleeData */
    tgg_match_from_console(start, slp->info);
    /* An online game never pauses: unranked games record pausing as on, but
     * Slippi's online code replaces the game's pause with one only that
     * player's screen shows */
    if (slp->major_scene == 8) {
        start->rules.disable_pausing = 1;
        start->rules.check_for_pauser_override = online_pauser;
    }
}

/* Slippi's gameplay codes, by the address each injects at, and the mod in
 * ~/dev/texturesgg/mods that reproduces it */
static const struct {
    uint32_t address;
    const char* mod;
    const char* code;
} rule_codes[] = {
    { 0x8016E510, "tgg.neutral-spawns", "External/NeutralSpawn" },
    { 0x8006A880, "tgg.offscreen-damage", "Online/Core/BrawlOffscreenDamage" },
    { 0x800D4C1C, "tgg.freeze-dead-up-fall", "Online/Core/FreezeDeadUpFallPhysics" },
    { 0x801D24FC, "tgg.stadium-monitor", "Common/PSCameraIndependentMonitor" },
    { 0x801D45EC, "tgg.stadium-preload", "Common/Preload Stadium Transformations" },
};

/* Whether the console's gecko list writes or injects at `address` (a C2
 * insert or an 04 write, as Slippi's codes are) */
static int injects(const Slp* slp, uint32_t address)
{
    for (size_t i = 0; i + 4 <= slp->gecko_len; i += 4) {
        const uint32_t w = slp_be32(slp->gecko + i);
        const uint32_t at = 0x80000000u | (w & 0x01FFFFFF);
        if (((w >> 24) == 0xC2 || (w >> 24) == 0x04) && at == address) {
            return 1;
        }
    }
    return 0;
}

void player_log_rules(const Slp* slp)
{
    char mods[256] = "";
    size_t n = 0;
    for (size_t i = 0; i < sizeof rule_codes / sizeof *rule_codes; i++) {
        if (injects(slp, rule_codes[i].address)) {
            n += (size_t) snprintf(mods + n, sizeof mods - n, "%s%s", n ? ", " : "",
                                   rule_codes[i].mod);
        }
    }
    /* Slippi toggles frozen Stadium at run time rather than by a code: a
     * Stadium match that never transformed was frozen */
    if (slp_be16(slp->info + 0xE) == 3 && slp->transformations == 0) {
        n += (size_t) snprintf(mods + n, sizeof mods - n, "%stgg.frozen-stadium", n ? ", " : "");
    }
    /* The port's built-in UCF is 0.84 (its pad buffer at 0x8006B460). Slippi
     * 3.9 to 3.16 ran UCF 0.8, with its tumble code at 0x800908F4 */
    const int legacy_ucf = injects(slp, 0x800908F4) && !injects(slp, 0x8006B460);
    if (legacy_ucf) {
        n += (size_t) snprintf(mods + n, sizeof mods - n, "%stgg.ucf-legacy", n ? ", " : "");
    }

    tgg_log("Slippi %u.%u.%u, stage %u, %d frames%s", slp->version[0], slp->version[1],
            slp->version[2], slp_be16(slp->info + 0xE), slp->frame_count,
            slp->major_scene == 8 ? ", played online" : "");
    if (n == 0) {
        tgg_log("its console ran no Slippi rule the port needs a mod for");
    } else {
        tgg_log("its console ran Slippi rules these mods reproduce: %s", mods);
        tgg_log("install and turn them on, or the match plays out differently");
    }
    if (legacy_ucf) {
        tgg_log("it ran UCF 0.8, not the built-in 0.84: start the game with TGG_UCF=0");
    }
}
