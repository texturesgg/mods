/* SPDX-License-Identifier: GPL-3.0-or-later */
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

/* Slippi's gameplay codes, by the address each injects at, and the mod that
 * reproduces it */
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
static int injects(const uint8_t* gecko, size_t len, uint32_t address)
{
    for (size_t i = 0; i + 4 <= len; i += 4) {
        const uint32_t w = slp_be32(gecko + i);
        const uint32_t at = 0x80000000u | (w & 0x01FFFFFF);
        if (((w >> 24) == 0xC2 || (w >> 24) == 0x04) && at == address) {
            return 1;
        }
    }
    return 0;
}

/* Slippi 3.17 and later run UCF 0.84 (its pad buffer at 0x8006B460), which is
 * tgg.ucf 0.84. Slippi 3.9 to 3.16 ran UCF 0.8 (its tumble code at
 * 0x800908F4), which is tgg.ucf 0.8 */
static int modern_ucf(const uint8_t* gecko, size_t len)
{
    return injects(gecko, len, 0x8006B460);
}

static int legacy_ucf(const uint8_t* gecko, size_t len)
{
    return injects(gecko, len, 0x800908F4) && !injects(gecko, len, 0x8006B460);
}

int player_rule_mods(const uint8_t* gecko, size_t len, int stage, int transformations,
                     int missing_only, char* out, size_t cap)
{
    const char* mods[8];
    int n = 0;
    for (size_t i = 0; i < sizeof rule_codes / sizeof *rule_codes; i++) {
        if (injects(gecko, len, rule_codes[i].address)) {
            mods[n++] = rule_codes[i].mod;
        }
    }
    /* Slippi toggles frozen Stadium at run time rather than by a code: a
     * Stadium match that never transformed was frozen */
    if (stage == 3 && transformations == 0) {
        mods[n++] = "tgg.frozen-stadium";
    }
    if (modern_ucf(gecko, len) || legacy_ucf(gecko, len)) {
        mods[n++] = "tgg.ucf";
    }
    size_t used = 0;
    int listed = 0;
    if (cap > 0) {
        out[0] = '\0';
    }
    for (int i = 0; i < n; i++) {
        if (missing_only && tgg_mod_loaded(mods[i]) != NULL) {
            continue;
        }
        const int w = snprintf(out + used, cap - used, "%s%s", listed ? ", " : "", mods[i]);
        used = w > 0 && used + (size_t) w < cap ? used + (size_t) w : cap - 1;
        listed++;
    }
    return listed;
}

void player_log_rules(const Slp* slp)
{
    char mods[256];
    const int n = player_rule_mods(slp->gecko, slp->gecko_len, slp_be16(slp->info + 0xE),
                                   slp->transformations, 0, mods, sizeof mods);
    tgg_log("Slippi %u.%u.%u, stage %u, %d frames%s", slp->version[0], slp->version[1],
            slp->version[2], slp_be16(slp->info + 0xE), slp->frame_count,
            slp->major_scene == 8 ? ", played online" : "");
    if (n == 0) {
        tgg_log("its console ran no Slippi rule the port needs a mod for");
        return;
    }
    tgg_log("its console ran Slippi rules these mods reproduce: %s", mods);
    char missing[256];
    if (player_rule_mods(slp->gecko, slp->gecko_len, slp_be16(slp->info + 0xE),
                         slp->transformations, 1, missing, sizeof missing) > 0)
    {
        tgg_log("not loaded: %s; install and turn them on, or the match plays out differently",
                missing);
    }
    const char* ucf = tgg_mod_loaded("tgg.ucf");
    const int legacy = legacy_ucf(slp->gecko, slp->gecko_len);
    if (ucf != NULL && (legacy || modern_ucf(slp->gecko, slp->gecko_len)) &&
        (strncmp(ucf, "0.8.", 4) == 0) != legacy)
    {
        tgg_log("it ran UCF %s, but tgg.ucf %s is loaded: the match may play out differently",
                legacy ? "0.8" : "0.84", ucf);
    }
}
