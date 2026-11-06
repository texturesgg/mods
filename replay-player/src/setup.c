/* The match a replay recorded, as the game sets one up (StartMeleeData), and
 * the Slippi rules its console ran. */
#include "player.h"

#include <melee/gm/gm_1601.h>
#include <melee/mn/types.h>

#include <stdio.h>
#include <string.h>

/* Bit k of a byte, most significant first, as the console lays out bitfields */
#define BIT(byte, k) (((byte) >> (7 - (k))) & 1)

static float be_float(const uint8_t* p)
{
    const uint32_t bits = slp_be32(p);
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

/* Slippi Online pauses on the client only (Online/Core/InitPause.asm): its
 * pauser check shows the pause screen to the local player and always returns
 * -1, so the match itself never pauses. */
static int online_pauser(void)
{
    return -1;
}

void player_match(const Slp* slp, StartMeleeData* start)
{
    const uint8_t* info = slp->info;
    StartMeleeRules* r = &start->rules;
    gm_SetupRulesDefaults(r);
    gm_SetupAllPlayerDefaults(start->players);

    /* The Game Info block is the console's StartMeleeRules, big-endian */
    r->match_kind = info[0] >> 5;
    r->x0_3 = (info[0] >> 2) & 7;
    r->timer_enabled = BIT(info[0], 6);
    r->timer_counts_up = BIT(info[0], 7);
    r->x1_0 = BIT(info[1], 0);
    r->x1_1 = BIT(info[1], 1);
    r->x1_2 = BIT(info[1], 2);
    r->x1_3 = BIT(info[1], 3);
    r->x1_4 = BIT(info[1], 4);
    r->x1_5 = BIT(info[1], 5);
    r->timer_shows_hours = BIT(info[1], 6);
    r->friendly_fire = BIT(info[1], 7);
    r->is_stock = BIT(info[2], 0);
    r->x2_1 = BIT(info[2], 1);
    r->x2_2 = BIT(info[2], 2);
    r->single_button = BIT(info[2], 3);
    /* An online game never pauses: unranked games record pausing as on, but
     * Slippi's online code replaces the game's pause with one only that
     * player's screen shows */
    r->disable_pausing = BIT(info[2], 4) || slp->major_scene == 8;
    r->x2_5 = BIT(info[2], 5);
    r->x2_6 = BIT(info[2], 6);
    r->x2_7 = BIT(info[2], 7);
    r->x3_0 = BIT(info[3], 0);
    r->x3_1 = BIT(info[3], 1);
    r->x3_2 = BIT(info[3], 2);
    r->x3_3 = BIT(info[3], 3);
    r->x3_4 = BIT(info[3], 4);
    r->x3_5 = BIT(info[3], 5);
    r->x3_6 = BIT(info[3], 6);
    r->x3_7 = BIT(info[3], 7);
    r->x4_0 = BIT(info[4], 0);
    r->is_vs = BIT(info[4], 1);
    r->x4_2 = BIT(info[4], 2);
    r->x4_3 = BIT(info[4], 3);
    r->x4_4 = BIT(info[4], 4);
    r->x4_5 = BIT(info[4], 5);
    r->x4_6 = BIT(info[4], 6);
    r->x4_7 = BIT(info[4], 7);
    r->x5_0 = BIT(info[5], 0);
    r->x5_1 = BIT(info[5], 1);
    r->x5_2 = BIT(info[5], 2);
    r->x5_3 = BIT(info[5], 3);
    r->x5_4 = BIT(info[5], 4);
    r->x5_5 = BIT(info[5], 5);
    r->x5_6 = BIT(info[5], 6);
    r->x5_7 = BIT(info[5], 7);
    r->x6 = info[0x6];
    r->x7 = info[0x7];
    r->is_teams = info[0x8];
    r->x9 = info[0x9];
    r->xA = info[0xA];
    r->item_freq = (int8_t) info[0xB];
    r->sd_penalty = (int8_t) info[0xC];
    r->xD = info[0xD];
    r->stkind = slp_be16(info + 0xE);
    r->time_limit = slp_be32(info + 0x10);
    r->x14 = info[0x14];
    r->x18 = slp_be32(info + 0x18);
    r->x20 = (uint64_t) slp_be32(info + 0x20) << 32 | slp_be32(info + 0x24);
    r->x28 = (int) slp_be32(info + 0x28);
    r->x2C = be_float(info + 0x2C);
    r->x30 = be_float(info + 0x30);
    r->game_speed = be_float(info + 0x34);
    if (slp->major_scene == 8) {
        r->check_for_pauser_override = online_pauser;
    }

    /* Then each port's PlayerInitData */
    for (int i = 0; i < 4; i++) {
        const uint8_t* p = info + 0x60 + 0x24 * i;
        PlayerInitData* pl = &start->players[i];
        pl->ckind = (int8_t) p[0x0];
        pl->slot_type = p[0x1];
        pl->stocks = (int8_t) p[0x2];
        pl->color = p[0x3];
        pl->slot = p[0x4];
        pl->spawn_pos = (int8_t) p[0x5];
        pl->spawn_dir = (int8_t) p[0x6];
        pl->sub_color = p[0x7];
        pl->handicap = (int8_t) p[0x8];
        pl->team = p[0x9];
        pl->nametag = p[0xA];
        pl->xB = p[0xB];
        pl->rumble_enabled = BIT(p[0xC], 0);
        pl->xC_b1 = BIT(p[0xC], 1);
        pl->vs_metal = BIT(p[0xC], 2);
        pl->xC_b3 = BIT(p[0xC], 3);
        pl->vs_invisible = BIT(p[0xC], 4);
        pl->xC_b5 = BIT(p[0xC], 5);
        pl->xC_b6 = BIT(p[0xC], 6);
        pl->xC_b7 = BIT(p[0xC], 7);
        pl->xD_b0 = BIT(p[0xD], 0);
        pl->xD_b1 = BIT(p[0xD], 1);
        pl->xD_b2 = BIT(p[0xD], 2);
        pl->xD_b3 = BIT(p[0xD], 3);
        pl->xD_b4 = BIT(p[0xD], 4);
        pl->xD_b5 = BIT(p[0xD], 5);
        pl->xD_b6 = BIT(p[0xD], 6);
        pl->xD_b7 = BIT(p[0xD], 7);
        pl->cpu_kind = p[0xE];
        pl->cpu_level = p[0xF];
        pl->damage = slp_be16(p + 0x10);
        pl->damage1 = slp_be16(p + 0x12);
        pl->hp = slp_be16(p + 0x14);
        pl->attack_ratio = be_float(p + 0x18);
        pl->defense_ratio = be_float(p + 0x1C);
        pl->model_scale = be_float(p + 0x20);
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
