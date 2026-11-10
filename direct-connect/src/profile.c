/* Two mod profiles side by side: the host's (netplay_host_profile) and this
 * game's (tgg_mod_set_profile), as the lines the screen shows. A profile is
 * the port's JSON:
 *
 *     {"format": 1, "game": {"name": "tgg-melee", "version": "0.1.0",
 *      "layout": "…", "settings": {"tgg.unlocks": true}},
 *      "mods": [{"id": "tgg.ucf", "version": "0.84.0", "settings": {…}}]}
 *
 * Only what the screen compares is read: the game's version, the built-in
 * modules' states, and each mod's id, version and settings (as their text:
 * both come from the same port code, so the same settings read the same). */
#include "dc.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define MODS_MAX 48
#define TEXT_MAX 64

typedef struct Item {
    char id[TEXT_MAX];
    char version[32];
    char settings[256]; /* a mod's settings as written; a module's state */
} Item;

typedef struct Profile {
    char game[48];
    Item modules[8]; /* game.settings: version holds "true" or "false" */
    int n_modules;
    Item mods[MODS_MAX];
    int n_mods;
} Profile;

/* ---- Reading: just enough JSON */

static void ws(const char** p)
{
    while (isspace((unsigned char) **p)) {
        (*p)++;
    }
}

/* A string into out (cut to cap); 0 if there isn't one */
static int string(const char** p, char* out, size_t cap)
{
    ws(p);
    if (**p != '"') {
        return 0;
    }
    (*p)++;
    size_t n = 0;
    while (**p != '"' && **p != '\0') {
        char c = *(*p)++;
        if (c == '\\' && **p != '\0') {
            c = *(*p)++;
            if (c == 'u') {
                for (int i = 0; i < 4 && isxdigit((unsigned char) **p); i++) {
                    (*p)++;
                }
                c = '?';
            }
        }
        if (n + 1 < cap) {
            out[n++] = c;
        }
    }
    if (**p != '"') {
        return 0;
    }
    (*p)++;
    if (cap > 0) {
        out[n] = '\0';
    }
    return 1;
}

/* Skips a value; its text (from its first character) is [*start, *p) */
static int skip(const char** p, int depth)
{
    ws(p);
    if (depth > 16) {
        return 0;
    }
    char scratch[2];
    if (**p == '"') {
        return string(p, scratch, sizeof scratch);
    }
    if (**p == '{' || **p == '[') {
        const char close = **p == '{' ? '}' : ']';
        (*p)++;
        ws(p);
        if (**p == close) {
            (*p)++;
            return 1;
        }
        for (;;) {
            if (close == '}') {
                if (!string(p, scratch, sizeof scratch)) {
                    return 0;
                }
                ws(p);
                if (**p != ':') {
                    return 0;
                }
                (*p)++;
            }
            if (!skip(p, depth + 1)) {
                return 0;
            }
            ws(p);
            if (**p == ',') {
                (*p)++;
                continue;
            }
            if (**p == close) {
                (*p)++;
                return 1;
            }
            return 0;
        }
    }
    const char* start = *p;
    while (**p != '\0' && **p != ',' && **p != '}' && **p != ']' && !isspace((unsigned char) **p)) {
        (*p)++;
    }
    return *p > start;
}

/* A value's text, cut to cap */
static int raw(const char** p, char* out, size_t cap)
{
    ws(p);
    const char* start = *p;
    if (!skip(p, 0)) {
        return 0;
    }
    const size_t n = (size_t) (*p - start);
    snprintf(out, cap, "%.*s", (int) (n < cap ? n : cap - 1), start);
    return 1;
}

/* Calls fn for each member of the object at *p */
static int members(const char** p, int (*fn)(const char* key, const char** p, void* user),
                   void* user)
{
    ws(p);
    if (**p != '{') {
        return 0;
    }
    (*p)++;
    ws(p);
    if (**p == '}') {
        (*p)++;
        return 1;
    }
    for (;;) {
        char key[TEXT_MAX];
        if (!string(p, key, sizeof key)) {
            return 0;
        }
        ws(p);
        if (**p != ':') {
            return 0;
        }
        (*p)++;
        if (!fn(key, p, user)) {
            return 0;
        }
        ws(p);
        if (**p == ',') {
            (*p)++;
            continue;
        }
        if (**p == '}') {
            (*p)++;
            return 1;
        }
        return 0;
    }
}

static int module_member(const char* key, const char** p, void* user)
{
    Profile* pr = user;
    if (pr->n_modules == (int) (sizeof pr->modules / sizeof pr->modules[0])) {
        return skip(p, 0);
    }
    Item* m = &pr->modules[pr->n_modules++];
    snprintf(m->id, sizeof m->id, "%s", key);
    return raw(p, m->version, sizeof m->version);
}

static int game_member(const char* key, const char** p, void* user)
{
    Profile* pr = user;
    if (strcmp(key, "version") == 0) {
        return string(p, pr->game, sizeof pr->game);
    }
    if (strcmp(key, "settings") == 0) {
        return members(p, module_member, pr);
    }
    return skip(p, 0);
}

static int mod_member(const char* key, const char** p, void* user)
{
    Item* m = user;
    if (strcmp(key, "id") == 0) {
        return string(p, m->id, sizeof m->id);
    }
    if (strcmp(key, "version") == 0) {
        return string(p, m->version, sizeof m->version);
    }
    if (strcmp(key, "settings") == 0) {
        return raw(p, m->settings, sizeof m->settings);
    }
    return skip(p, 0);
}

static int top_member(const char* key, const char** p, void* user)
{
    Profile* pr = user;
    if (strcmp(key, "game") == 0) {
        return members(p, game_member, pr);
    }
    if (strcmp(key, "mods") != 0) {
        return skip(p, 0);
    }
    ws(p);
    if (**p != '[') {
        return 0;
    }
    (*p)++;
    ws(p);
    if (**p == ']') {
        (*p)++;
        return 1;
    }
    for (;;) {
        Item scratch;
        Item* m = pr->n_mods < MODS_MAX ? &pr->mods[pr->n_mods++] : &scratch;
        memset(m, 0, sizeof *m);
        if (!members(p, mod_member, m)) {
            return 0;
        }
        ws(p);
        if (**p == ',') {
            (*p)++;
            continue;
        }
        if (**p == ']') {
            (*p)++;
            return 1;
        }
        return 0;
    }
}

static int read_profile(const char* json, Profile* out)
{
    memset(out, 0, sizeof *out);
    const char* p = json;
    return json != NULL && members(&p, top_member, out);
}

/* ---- Comparing */

static const Item* find(const Item* items, int n, const char* id)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(items[i].id, id) == 0) {
            return &items[i];
        }
    }
    return NULL;
}

static void add(char lines[][DIFF_TEXT], int* n, const char* text)
{
    if (*n < DIFF_LINES) {
        strncpy(lines[*n], text, DIFF_TEXT - 1);
        lines[*n][DIFF_TEXT - 1] = '\0';
    }
    (*n)++;
}

/* A built-in module is on unless a profile says false */
static const char* module_state(const Profile* pr, const char* id)
{
    const Item* m = find(pr->modules, pr->n_modules, id);
    return m != NULL && strcmp(m->version, "false") == 0 ? "off" : "on";
}

static void compare_modules(const Profile* a, const Profile* b, Diff* d, int a_is_host)
{
    for (int i = 0; i < a->n_modules; i++) {
        const char* id = a->modules[i].id;
        if (!a_is_host && find(b->modules, b->n_modules, id) != NULL) {
            continue; /* already compared from the host's side */
        }
        const char* host = module_state(a_is_host ? a : b, id);
        const char* here = module_state(a_is_host ? b : a, id);
        if (strcmp(host, here) != 0) {
            char line[160];
            snprintf(line, sizeof line, "%s %s", id, host);
            add(d->there, &d->n_there, line);
            snprintf(line, sizeof line, "%s %s", id, here);
            add(d->here, &d->n_here, line);
        }
    }
}

int profile_diff(const char* host_json, const char* own_json, Diff* d)
{
    static Profile host, own;
    memset(d, 0, sizeof *d);
    if (!read_profile(host_json, &host) || !read_profile(own_json, &own)) {
        return -1;
    }
    snprintf(d->game_there, sizeof d->game_there, "%s", host.game);
    snprintf(d->game_here, sizeof d->game_here, "%s", own.game);
    compare_modules(&host, &own, d, 1);
    compare_modules(&own, &host, d, 0);
    char line[160];
    for (int i = 0; i < host.n_mods; i++) {
        const Item* h = &host.mods[i];
        const Item* o = find(own.mods, own.n_mods, h->id);
        if (o == NULL || strcmp(o->version, h->version) != 0) {
            snprintf(line, sizeof line, "%s %s", h->id, h->version);
            add(d->there, &d->n_there, line);
            if (o != NULL) {
                snprintf(line, sizeof line, "%s %s", o->id, o->version);
                add(d->here, &d->n_here, line);
            }
        } else if (strcmp(o->settings, h->settings) != 0) {
            snprintf(line, sizeof line, "%s %s, other settings", h->id, h->version);
            add(d->there, &d->n_there, line);
            snprintf(line, sizeof line, "%s %s, other settings", o->id, o->version);
            add(d->here, &d->n_here, line);
        }
    }
    for (int i = 0; i < own.n_mods; i++) {
        const Item* o = &own.mods[i];
        if (find(host.mods, host.n_mods, o->id) == NULL) {
            snprintf(line, sizeof line, "%s %s", o->id, o->version);
            add(d->here, &d->n_here, line);
        }
    }
    return 0;
}
