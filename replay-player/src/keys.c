/* The keyboard, for the playback controls. The mod API has no input for
 * presentation code, and SDL, which the game reads the keyboard with, is
 * linked into the game without exporting it, so this asks the X server
 * directly: libX11, which the game's window already loaded under X11 (and
 * XWayland), and XQueryKeymap, which says which keys are down on the whole
 * display, whatever window has focus. Without X there are no controls. */
#include "player.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

typedef void* (*XOpenDisplayFn)(const char*);
typedef int (*XQueryKeymapFn)(void*, char[32]);
typedef unsigned char (*XKeysymToKeycodeFn)(void*, unsigned long);

/* X keysyms */
#define XK_space 0x0020
#define XK_period 0x002e
#define XK_minus 0x002d
#define XK_equal 0x003d
#define XK_0 0x0030
#define XK_Right 0xff53
#define XK_Up 0xff52
#define XK_Down 0xff54

static struct {
    void* display;
    XQueryKeymapFn query;
    unsigned char code[8];
    char last[32];
} x;

enum { K_PAUSE, K_STEP, K_STEP2, K_FASTER, K_FASTER2, K_SLOWER, K_SLOWER2, K_NORMAL };
static const unsigned long keysyms[8] = {
    XK_space, XK_period, XK_Right, XK_equal, XK_Up, XK_minus, XK_Down, XK_0,
};

int keys_open(void)
{
    void* lib = dlopen("libX11.so.6", RTLD_NOW | RTLD_NOLOAD);
    if (lib == NULL) {
        lib = dlopen("libX11.so.6", RTLD_NOW);
    }
    if (lib == NULL || getenv("DISPLAY") == NULL) {
        tgg_log("no X display, so no playback keys");
        return 0;
    }
    XOpenDisplayFn open = (XOpenDisplayFn) dlsym(lib, "XOpenDisplay");
    XKeysymToKeycodeFn to_code = (XKeysymToKeycodeFn) dlsym(lib, "XKeysymToKeycode");
    x.query = (XQueryKeymapFn) dlsym(lib, "XQueryKeymap");
    x.display = open != NULL ? open(NULL) : NULL;
    if (x.display == NULL || to_code == NULL || x.query == NULL) {
        tgg_log("can't reach the X display, so no playback keys");
        x.display = NULL;
        return 0;
    }
    for (int i = 0; i < 8; i++) {
        x.code[i] = to_code(x.display, keysyms[i]);
    }
    tgg_log("keys: space pauses, . or right steps a frame, = or up is faster, - or down "
            "slower, 0 normal speed");
    return 1;
}

static int pressed(const char* now, int key)
{
    const unsigned char c = x.code[key];
    const int down = (now[c >> 3] >> (c & 7)) & 1;
    const int was = (x.last[c >> 3] >> (c & 7)) & 1;
    return c != 0 && down && !was;
}

Keys keys_pressed(void)
{
    Keys k = { 0 };
    if (x.display == NULL) {
        return k;
    }
    char now[32];
    x.query(x.display, now);
    k.pause = pressed(now, K_PAUSE);
    k.step = pressed(now, K_STEP) || pressed(now, K_STEP2);
    k.faster = pressed(now, K_FASTER) || pressed(now, K_FASTER2);
    k.slower = pressed(now, K_SLOWER) || pressed(now, K_SLOWER2);
    k.normal = pressed(now, K_NORMAL);
    memcpy(x.last, now, sizeof now);
    return k;
}
