/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The room report, for an app that started the game into a room
 * (TGG_DIRECT_CONNECT=room or room:<code>): with TGG_DIRECT_CONNECT_REPORT
 * naming a file, the game appends one JSON line to it each time the room
 * changes, and the last line is where things are:
 *
 *     {"state":"waiting","code":"ABCD","link":"textures-gg://join/ABCD",
 *      "host":"saavy","players":["saavy"],"max":4}
 *
 * The states, in the order they can come:
 *   starting      signing in to textures.gg and reaching the meeting point
 *   signing-in    nobody's signed in: "signin" has the url and the code to
 *                 confirm there
 *   waiting       in the room, before the host starts: code, link, host,
 *                 players (everyone in the room, the host first) and max (the
 *                 most games the room takes)
 *   getting-mods  this game is getting the host's mods; it restarts with them
 *                 and joins the room again by itself (the next game appends
 *                 to the same file)
 *   playing       the host started: the same fields as waiting
 *   ended         left the room or it closed: "why" says why, for the player
 *
 * Every line has every field ("" or [] when it doesn't apply), so a reader
 * needs one shape. Lines are whole; a reader skips one it can't parse. */
#include "dc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char last[2048];
static int ended;

static void json_string(char* out, size_t cap, const char* s)
{
    size_t n = 0;
    out[n++] = '"';
    for (; *s != '\0' && n + 8 < cap; s++) {
        const unsigned char c = (unsigned char) *s;
        if (c == '"' || c == '\\') {
            out[n++] = '\\';
            out[n++] = (char) c;
        } else if (c < 0x20) {
            n += (size_t) snprintf(out + n, cap - n, "\\u%04x", c);
        } else {
            out[n++] = (char) c;
        }
    }
    out[n++] = '"';
    out[n] = '\0';
}

static const char* state_of(const char** why)
{
    *why = "";
    if (dc.view == VIEW_RESTARTING) {
        return "getting-mods";
    }
    if (!dc.room || dc.meet.state == MEET_FAILED) {
        *why = dc.meet.why[0] != '\0' ? dc.meet.why : dc.why[0] != '\0' ? dc.why : "You left the room.";
        return "ended";
    }
    switch (dc.meet.state) {
    case MEET_SIGN_IN:
        return "signing-in";
    case MEET_OFF:
    case MEET_STARTING:
        return "starting";
    default:
        break;
    }
    if (dc.meet.code[0] == '\0') {
        return "starting";
    }
    return dc.meet.locked ? "playing" : "waiting";
}

void report_frame(void)
{
    const char* path = getenv("TGG_DIRECT_CONNECT_REPORT");
    if (path == NULL || path[0] == '\0') {
        return;
    }
    const char* why = "";
    const char* state = state_of(&why);
    if (strcmp(state, "ended") == 0 && (last[0] == '\0' || ended)) {
        return; /* nothing started, or its end is written */
    }
    char code[32], host[140], players[NETPLAY_MAX_PLAYERS * 140 + 8] = "[", url[300], scode[80],
        quoted_why[700];
    json_string(code, sizeof code, dc.meet.code);
    json_string(host, sizeof host, dc.meet.n_members > 0 ? dc.meet.members[0] : "");
    for (int i = 0; i < dc.meet.n_members && i < MEET_MEMBERS; i++) {
        char name[140];
        json_string(name, sizeof name, dc.meet.members[i]);
        snprintf(players + strlen(players), sizeof players - strlen(players), "%s%s",
                 i > 0 ? "," : "", name);
    }
    snprintf(players + strlen(players), sizeof players - strlen(players), "]");
    json_string(url, sizeof url, dc.meet.signin_url);
    json_string(scode, sizeof scode, dc.meet.signin_code);
    json_string(quoted_why, sizeof quoted_why, why);
    char line[sizeof last];
    snprintf(line, sizeof line,
             "{\"state\":\"%s\",\"code\":%s,\"link\":\"%s%s\",\"host\":%s,\"players\":%s,"
             "\"max\":%d,\"signin\":{\"url\":%s,\"code\":%s},\"why\":%s}",
             state, code, dc.meet.code[0] != '\0' ? "textures-gg://join/" : "", dc.meet.code, host,
             players, dc.meet.games, url, scode, quoted_why);
    if (strcmp(line, last) == 0) {
        return;
    }
    FILE* f = fopen(path, "ab");
    if (f == NULL) {
        tgg_log("can't write the room report to %s", path);
        return;
    }
    fprintf(f, "%s\n", line);
    fclose(f);
    snprintf(last, sizeof last, "%s", line);
    ended = strcmp(state, "ended") == 0;
}
