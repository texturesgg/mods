/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Played with: after each finished session through textures.gg (a room or a
 * queue's match, with at least one match played to the end), one JSON line in
 * <data folder>/played-with.jsonl, for apps (the studio app shows the last
 * 20):
 *
 *     {"at":"2026-11-17T01:02:03Z","with":["saavy","bo"],"via":"room"}
 *
 * `with` is the other players' textures.gg names, however many there were;
 * `via` is "room", or the queue's name ("Competitive"). Names only, never
 * addresses. Past MAX_LINES lines the file is cut to its last KEEP_LINES.
 *
 * The names come from the games themselves: each game sends
 * "tgg-name <name>" (netplay_send) once its session starts. */
#include "dc.h"

#include "tgg/events.h"
#include "tgg/files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FILE_NAME "played-with.jsonl"
#define MAX_LINES 200
#define KEEP_LINES 100
#define NAME_PREFIX "tgg-name "

static struct {
    int live;      /* a session is on */
    int matches;   /* matches played to the end in it */
    int me;        /* this game's side */
    int players;   /* the most games it had */
    int sent_name; /* this game's name went out */
    char names[NETPLAY_MAX_PLAYERS][64]; /* each side's, as it sent it */
    char via[64];
    int through_meet; /* the session came through textures.gg */
} ps;

int played_got(const char* text, int from)
{
    const size_t n = strlen(NAME_PREFIX);
    if (strncmp(text, NAME_PREFIX, n) != 0) {
        return 0;
    }
    if (from >= 0 && from < NETPLAY_MAX_PLAYERS) {
        snprintf(ps.names[from], sizeof ps.names[0], "%s", text + n);
    }
    return 1;
}

/* `s` as a JSON string into `out` */
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

/* Cuts the file to its last KEEP_LINES once it has more than MAX_LINES */
static void trim(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return;
    }
    static char buf[MAX_LINES * 2 * 600];
    const size_t len = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[len] = '\0';
    int lines = 0;
    for (size_t i = 0; i < len; i++) {
        lines += buf[i] == '\n';
    }
    if (lines <= MAX_LINES && len < sizeof buf - 1) {
        return;
    }
    /* From the end back to the start of the last KEEP_LINES lines */
    size_t start = len;
    int seen = 0;
    while (start > 0) {
        if (buf[start - 1] == '\n' && start != len && ++seen == KEEP_LINES) {
            break;
        }
        start--;
    }
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE* out = fopen(tmp, "wb");
    if (out == NULL) {
        return;
    }
    const int ok = fwrite(buf + start, 1, len - start, out) == len - start;
    if (fclose(out) != 0 || !ok) {
        remove(tmp);
        return;
    }
    /* Windows' rename won't replace a file */
    remove(path);
    if (rename(tmp, path) != 0) {
        tgg_log("played with: can't replace %s", path);
    }
}

static void write_line(void)
{
    char with[NETPLAY_MAX_PLAYERS * 140] = "[";
    int n = 0;
    for (int side = 0; side < ps.players && side < NETPLAY_MAX_PLAYERS; side++) {
        const char* name = ps.names[side];
        if (side == ps.me || name[0] == '\0') {
            continue;
        }
        char quoted[140];
        json_string(quoted, sizeof quoted, name);
        snprintf(with + strlen(with), sizeof with - strlen(with), "%s%s", n > 0 ? "," : "", quoted);
        n++;
    }
    /* A game that never said its name: the meeting point's names for it */
    for (int i = 0; n == 0 && i < dc.meet.n_peers; i++) {
        char quoted[140];
        json_string(quoted, sizeof quoted, dc.meet.peers[i]);
        snprintf(with + strlen(with), sizeof with - strlen(with), "%s%s", i > 0 ? "," : "", quoted);
        if (i == dc.meet.n_peers - 1) {
            n = dc.meet.n_peers;
        }
    }
    snprintf(with + strlen(with), sizeof with - strlen(with), "]");
    if (n == 0) {
        tgg_log("played with: nobody's name is known, nothing written");
        return;
    }
    char at[32];
    const time_t now = time(NULL);
    const struct tm* t = gmtime(&now);
    if (t == NULL || strftime(at, sizeof at, "%Y-%m-%dT%H:%M:%SZ", t) == 0) {
        return;
    }
    char via[140];
    json_string(via, sizeof via, ps.via);
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", tgg_data_dir(), FILE_NAME);
    FILE* f = fopen(path, "ab");
    if (f == NULL) {
        tgg_log("played with: can't write %s", path);
        return;
    }
    fprintf(f, "{\"at\":\"%s\",\"with\":%s,\"via\":%s}\n", at, with, via);
    fclose(f);
    tgg_log("played with: %s via %s, %d match%s", with, ps.via, ps.matches,
            ps.matches == 1 ? "" : "es");
    trim(path);
}

/* The session ended: its line, when it played a match */
static void session_over(void)
{
    if (ps.live && ps.through_meet && ps.matches > 0) {
        write_line();
    }
    memset(&ps, 0, sizeof ps);
}

void played_frame(void)
{
    netplay_info info;
    netplay_status(&info);
    const int live = info.state == NETPLAY_CONNECTED || info.state == NETPLAY_STARTING ||
                     info.state == NETPLAY_PLAYING;
    if (!live) {
        if (ps.live && info.state == NETPLAY_FAILED && info.matches > ps.matches) {
            ps.matches = info.matches; /* it ended as a match did */
        }
        session_over();
        return;
    }
    if (!ps.live) {
        ps.live = 1;
    }
    /* Through textures.gg: a room, or a queue's match */
    if (dc.room || dc.meet.queue) {
        ps.through_meet = 1;
        if (dc.meet.queue) {
            snprintf(ps.via, sizeof ps.via, "%s",
                     dc.meet.label[0] != '\0' ? dc.meet.label : "Find a match");
        } else {
            snprintf(ps.via, sizeof ps.via, "room");
        }
    }
    ps.me = info.side;
    if (info.players > ps.players) {
        ps.players = info.players;
    }
    if (info.matches > ps.matches) {
        ps.matches = info.matches;
    }
    if (!ps.sent_name && info.locked && info.state == NETPLAY_CONNECTED &&
        dc.meet.name[0] != '\0')
    {
        char msg[96];
        snprintf(msg, sizeof msg, NAME_PREFIX "%s", dc.meet.name);
        if (netplay_send(msg, (int) strlen(msg)) == 0) {
            ps.sent_name = 1;
            snprintf(ps.names[info.side], sizeof ps.names[0], "%s", dc.meet.name);
        }
    }
}

/* The game is closing: a session it was in still counts */
static void on_quit(void)
{
    session_over();
}
TGG_ON(quit, on_quit);

void dc_netplay_close(void)
{
    played_frame(); /* the session as it was, matches and all */
    netplay_close();
    played_frame(); /* and its line */
}
