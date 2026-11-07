/* A fake network for tests: each datagram sent waits its latency, plus or
 * minus the jitter, and some are lost. Jitter reorders them. The randomness
 * is its own generator, seeded from the spec, never the game's. */
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HELD_MAX 512

typedef struct Held {
    double due;
    size_t len;
    uint8_t data[WIRE_MAX_BYTES];
} Held;

static struct {
    int on;
    double latency, jitter, loss;
    uint64_t rng;
    Held held[HELD_MAX];
    int count;
    unsigned sent, lost;
} fk;

static double next_random(void)
{
    /* xorshift64*: a number in [0, 1) */
    fk.rng ^= fk.rng >> 12;
    fk.rng ^= fk.rng << 25;
    fk.rng ^= fk.rng >> 27;
    return (double) ((fk.rng * 0x2545F4914F6CDD1Dull) >> 11) / (double) (1ull << 53);
}

void fakenet_init(const char* spec)
{
    fk.count = 0;
    if (spec == NULL || spec[0] == '\0') {
        return;
    }
    fk.on = 1;
    fk.rng = 0x9E3779B97F4A7C15ull;
    char copy[256];
    snprintf(copy, sizeof copy, "%s", spec);
    for (char* part = strtok(copy, ","); part != NULL; part = strtok(NULL, ",")) {
        char* eq = strchr(part, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        const double value = atof(eq + 1);
        if (strcmp(part, "latency") == 0) {
            fk.latency = value;
        } else if (strcmp(part, "jitter") == 0) {
            fk.jitter = value;
        } else if (strcmp(part, "loss") == 0) {
            fk.loss = value / 100.0;
        } else if (strcmp(part, "seed") == 0) {
            fk.rng ^= (uint64_t) value * 0xD1B54A32D192ED03ull;
        }
    }
    if (fk.rng == 0) {
        fk.rng = 1;
    }
    tgg_log("fake network: %.0f ms each way, jitter %.0f ms, %.1f%% lost", fk.latency, fk.jitter,
            fk.loss * 100.0);
}

void net_send(const void* data, size_t len)
{
    if (!fk.on) {
        net_send_now(data, len);
        return;
    }
    fk.sent++;
    if (next_random() < fk.loss) {
        fk.lost++;
        return;
    }
    if (fk.count == HELD_MAX || len > WIRE_MAX_BYTES) {
        fk.lost++;
        return;
    }
    double delay = fk.latency + (next_random() * 2.0 - 1.0) * fk.jitter;
    if (delay < 0) {
        delay = 0;
    }
    Held* h = &fk.held[fk.count++];
    h->due = net_now_ms() + delay;
    h->len = len;
    memcpy(h->data, data, len);
}

void fakenet_flush(void)
{
    if (!fk.on) {
        return;
    }
    const double now = net_now_ms();
    int kept = 0;
    for (int i = 0; i < fk.count; i++) {
        if (fk.held[i].due <= now) {
            net_send_now(fk.held[i].data, fk.held[i].len);
        } else {
            if (kept != i) {
                fk.held[kept] = fk.held[i];
            }
            kept++;
        }
    }
    fk.count = kept;
}
