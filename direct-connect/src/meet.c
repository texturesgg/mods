/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The meeting point on textures.gg (meet.h): a ticket from the tgg beside the
 * game (tgg/account.h), a WebSocket to the meeting point (tgg.http), this
 * game's addresses from netplay (netplay_prepare, netplay_candidates), a room,
 * and once the meeting point says go, a netplay session on the other game's
 * addresses (netplay_open_meet). The messages are in the textures.gg site's
 * packages/meet/src/protocol.ts.
 *
 * TGG_MEET_URL points it at another meeting point (a preview, or a local one
 * with ws://), and TGG_MEET_TICKET gives a ticket instead of asking tgg (a
 * local one takes "dev:<name>"). */
#include "meet.h"

#include "netplay.h"
#include "http.h"

#include "tgg/account.h"
#include "tgg/events.h"
#include "tgg/mod.h"
#include "tgg/net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEET_URL "wss://meet.textures.gg/v1"
#define UDP_PORT 51000
#define CANDIDATES 8

enum { STEP_TICKET, STEP_SOCKET, STEP_ROOM, STEP_PUNCH, STEP_DONE };

static struct {
    meet_status st;
    int step;
    int joining;
    char want_code[16];
    http_ws* ws;
    int sent_hello, sent_room, sent_candidates, reported;
    double met_ms;        /* when the meeting point said go */
    int relay_asked;      /* the host asked for a relay */
    int relay_told;       /* and told the others its address */
    int relay_tried;      /* a relay was up: the host got one, or a joiner heard of the host's */
    int relay_missing;    /* the meeting point had none to give */
    double bytes_told, bytes_ms; /* relay bytes reported so far, and when */
    char peer_name[64];
} m;

static void fail(const char* why)
{
    m.st.state = MEET_FAILED;
    snprintf(m.st.why, sizeof m.st.why, "%s", why);
    tgg_log("meet: %s", why);
    tgg_account_cancel();
    http_ws_close(m.ws);
    m.ws = NULL;
}

/* ---- A little JSON, for the meeting point's messages -------------------------- */

static const char* ws_skip(const char* p)
{
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
        p++;
    }
    return p;
}

/* Past one value, or NULL when it's broken */
static const char* skip_value(const char* p, int depth)
{
    p = ws_skip(p);
    if (depth > 16) {
        return NULL;
    }
    if (*p == '"') {
        for (p++; *p != '"'; p++) {
            if (*p == '\0') {
                return NULL;
            }
            if (*p == '\\' && p[1] != '\0') {
                p++;
            }
        }
        return p + 1;
    }
    if (*p == '{' || *p == '[') {
        const char close = *p == '{' ? '}' : ']';
        p = ws_skip(p + 1);
        if (*p == close) {
            return p + 1;
        }
        for (;;) {
            if (close == '}') {
                p = skip_value(p, depth + 1); /* the key */
                if (p == NULL || *(p = ws_skip(p)) != ':') {
                    return NULL;
                }
                p++;
            }
            p = skip_value(p, depth + 1);
            if (p == NULL) {
                return NULL;
            }
            p = ws_skip(p);
            if (*p == ',') {
                p++;
                continue;
            }
            return *p == close ? p + 1 : NULL;
        }
    }
    while (*p != '\0' && *p != ',' && *p != '}' && *p != ']' && *p != ' ') {
        p++;
    }
    return p;
}

/* An object's member `key`: where its value starts, or NULL */
static const char* member(const char* obj, const char* key)
{
    const char* p = ws_skip(obj);
    if (*p != '{') {
        return NULL;
    }
    p = ws_skip(p + 1);
    const size_t n = strlen(key);
    while (*p == '"') {
        const int match = strncmp(p + 1, key, n) == 0 && p[1 + n] == '"';
        p = skip_value(p, 1);
        if (p == NULL || *(p = ws_skip(p)) != ':') {
            return NULL;
        }
        p = ws_skip(p + 1);
        if (match) {
            return p;
        }
        p = skip_value(p, 1);
        if (p == NULL) {
            return NULL;
        }
        p = ws_skip(p);
        if (*p != ',') {
            return NULL;
        }
        p = ws_skip(p + 1);
    }
    return NULL;
}

/* A member's string into `out` (escapes kept simple); 0 when it isn't one */
static int text(const char* obj, const char* key, char* out, size_t cap)
{
    const char* p = member(obj, key);
    if (p == NULL || *p != '"' || cap == 0) {
        return 0;
    }
    size_t n = 0;
    for (p++; *p != '"' && *p != '\0'; p++) {
        char c = *p;
        if (c == '\\' && p[1] != '\0') {
            c = *++p;
            c = c == 'n' ? '\n' : c == 't' ? '\t' : c;
        }
        if (n + 1 < cap) {
            out[n++] = c;
        }
    }
    out[n] = '\0';
    return 1;
}

static long number(const char* obj, const char* key, long fallback)
{
    const char* p = member(obj, key);
    return p != NULL && (*p == '-' || (*p >= '0' && *p <= '9')) ? strtol(p, NULL, 10) : fallback;
}

/* The elements of an array: calls `each` on each one's start */
static void elements(const char* array, void (*each)(const char* value, void* ctx), void* ctx)
{
    const char* p = array != NULL ? ws_skip(array) : NULL;
    if (p == NULL || *p != '[') {
        return;
    }
    p = ws_skip(p + 1);
    while (*p != ']' && *p != '\0') {
        each(p, ctx);
        p = skip_value(p, 1);
        if (p == NULL) {
            return;
        }
        p = ws_skip(p);
        if (*p == ',') {
            p = ws_skip(p + 1);
        }
    }
}

/* Writes `s` into a JSON string, escaped */
static void quote(char* out, size_t cap, const char* s)
{
    size_t n = 0;
    if (cap < 3) {
        return;
    }
    out[n++] = '"';
    for (; *s != '\0' && n + 3 < cap; s++) {
        if (*s == '"' || *s == '\\') {
            out[n++] = '\\';
        }
        out[n++] = (unsigned char) *s < 0x20 ? ' ' : *s;
    }
    out[n++] = '"';
    out[n] = '\0';
}

/* ---- Talking to the meeting point ---------------------------------------------- */

static void send_json(const char* json)
{
    if (http_ws_send(m.ws, json, strlen(json)) != 0) {
        tgg_log("meet: couldn't send %s", json);
    }
}

static void send_hello(void)
{
    char game[200], target[80], versions[80], msg[512];
    quote(game, sizeof game, tgg_game_build());
    quote(target, sizeof target, TGG_TARGET);
    quote(versions, sizeof versions, tgg_mod_set_versions());
    snprintf(msg, sizeof msg, "{\"type\":\"hello\",\"game\":%s,\"target\":%s,\"versions\":%s}",
             game, target, versions);
    send_json(msg);
}

/* 1 once this game's candidates went out, 0 while netplay still gathers them */
static int send_candidates(void)
{
    netplay_candidate list[CANDIDATES];
    const int n = netplay_candidates(list, CANDIDATES);
    if (n < 0) {
        return 0;
    }
    char msg[2048];
    size_t used = (size_t) snprintf(msg, sizeof msg, "{\"type\":\"candidates\",\"list\":[");
    for (int i = 0; i < n && used < sizeof msg; i++) {
        char addr[96], kind[24];
        quote(addr, sizeof addr, list[i].addr);
        quote(kind, sizeof kind, list[i].kind);
        used += (size_t) snprintf(msg + used, sizeof msg - used, "%s{\"addr\":%s,\"kind\":%s}",
                                  i > 0 ? "," : "", addr, kind);
    }
    if (used < sizeof msg) {
        snprintf(msg + used, sizeof msg - used, "],\"nat\":\"unknown\"}");
    }
    send_json(msg);
    tgg_log("meet: %d addresses for the other game", n);
    for (int i = 0; i < n; i++) {
        tgg_log("meet:   %s (%s)", list[i].addr, list[i].kind);
    }
    return 1;
}

typedef struct Peers {
    char addrs[CANDIDATES * 3][64];
    char kinds[CANDIDATES * 3][12];
    int n;
    char name[64];
} Peers;

static void each_candidate(const char* value, void* ctx)
{
    Peers* p = ctx;
    if (p->n < (int) (sizeof p->addrs / sizeof p->addrs[0]) &&
        text(value, "addr", p->addrs[p->n], sizeof p->addrs[0]))
    {
        if (!text(value, "kind", p->kinds[p->n], sizeof p->kinds[0])) {
            p->kinds[p->n][0] = '\0';
        }
        p->n++;
    }
}

static void each_peer(const char* value, void* ctx)
{
    Peers* p = ctx;
    if (p->name[0] == '\0') {
        text(value, "name", p->name, sizeof p->name);
    }
    elements(member(value, "candidates"), each_candidate, p);
}

static void got_meet(const char* msg)
{
    static Peers peers;
    char token[32];
    memset(&peers, 0, sizeof peers);
    if (!text(msg, "token", token, sizeof token)) {
        return;
    }
    m.st.side = (int) number(msg, "side", -1);
    elements(member(msg, "peers"), each_peer, &peers);
    snprintf(m.peer_name, sizeof m.peer_name, "%s", peers.name);
    const char* addrs[CANDIDATES * 3];
    const char* kinds[CANDIDATES * 3];
    for (int i = 0; i < peers.n; i++) {
        addrs[i] = peers.addrs[i];
        kinds[i] = peers.kinds[i];
    }
    const char* why = NULL;
    if (m.st.side < 0 || peers.n == 0) {
        char text_why[200];
        snprintf(text_why, sizeof text_why, "%s's game sent no address to reach it at.",
                 peers.name[0] != '\0' ? peers.name : "The other player");
        fail(text_why);
        return;
    }
    tgg_log("meet: meeting %s as side %d, %d of their addresses", peers.name, m.st.side, peers.n);
    if (netplay_open_meet(m.st.side, addrs, kinds, peers.n, token, &why) != 0) {
        fail(why != NULL ? why : "Netplay couldn't start.");
        return;
    }
    m.met_ms = tgg_net_time_ms();
    m.st.state = MEET_PUNCHING;
    m.step = STEP_PUNCH;
}

static void got_room(const char* msg)
{
    char was[sizeof m.st.code];
    snprintf(was, sizeof was, "%s", m.st.code);
    text(msg, "code", m.st.code, sizeof m.st.code);
    if (strcmp(was, m.st.code) != 0) {
        tgg_log("meet: in room %s", m.st.code);
    }
    m.st.n_members = 0;
    const char* list = member(msg, "members");
    const char* p = list != NULL ? ws_skip(list) : NULL;
    if (p != NULL && *p == '[') {
        p = ws_skip(p + 1);
        while (*p == '"' && m.st.n_members < MEET_MEMBERS) {
            char wrapped[96];
            const char* end = skip_value(p, 1);
            if (end == NULL || (size_t) (end - p) + 8 >= sizeof wrapped) {
                break;
            }
            snprintf(wrapped, sizeof wrapped, "{\"v\":%.*s}", (int) (end - p), p);
            text(wrapped, "v", m.st.members[m.st.n_members], sizeof m.st.members[0]);
            m.st.n_members++;
            p = ws_skip(end);
            if (*p == ',') {
                p = ws_skip(p + 1);
            }
        }
    }
    if (m.step == STEP_ROOM && m.st.state != MEET_PUNCHING) {
        m.st.state = MEET_IN_ROOM;
    }
}

/* The host: bytes through the relay since the last report, when there are some and
 * `due` */
static void report_relay_bytes(int due)
{
    const double bytes = netplay_relay_bytes();
    if (!due || bytes <= m.bytes_told || m.ws == NULL) {
        return;
    }
    char msg[96];
    snprintf(msg, sizeof msg, "{\"type\":\"relay.bytes\",\"bytes\":%.0f}", bytes - m.bytes_told);
    send_json(msg);
    m.bytes_told = bytes;
    m.bytes_ms = tgg_net_time_ms();
}

/* The host: relay credentials from the meeting point */
static void got_relay(const char* msg)
{
    char server[128], user[256], pass[256];
    const char* why = NULL;
    if (!text(msg, "server", server, sizeof server) || !text(msg, "username", user, sizeof user) ||
        !text(msg, "credential", pass, sizeof pass))
    {
        return;
    }
    m.relay_tried = 1;
    if (netplay_relay(server, user, pass, &why) != 0) {
        tgg_log("meet: no relay: %s", why != NULL ? why : "?");
    }
}

static void got(const char* msg)
{
    char type[24];
    if (!text(msg, "type", type, sizeof type)) {
        return;
    }
    if (strcmp(type, "welcome") == 0) {
        text(msg, "name", m.st.name, sizeof m.st.name);
        tgg_log("meet: signed in as %s", m.st.name);
    } else if (strcmp(type, "room") == 0) {
        got_room(msg);
    } else if (strcmp(type, "meet") == 0) {
        got_meet(msg);
    } else if (strcmp(type, "relay") == 0) {
        got_relay(msg);
    } else if (strcmp(type, "relay.peer") == 0) {
        char addr[64];
        if (text(msg, "addr", addr, sizeof addr) && m.st.side > 0) {
            const char* addrs[1] = {addr};
            const char* kinds[1] = {"relay"};
            tgg_log("meet: the host is also at %s, through a relay", addr);
            m.relay_tried = 1;
            netplay_add_candidates(addrs, kinds, 1);
        }
    } else if (strcmp(type, "closed") == 0) {
        char why[200];
        if (m.step != STEP_DONE) {
            fail(text(msg, "why", why, sizeof why) ? why : "The room closed.");
        }
    } else if (strcmp(type, "error") == 0) {
        char why[200] = "";
        if (text(msg, "message", why, sizeof why) && m.step < STEP_PUNCH) {
            fail(why);
        } else if (m.relay_asked && !m.relay_tried) {
            tgg_log("meet: no relay: %s", why);
            m.relay_missing = 1;
        }
    }
}

/* The host: no direct way through after 3 seconds, so ask for a relay and tell the
 * others its address; once relayed, report its bytes each minute */
static void relay_step(void)
{
    const double now = tgg_net_time_ms();
    double ms = 0;
    if (m.st.side != 0) {
        return;
    }
    if (m.step == STEP_PUNCH && !m.relay_asked && netplay_route(&ms)[0] == '\0' &&
        now - m.met_ms > 3000)
    {
        m.relay_asked = 1;
        tgg_log("meet: no direct way through yet; asking for a relay");
        send_json("{\"type\":\"relay.request\"}");
    }
    char addr[64];
    if (m.relay_asked && !m.relay_told && netplay_relay_address(addr, sizeof addr) == 1) {
        char msg[128];
        m.relay_told = 1;
        snprintf(msg, sizeof msg, "{\"type\":\"relay.ready\",\"addr\":\"%s\"}", addr);
        send_json(msg);
    }
    report_relay_bytes(now - m.bytes_ms > 60000);
}

/* What to tell the player when the games couldn't reach each other */
static void could_not_connect(const char* netplay_why)
{
    char why[320];
    const char* who = m.peer_name[0] != '\0' ? m.peer_name : "the other player";
    if (m.relay_tried) {
        snprintf(why, sizeof why, "Couldn't reach %s, even through the relay.", who);
    } else if (m.relay_missing) {
        snprintf(why, sizeof why,
                 "Couldn't reach %s, and there's no relay. Forwarding UDP port %d to the host "
                 "fixes it.",
                 who, UDP_PORT);
    } else {
        snprintf(why, sizeof why, "Couldn't reach %s.", who);
    }
    tgg_log("meet: %s", netplay_why);
    char report[300], quoted[260];
    quote(quoted, sizeof quoted, netplay_why);
    snprintf(report, sizeof report, "{\"type\":\"failed\",\"why\":%s}", quoted);
    if (m.ws != NULL && !m.reported) {
        send_json(report);
        m.reported = 1;
    }
    fail(why);
}

static void step(void)
{
    if (m.st.state == MEET_OFF || m.st.state == MEET_FAILED) {
        return;
    }
    if (m.step == STEP_TICKET) {
        const char* given = getenv("TGG_MEET_TICKET");
        char ticket[600] = "";
        if (given != NULL && given[0] != '\0') {
            snprintf(ticket, sizeof ticket, "%s", given);
        } else {
            tgg_account_status a;
            tgg_account_poll(&a);
            if (a.state == TGG_ACCOUNT_SIGN_IN) {
                m.st.state = MEET_SIGN_IN;
                snprintf(m.st.signin_code, sizeof m.st.signin_code, "%s", a.code);
                snprintf(m.st.signin_url, sizeof m.st.signin_url, "%s", a.url);
                return;
            }
            if (a.state == TGG_ACCOUNT_FAILED) {
                fail(a.message);
                return;
            }
            if (a.state != TGG_ACCOUNT_DONE) {
                return;
            }
            snprintf(ticket, sizeof ticket, "%s", a.ticket);
            snprintf(m.st.name, sizeof m.st.name, "%s", a.name);
        }
        const char* base = getenv("TGG_MEET_URL");
        char url[900];
        snprintf(url, sizeof url, "%s?ticket=%s", base != NULL && base[0] != '\0' ? base : MEET_URL,
                 ticket);
        m.ws = http_ws_open(url, NULL);
        if (m.ws == NULL) {
            fail("Couldn't reach textures.gg.");
            return;
        }
        m.st.state = MEET_STARTING;
        m.step = STEP_SOCKET;
    }
    if (m.ws == NULL) {
        return;
    }
    const int ws_state = http_ws_state(m.ws);
    char msg[8192];
    int n;
    while (m.ws != NULL && (n = http_ws_receive(m.ws, msg, sizeof msg)) != 0) {
        if (n > 0) {
            got(msg);
        } else {
            msg[0] = '\0';
            http_ws_receive(m.ws, NULL, 0); /* too big for us: can't be ours */
            break;
        }
    }
    if (m.ws == NULL || m.st.state == MEET_FAILED) {
        return;
    }
    if (ws_state == HTTP_WS_CLOSED) {
        if (m.step < STEP_DONE) {
            char why[320];
            snprintf(why, sizeof why, "Lost the connection to textures.gg: %s",
                     http_ws_error(m.ws));
            fail(why);
        }
        return;
    }
    if (ws_state != HTTP_WS_OPEN) {
        return;
    }
    if (!m.sent_hello) {
        send_hello();
        m.sent_hello = 1;
    }
    if (!m.sent_candidates) {
        m.sent_candidates = send_candidates();
        if (!m.sent_candidates) {
            return;
        }
    }
    if (!m.sent_room) {
        char room[96];
        if (m.joining) {
            char code[32];
            quote(code, sizeof code, m.want_code);
            snprintf(room, sizeof room, "{\"type\":\"room.join\",\"code\":%s}", code);
        } else {
            snprintf(room, sizeof room, "{\"type\":\"room.make\",\"games\":2}");
        }
        send_json(room);
        m.sent_room = 1;
        m.step = STEP_ROOM;
    }
    relay_step();
    if (m.step == STEP_PUNCH) {
        netplay_info info;
        netplay_status(&info);
        if (info.state == NETPLAY_FAILED) {
            could_not_connect(info.why);
        } else if (info.state == NETPLAY_CONNECTED || info.state == NETPLAY_MISMATCH ||
                   info.state == NETPLAY_PLAYING || info.state == NETPLAY_STARTING)
        {
            double ms = 0;
            const char* how = netplay_route(&ms);
            char report[160];
            snprintf(report, sizeof report, "{\"type\":\"connected\",\"how\":\"%s\",\"ms\":%.0f}",
                     how[0] != '\0' ? how : "other", ms > 0 ? ms : 0);
            if (!m.reported) {
                send_json(report);
            }
            m.reported = 1;
            m.st.state = MEET_CONNECTED;
            m.step = STEP_DONE;
        }
    }
}

static int start(int joining, const char* code)
{
    meet_leave();
    memset(&m, 0, sizeof m);
    m.joining = joining;
    snprintf(m.want_code, sizeof m.want_code, "%s", code != NULL ? code : "");
    m.st.state = MEET_STARTING;
    const char* why = NULL;
    if (netplay_prepare(UDP_PORT, &why) != 0 && netplay_prepare(0, &why) != 0) {
        fail(why != NULL ? why : "Couldn't open a network port.");
        return -1;
    }
    const char* given = getenv("TGG_MEET_TICKET");
    if ((given == NULL || given[0] == '\0') && tgg_account_ticket("meet") != 0) {
        tgg_account_status a;
        tgg_account_poll(&a);
        fail(a.message[0] != '\0' ? a.message
                                  : "Online needs the tgg that comes with the game beside it.");
        return -1;
    }
    m.step = STEP_TICKET;
    step();
    return 0;
}

int meet_make(void)
{
    return start(0, NULL);
}

int meet_join(const char* code)
{
    return start(1, code);
}

void meet_poll(meet_status* out)
{
    step();
    if (out != NULL) {
        *out = m.st;
    }
}

void meet_leave(void)
{
    if (m.ws != NULL) {
        if (http_ws_state(m.ws) == HTTP_WS_OPEN) {
            report_relay_bytes(1);
            send_json("{\"type\":\"room.leave\"}");
        }
        http_ws_close(m.ws);
        m.ws = NULL;
    }
    tgg_account_cancel();
    if (m.st.state != MEET_CONNECTED && m.st.state != MEET_OFF) {
        netplay_info info;
        netplay_status(&info);
        if (info.state == NETPLAY_OFF || info.state == NETPLAY_WAITING ||
            info.state == NETPLAY_CONNECTING || info.state == NETPLAY_FAILED)
        {
            netplay_close();
        }
    }
    memset(&m.st, 0, sizeof m.st);
    m.step = STEP_TICKET;
}

/* Moves on each frame, whether the screen polls or not */
static void on_frame(void)
{
    step();
}
TGG_ON(frame, on_frame);
