/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Fighters by CharacterKind and stages by StKind, as the game's menus name
 * them. */
#include "browser.h"

const char* fighter_name(int ckind)
{
    static const char* const names[] = {
        "Captain Falcon", "Donkey Kong", "Fox",     "Mr. Game & Watch", "Kirby",
        "Bowser",         "Link",        "Luigi",   "Mario",            "Marth",
        "Mewtwo",         "Ness",        "Peach",   "Pikachu",          "Ice Climbers",
        "Jigglypuff",     "Samus",       "Yoshi",   "Zelda",            "Sheik",
        "Falco",          "Young Link",  "Dr. Mario", "Roy",            "Pichu",
        "Ganondorf",
    };
    return ckind >= 0 && ckind < (int) (sizeof names / sizeof *names) ? names[ckind] : "?";
}

const char* stage_name(int stkind)
{
    static const struct {
        int kind;
        const char* name;
    } names[] = {
        { 2, "Fountain of Dreams" },  { 3, "Pokemon Stadium" },    { 4, "Princess Peach's Castle" },
        { 5, "Kongo Jungle" },        { 6, "Brinstar" },           { 7, "Corneria" },
        { 8, "Yoshi's Story" },       { 9, "Onett" },              { 10, "Mute City" },
        { 11, "Rainbow Cruise" },     { 12, "Jungle Japes" },      { 13, "Great Bay" },
        { 14, "Hyrule Temple" },      { 15, "Brinstar Depths" },   { 16, "Yoshi's Island" },
        { 17, "Green Greens" },       { 18, "Fourside" },          { 19, "Mushroom Kingdom" },
        { 20, "Mushroom Kingdom II" }, { 22, "Venom" },            { 23, "Poke Floats" },
        { 24, "Big Blue" },           { 25, "Icicle Mountain" },   { 27, "Flat Zone" },
        { 28, "Dream Land" },         { 29, "Yoshi's Island (N64)" }, { 30, "Kongo Jungle (N64)" },
        { 31, "Battlefield" },        { 32, "Final Destination" },
    };
    for (unsigned i = 0; i < sizeof names / sizeof *names; i++) {
        if (names[i].kind == stkind) {
            return names[i].name;
        }
    }
    return "Unknown stage";
}
