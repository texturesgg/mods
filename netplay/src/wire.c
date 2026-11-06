/* What goes over the network: a port's input as the game made it, and the
 * packets, written field by field in little-endian order so both ends read
 * them alike whatever their compiler lays a struct out as. */
#include "netplay.h"

#include <sysdolphin/baselib/controller.h>

#include <string.h>

WireInput wire_from_pad(const HSD_PadStatus* s, const PADStatus* raw)
{
    WireInput in;
    memset(&in, 0, sizeof in);
    in.buttons = s->button;
    in.physical = raw->button;
    in.raw_x = raw->stickX;
    in.raw_y = raw->stickY;
    in.raw_cx = raw->substickX;
    in.raw_cy = raw->substickY;
    in.raw_l = raw->triggerLeft;
    in.raw_r = raw->triggerRight;
    in.x = s->stickX;
    in.y = s->stickY;
    in.cx = s->subStickX;
    in.cy = s->subStickY;
    in.l = s->analogL;
    in.r = s->analogR;
    in.err = s->err;
    in.nx = s->nml_stickX;
    in.ny = s->nml_stickY;
    in.ncx = s->nml_subStickX;
    in.ncy = s->nml_subStickY;
    in.nl = s->nml_analogL;
    in.nr = s->nml_analogR;
    return in;
}

void wire_to_pad(const WireInput* in, HSD_PadStatus* s, PADStatus* raw)
{
    raw->err = in->err;
    raw->button = in->physical;
    raw->stickX = in->raw_x;
    raw->stickY = in->raw_y;
    raw->substickX = in->raw_cx;
    raw->substickY = in->raw_cy;
    raw->triggerLeft = in->raw_l;
    raw->triggerRight = in->raw_r;
    raw->analogA = 0;
    raw->analogB = 0;

    s->err = in->err;
    s->button = in->buttons;
    s->stickX = in->x;
    s->stickY = in->y;
    s->subStickX = in->cx;
    s->subStickY = in->cy;
    s->analogL = in->l;
    s->analogR = in->r;
    s->analogA = 0;
    s->analogB = 0;
    s->nml_stickX = in->nx;
    s->nml_stickY = in->ny;
    s->nml_subStickX = in->ncx;
    s->nml_subStickY = in->ncy;
    s->nml_analogL = in->nl;
    s->nml_analogR = in->nr;
    s->nml_analogA = 0;
    s->nml_analogB = 0;
}

WireInput wire_neutral(void)
{
    WireInput in;
    memset(&in, 0, sizeof in);
    return in;
}

int wire_same(const WireInput* a, const WireInput* b)
{
    return memcmp(a, b, sizeof *a) == 0;
}

/* Writing and reading, a field at a time */

typedef struct Cursor {
    uint8_t* w;
    const uint8_t* r;
    const uint8_t* end;
    int bad;
} Cursor;

static void put(Cursor* c, uint64_t v, int bytes)
{
    for (int i = 0; i < bytes; i++) {
        *c->w++ = (uint8_t) (v >> (8 * i));
    }
}

static uint64_t get(Cursor* c, int bytes)
{
    if (c->r + bytes > c->end) {
        c->bad = 1;
        return 0;
    }
    uint64_t v = 0;
    for (int i = 0; i < bytes; i++) {
        v |= (uint64_t) *c->r++ << (8 * i);
    }
    return v;
}

static void put_f(Cursor* c, float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof bits);
    put(c, bits, 4);
}

static float get_f(Cursor* c)
{
    const uint32_t bits = (uint32_t) get(c, 4);
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

static void put_bytes(Cursor* c, const void* p, size_t n)
{
    memcpy(c->w, p, n);
    c->w += n;
}

static void get_bytes(Cursor* c, void* p, size_t n)
{
    if (c->r + n > c->end) {
        c->bad = 1;
        return;
    }
    memcpy(p, c->r, n);
    c->r += n;
}

static void put_input(Cursor* c, const WireInput* in)
{
    put(c, in->buttons, 4);
    put(c, in->physical, 2);
    put(c, (uint8_t) in->raw_x, 1);
    put(c, (uint8_t) in->raw_y, 1);
    put(c, (uint8_t) in->raw_cx, 1);
    put(c, (uint8_t) in->raw_cy, 1);
    put(c, in->raw_l, 1);
    put(c, in->raw_r, 1);
    put(c, (uint8_t) in->x, 1);
    put(c, (uint8_t) in->y, 1);
    put(c, (uint8_t) in->cx, 1);
    put(c, (uint8_t) in->cy, 1);
    put(c, in->l, 1);
    put(c, in->r, 1);
    put(c, (uint8_t) in->err, 1);
    put_f(c, in->nx);
    put_f(c, in->ny);
    put_f(c, in->ncx);
    put_f(c, in->ncy);
    put_f(c, in->nl);
    put_f(c, in->nr);
}

static void get_input(Cursor* c, WireInput* in)
{
    memset(in, 0, sizeof *in);
    in->buttons = (uint32_t) get(c, 4);
    in->physical = (uint16_t) get(c, 2);
    in->raw_x = (int8_t) get(c, 1);
    in->raw_y = (int8_t) get(c, 1);
    in->raw_cx = (int8_t) get(c, 1);
    in->raw_cy = (int8_t) get(c, 1);
    in->raw_l = (uint8_t) get(c, 1);
    in->raw_r = (uint8_t) get(c, 1);
    in->x = (int8_t) get(c, 1);
    in->y = (int8_t) get(c, 1);
    in->cx = (int8_t) get(c, 1);
    in->cy = (int8_t) get(c, 1);
    in->l = (uint8_t) get(c, 1);
    in->r = (uint8_t) get(c, 1);
    in->err = (int8_t) get(c, 1);
    in->nx = get_f(c);
    in->ny = get_f(c);
    in->ncx = get_f(c);
    in->ncy = get_f(c);
    in->nl = get_f(c);
    in->nr = get_f(c);
}

size_t wire_encode(const WirePacket* p, uint8_t* buf)
{
    Cursor c = { .w = buf };
    put(&c, WIRE_MAGIC, 4);
    put(&c, WIRE_VERSION, 1);
    put(&c, p->type, 1);
    if (p->type == WIRE_HELLO) {
        const WireHello* h = &p->u.hello;
        put_bytes(&c, h->build, sizeof h->build);
        put_bytes(&c, h->mod_set, sizeof h->mod_set);
        put(&c, h->side, 1);
        put(&c, h->delay, 1);
        put(&c, h->seed, 4);
        put(&c, h->replay, 4);
    } else if (p->type == WIRE_INPUT) {
        const WireInputs* in = &p->u.inputs;
        put(&c, (uint32_t) in->frame, 4);
        put(&c, (uint32_t) in->ack, 4);
        put(&c, in->match, 4);
        put(&c, (uint32_t) in->sum_frame, 4);
        put(&c, in->sum, 8);
        put(&c, in->ping, 4);
        put(&c, in->pong, 4);
        put(&c, (uint32_t) in->first, 4);
        put(&c, in->count, 1);
        for (int i = 0; i < in->count && i < WIRE_MAX_INPUTS; i++) {
            put_input(&c, &in->inputs[i]);
        }
    }
    return (size_t) (c.w - buf);
}

int wire_decode(const uint8_t* buf, size_t len, WirePacket* p)
{
    Cursor c = { .r = buf, .end = buf + len };
    if (get(&c, 4) != WIRE_MAGIC || get(&c, 1) != WIRE_VERSION) {
        return 0;
    }
    memset(p, 0, sizeof *p);
    p->type = (uint8_t) get(&c, 1);
    if (p->type == WIRE_HELLO) {
        WireHello* h = &p->u.hello;
        get_bytes(&c, h->build, sizeof h->build);
        get_bytes(&c, h->mod_set, sizeof h->mod_set);
        h->build[sizeof h->build - 1] = '\0';
        h->mod_set[sizeof h->mod_set - 1] = '\0';
        h->side = (uint8_t) get(&c, 1);
        h->delay = (uint8_t) get(&c, 1);
        h->seed = (uint32_t) get(&c, 4);
        h->replay = (uint32_t) get(&c, 4);
    } else if (p->type == WIRE_INPUT) {
        WireInputs* in = &p->u.inputs;
        in->frame = (int32_t) get(&c, 4);
        in->ack = (int32_t) get(&c, 4);
        in->match = (uint32_t) get(&c, 4);
        in->sum_frame = (int32_t) get(&c, 4);
        in->sum = get(&c, 8);
        in->ping = (uint32_t) get(&c, 4);
        in->pong = (uint32_t) get(&c, 4);
        in->first = (int32_t) get(&c, 4);
        in->count = (uint8_t) get(&c, 1);
        if (in->count > WIRE_MAX_INPUTS) {
            return 0;
        }
        for (int i = 0; i < in->count; i++) {
            get_input(&c, &in->inputs[i]);
        }
    } else if (p->type != WIRE_BYE) {
        return 0;
    }
    return !c.bad;
}
