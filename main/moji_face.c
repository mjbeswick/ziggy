// Bumble's face: a big yellow smiley with black ink, drawn as vectors.
//
// One LVGL object, one draw callback.  Emotions are parameter presets; the
// tick eases toward them and adds breathing, blinking, eye wander, idle
// gestures (winks, yawns, glances) and a talking mouth.
#include "moji_face.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CX 233
#define CY 233
#define YELLOW 0xFFCE2E
#define YELLOW_DARK 0xE0A800
#define INK 0x161208

typedef enum {
    EX_NONE = 0,
    EX_SPARKLE,
    EX_HEART,
    EX_ZZZ,
    EX_QUESTION,
    EX_SWEAT,
    EX_ELLIPSIS,
    EX_TEAR,
} extra_t;

typedef struct {
    float eye_open, eye_r, eye_dx, eye_dy, eye_curve, look_x, look_y;
    float brow, brow_angle;
    float mouth_curve, mouth_w, mouth_open, mouth_y;
    float glow, bounce, blush;
    int extra;
} face_params_t;

typedef struct {
    face_params_t cur, target;
    emo_t emotion;
    bool poking;
    uint32_t poke_until;

    float level;
    float talking;
    float mouth_talk;

    uint32_t next_blink;
    uint32_t blink_start;
    bool blinking;
    int blink_count;

    int wink;                 // 0 none, -1 left eye, +1 right eye
    uint32_t wink_until;
    uint32_t next_gesture;
    int gesture;              // 0 none, 1 glance, 2 brow, 3 yawn, 4 wiggle
    uint32_t gesture_until;
    float glance;

    uint32_t started;
} face_state_t;

static face_state_t g;
static lv_obj_t *s_obj;

static const face_params_t P[EMO_COUNT] = {
    /* NEUTRAL */ {1, 10.5f, 50, -24, .15f, 0, 0, 0, 0,
                   .25f, .85f, 0, 54, .50f, 0, .10f, EX_NONE},
    /* HAPPY */ {1, 10, 52, -26, 1, 0, -2, .30f, 0,
                1.0f, 1.05f, 0, 54, .70f, 0, .55f, EX_NONE},
    /* EXCITED */ {1, 10.5f, 53, -27, 1, 3, -6, .55f, -.05f,
                  1.0f, 1.15f, .28f, 55, .95f, .012f, .65f, EX_SPARKLE},
    /* LISTENING */ {1, 12, 54, -28, .05f, 0, -3, .45f, 0,
                    .05f, .55f, .18f, 56, .80f, 0, .15f, EX_NONE},
    /* THINKING */ {.9f, 9, 50, -24, .2f, 8, -11, .55f, -.15f,
                   0, .5f, 0, 54, .55f, 0, .10f, EX_ELLIPSIS},
    /* CONFUSED */ {1, 10, 51, -25, 0, -7, -4, .85f, .55f,
                   -.25f, .7f, .05f, 55, .45f, 0, .10f, EX_QUESTION},
    /* SAD */ {.72f, 9, 50, -22, -.2f, 0, 7, .70f, .70f,
              -.85f, .75f, 0, 55, .30f, 0, 0, EX_TEAR},
    /* SLEEPY */ {.05f, 9, 50, -22, 0, 0, 4, .10f, 0,
                 .10f, .6f, 0, 56, .20f, 0, .05f, EX_ZZZ},
    /* SURPRISED */ {1.08f, 13, 56, -29, 0, 0, -3, .90f, 0,
                    0, .45f, .55f, 57, .95f, .02f, .10f, EX_SPARKLE},
    /* STERN */ {.62f, 10, 50, -24, -.15f, 0, 0, .90f, -.8f,
                -.5f, .8f, 0, 55, .40f, 0, 0, EX_NONE},
};

static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

const char *emo_name(emo_t e)
{
    static const char *n[EMO_COUNT] = {
        "neutral", "happy", "excited", "listening", "thinking",
        "confused", "sad", "sleepy", "surprised", "stern"};
    return (e >= 0 && e < EMO_COUNT) ? n[e] : "?";
}

/* ---------------------------------------------------------------- drawing */

static void arc(lv_layer_t *L, int cx, int cy, int r, int w,
                lv_color_t c, lv_opa_t opa, float a1, float a2, bool round)
{
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = cx;
    d.center.y = cy;
    d.radius = r;
    d.width = w;
    d.color = c;
    d.opa = opa;
    d.start_angle = a1;
    d.end_angle = a2;
    d.rounded = round;
    lv_draw_arc(L, &d);
}

static void disc(lv_layer_t *L, int cx, int cy, int rx, int ry,
                 lv_color_t c, lv_opa_t opa)
{
    if (rx < 1) rx = 1;
    if (ry < 1) ry = 1;
    lv_area_t a = {cx - rx, cy - ry, cx + rx - 1, cy + ry - 1};
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = c;
    d.bg_opa = opa;
    d.border_opa = LV_OPA_TRANSP;
    lv_draw_rect(L, &d, &a);
}

static void line(lv_layer_t *L, int x1, int y1, int x2, int y2,
                 int w, lv_color_t c, lv_opa_t opa)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.p1.x = x1;
    d.p1.y = y1;
    d.p2.x = x2;
    d.p2.y = y2;
    d.color = c;
    d.width = w;
    d.opa = opa;
    d.round_start = 1;
    d.round_end = 1;
    lv_draw_line(L, &d);
}

static void tri(lv_layer_t *L, int x1, int y1, int x2, int y2, int x3, int y3,
                lv_color_t c, lv_opa_t opa)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.p[0].x = x1;
    d.p[0].y = y1;
    d.p[1].x = x2;
    d.p[1].y = y2;
    d.p[2].x = x3;
    d.p[2].y = y3;
    d.color = c;
    d.opa = opa;
    lv_draw_triangle(L, &d);
}

static void glyph(lv_layer_t *L, const lv_font_t *font, const char *txt,
                  int x, int y, lv_color_t c, lv_opa_t opa)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = font;
    d.color = c;
    d.opa = opa;
    d.text = txt;
    d.text_local = 1;
    lv_area_t a = {x, y, x + 160, y + 48};
    lv_draw_label(L, &d, &a);
}

static void face_disc(lv_layer_t *L, int cx, int cy, int r)
{
    lv_area_t a = {cx - r, cy - r, cx + r - 1, cy + r - 1};
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_opa = LV_OPA_COVER;
    d.border_opa = LV_OPA_TRANSP;
    d.bg_color = lv_color_hex(0xFFDD5A);
    d.bg_grad.dir = LV_GRAD_DIR_VER;
    d.bg_grad.stops_count = 2;
    d.bg_grad.stops[0].color = lv_color_hex(0xFFDF66);
    d.bg_grad.stops[0].opa = LV_OPA_COVER;
    d.bg_grad.stops[0].frac = 0;
    d.bg_grad.stops[1].color = lv_color_hex(0xF2B300);
    d.bg_grad.stops[1].opa = LV_OPA_COVER;
    d.bg_grad.stops[1].frac = 255;
    lv_draw_rect(L, &d, &a);
}

static void heart(lv_layer_t *L, int x, int y, int s, lv_color_t c, lv_opa_t opa)
{
    disc(L, x - s / 2, y - s / 3, s / 2, s / 2, c, opa);
    disc(L, x + s / 2, y - s / 3, s / 2, s / 2, c, opa);
    tri(L, x - s, y - s / 3, x + s, y - s / 3, x, y + s, c, opa);
}

static void sparkle(lv_layer_t *L, int x, int y, int s, lv_color_t c, lv_opa_t opa)
{
    line(L, x - s, y, x + s, y, 2, c, opa);
    line(L, x, y - s, x, y + s, 2, c, opa);
    line(L, x - s * 2 / 3, y - s * 2 / 3, x + s * 2 / 3, y + s * 2 / 3, 1, c, opa / 2);
    line(L, x - s * 2 / 3, y + s * 2 / 3, x + s * 2 / 3, y - s * 2 / 3, 1, c, opa / 2);
}

static void draw_face(lv_event_t *e)
{
    lv_layer_t *L = lv_event_get_layer(e);
    if (!L) return;

    const face_params_t *p = &g.cur;
    uint32_t now = lv_tick_get();
    float t = (now - g.started) / 1000.0f;

    lv_color_t yellow = lv_color_hex(YELLOW);
    lv_color_t shade = lv_color_hex(YELLOW_DARK);
    lv_color_t ink = lv_color_hex(INK);
    lv_color_t cream = lv_color_hex(0xFFF3B0);

    // Scanlines go under the face: a hint of CRT around the edges only.
    for (int y = 0; y < 466; y += 4) {
        line(L, 0, y, 465, y, 1, shade, 6);
    }

    // Screen rim and tick marks.
    arc(L, CX, CY, 222, 1, shade, 26, 0, 360, false);
    arc(L, CX, CY, 217, 1, shade, 16, 0, 360, false);
    for (int i = 0; i < 24; i++) {
        float a = i * 15.0f;
        float rad = a * (float)M_PI / 180.0f;
        int x1 = CX + (int)(lroundf(cosf(rad) * 212));
        int y1 = CY + (int)(lroundf(sinf(rad) * 212));
        int x2 = CX + (int)(lroundf(cosf(rad) * 217));
        int y2 = CY + (int)(lroundf(sinf(rad) * 217));
        line(L, x1, y1, x2, y2, 1, shade, 34);
    }

    // Breathing, bob and the occasional squash.
    float s = 1.0f + 0.012f * sinf(t * 1.6f) + p->bounce * sinf(t * 7.0f);
    int cx = CX + (int)(2.0f * sinf(t * 0.7f));
    int cy = CY + (int)(2.0f * sinf(t * 0.9f) + 2.0f * sinf(t * 2.3f));
    int R = (int)(134 * s);

    // Listening meter: ticks that dance with the microphone level.
    if (g.level > 0.02f) {
        for (int i = 0; i < 40; i++) {
            float a = i * 9.0f - 90.0f;
            float wob = 0.55f + 0.45f * sinf(i * 0.7f + t * 6.0f);
            int len = 3 + (int)(16 * g.level * wob);
            float rad = a * (float)M_PI / 180.0f;
            int x1 = cx + (int)(cosf(rad) * 178);
            int y1 = cy + (int)(sinf(rad) * 178);
            int x2 = cx + (int)(cosf(rad) * (178 + len));
            int y2 = cy + (int)(sinf(rad) * (178 + len));
            lv_opa_t opa = (lv_opa_t)(40 + 150 * g.level * wob);
            line(L, x1, y1, x2, y2, 2, yellow, opa);
        }
    }

    // Thinking sweep around the rim.
    if (g.emotion == EMO_THINKING) {
        float a0 = fmodf(t * 210.0f, 360.0f);
        arc(L, cx, cy, 194, 2, yellow, 130, a0, a0 + 62, true);
    }

    // Halo, then the yellow face.
    if (p->glow > 0.05f) {
        arc(L, cx, cy, R + 5, 8, yellow, (lv_opa_t)(p->glow * 22), 0, 360, false);
        arc(L, cx, cy, R + 11, 4, yellow, (lv_opa_t)(p->glow * 12), 0, 360, false);
    }
    face_disc(L, cx, cy, R);
    arc(L, cx, cy, R, 2, shade, 230, 0, 360, false);
    arc(L, cx, cy, R - 3, 4, cream, 80, 205, 265, true);   // rim light
    arc(L, cx, cy, R - 4, 6, shade, 55, 25, 155, false);   // underside shade

    // Blush.
    if (p->blush > 0.02f) {
        lv_opa_t bo = (lv_opa_t)(p->blush * 75);
        disc(L, cx - 76, cy + 30, 22, 12, lv_color_hex(0xFF9A48), bo);
        disc(L, cx + 76, cy + 30, 22, 12, lv_color_hex(0xFF9A48), bo);
    }

    // Eyes.
    int exo = (int)(p->eye_dx * s);
    int eyo = (int)(p->eye_dy * s);
    int side_index = 0;
    for (int side = -1; side <= 1; side += 2, side_index++) {
        int ex = cx + side * exo + (int)(p->look_x * s);
        int ey = cy + eyo + (int)(p->look_y * s);
        int er = (int)(p->eye_r * s);

        bool closed = g.blinking || (g.wink == side && lv_tick_get() < g.wink_until);
        float open = closed ? 0 : p->eye_open;

        if (p->eye_curve > 0.35f) {                 // happy ^ ^
            int w = (int)(er * 1.4f);
            line(L, ex - w, ey + 6, ex - w / 2, ey - 3, 5, ink, 255);
            line(L, ex - w / 2, ey - 3, ex + w / 2, ey - 3, 5, ink, 255);
            line(L, ex + w / 2, ey - 3, ex + w, ey + 6, 5, ink, 255);
        } else if (open < (p->eye_open * 0.3f)) {    // closed, a soft curve
            int w = (int)(er * 1.35f);
            line(L, ex - w, ey - 1, ex, ey + 3, 4, ink, 255);
            line(L, ex, ey + 3, ex + w, ey - 1, 4, ink, 255);
        } else {
            disc(L, ex, ey, er, (int)(er * open), ink, 255);
            if (er > 5) {                            // catchlight
                disc(L, ex - er / 3, ey - er / 3, er / 4, er / 4, cream, 210);
            }
            if (open > 1.02f) {                      // wide-eyed
                disc(L, ex, ey, er + 1, (int)((er + 1) * open), ink, 255);
                disc(L, ex - er / 3, ey - er / 3, er / 3, er / 3, cream, 220);
            }
        }
        if (p->eye_curve < -0.15f) {                 // sad lid
            line(L, ex - (int)(er * 1.4f), ey - (int)(er * 0.9f),
                 ex + (int)(er * 1.4f), ey - (int)(er * 0.2f), 3, ink, 200);
        }

        // Brows.
        if (p->brow > 0.02f) {
            int by = ey - (int)(er * 2.7f) - (int)(p->brow * 2);
            int tilt = (int)(p->brow_angle * 9 * side);
            lv_opa_t bo = (lv_opa_t)(p->brow * 255);
            line(L, ex - (int)(er * 1.5f), by - tilt / 2,
                 ex + (int)(er * 1.5f), by + tilt / 2, 3, ink, bo);
        }
    }

    // Mouth.
    int mx = cx;
    int my = cy + (int)(p->mouth_y * s);
    int mw = (int)(p->mouth_w * s * 100);

    if (p->mouth_open > 0.10f) {
        int rx = (int)((14 + 10 * p->mouth_w) * s);
        int ry = (int)((5 + 32 * p->mouth_open) * s);
        disc(L, mx, my, rx, ry, ink, 255);
        // A little tongue when it is really open.
        if (p->mouth_open > 0.45f) {
            disc(L, mx, my + ry / 3, (int)(rx * 0.55f), (int)(ry * 0.45f),
                 lv_color_hex(0xC24A4A), 255);
        }
    } else if (p->mouth_curve > 0.18f) {
        arc(L, mx, my - (int)(36 * s), (int)(44 * mw / 100.0f * s), 5, ink, 255,
            28, 152, true);
    } else if (p->mouth_curve < -0.18f) {
        arc(L, mx, my + (int)(18 * s), (int)(36 * mw / 100.0f * s), 5, ink, 255,
            208, 332, true);
    } else if (p->extra == EX_QUESTION) {
        int half = (int)(p->mouth_w * 24 * s);
        int amp = (int)(5 * s);
        line(L, mx - half, my, mx - half / 2, my - amp, 4, ink, 255);
        line(L, mx - half / 2, my - amp, mx, my, 4, ink, 255);
        line(L, mx, my, mx + half / 2, my + amp, 4, ink, 255);
        line(L, mx + half / 2, my + amp, mx + half, my, 4, ink, 255);
    } else {
        int half = (int)(p->mouth_w * 26 * s);
        line(L, mx - half, my, mx + half, my, 4, ink, 255);
    }

    // Accents.
    float ph = fmodf(t, 3.0f);
    switch (p->extra) {
    case EX_SPARKLE: {
        int sp = (int)(7 + 2 * sinf(t * 5.0f));
        sparkle(L, cx + 96, cy - 92, sp, cream, 220);
        sparkle(L, cx - 100, cy - 56, sp - 2, cream, 160);
        break;
    }
    case EX_HEART:
        for (int i = 0; i < 2; i++) {
            float q = fmodf(ph + i * 1.5f, 3.0f) / 3.0f;
            heart(L, cx + 88 + (int)(8 * sinf(q * 6.0f)), cy - 60 - (int)(q * 70),
                  8, lv_color_hex(0xFF5A64), (lv_opa_t)(220 * (1.0f - q)));
        }
        break;
    case EX_ZZZ:
        for (int i = 0; i < 3; i++) {
            float q = fmodf(ph + i, 3.0f) / 3.0f;
            int x = cx + 78 + (int)(q * 26);
            int y = cy - 80 - (int)(q * 52);
            glyph(L, i == 0 ? &lv_font_unscii_16 : &lv_font_unscii_8, "z",
                  x, y, ink, (lv_opa_t)(200 * (1.0f - q * 0.8f)));
        }
        break;
    case EX_QUESTION:
        glyph(L, &lv_font_unscii_16, "?", cx + 80, cy - 98, ink, 230);
        break;
    case EX_SWEAT: {
        int y = cy - 34 + (int)(10 * sinf(t * 1.6f));
        disc(L, cx + 90, y, 5, 7, lv_color_hex(0x61C8FF), 220);
        tri(L, cx + 85, y - 4, cx + 95, y - 4, cx + 90, y - 17,
            lv_color_hex(0x61C8FF), 220);
        break;
    }
    case EX_ELLIPSIS: {
        int y = cy + 96;
        for (int i = -1; i <= 1; i++) {
            float q = 0.45f + 0.55f * sinf(t * 4.0f - i * 0.9f);
            disc(L, cx + i * 22, y, 5, 5, ink, (lv_opa_t)(80 + 170 * q));
        }
        break;
    }
    case EX_TEAR:
        for (int side = -1; side <= 1; side += 2) {
            int x = cx + side * (int)(p->eye_dx * s) + side * 8;
            int y = cy + (int)(p->eye_dy * s) + 26 + (int)(6 * sinf(t * 1.2f));
            disc(L, x, y, 3, 5, lv_color_hex(0x61C8FF), 220);
        }
        break;
    default:
        break;
    }
}

/* -------------------------------------------------------------- animation */

static void preset_lerp(face_params_t *dst, const face_params_t *a, const face_params_t *b, float t)
{
    dst->eye_open = lerpf(a->eye_open, b->eye_open, t);
    dst->eye_r = lerpf(a->eye_r, b->eye_r, t);
    dst->eye_dx = lerpf(a->eye_dx, b->eye_dx, t);
    dst->eye_dy = lerpf(a->eye_dy, b->eye_dy, t);
    dst->eye_curve = lerpf(a->eye_curve, b->eye_curve, t);
    dst->look_x = lerpf(a->look_x, b->look_x, t);
    dst->look_y = lerpf(a->look_y, b->look_y, t);
    dst->brow = lerpf(a->brow, b->brow, t);
    dst->brow_angle = lerpf(a->brow_angle, b->brow_angle, t);
    dst->mouth_curve = lerpf(a->mouth_curve, b->mouth_curve, t);
    dst->mouth_w = lerpf(a->mouth_w, b->mouth_w, t);
    dst->mouth_open = lerpf(a->mouth_open, b->mouth_open, t);
    dst->mouth_y = lerpf(a->mouth_y, b->mouth_y, t);
    dst->glow = lerpf(a->glow, b->glow, t);
    dst->bounce = lerpf(a->bounce, b->bounce, t);
    dst->blush = lerpf(a->blush, b->blush, t);
    dst->extra = t > 0.5f ? b->extra : a->extra;
}

static void apply_target(emo_t e, float intensity)
{
    if (intensity < 0) intensity = 0;
    if (intensity > 1) intensity = 1;
    preset_lerp(&g.target, &P[EMO_NEUTRAL], &P[e], intensity);
}

lv_obj_t *face_create(lv_obj_t *parent)
{
    memset(&g, 0, sizeof g);
    g.emotion = EMO_NEUTRAL;
    g.target = P[EMO_NEUTRAL];
    g.cur = P[EMO_NEUTRAL];
    g.next_blink = lv_tick_get() + 2200;
    g.next_gesture = lv_tick_get() + 4000;
    g.started = lv_tick_get();

    s_obj = lv_obj_create(parent);
    lv_obj_remove_style_all(s_obj);
    lv_obj_set_size(s_obj, 466, 466);
    lv_obj_set_pos(s_obj, 0, 0);
    lv_obj_set_scrollable(s_obj, false);
    lv_obj_set_clickable(s_obj, false);
    lv_obj_add_event_cb(s_obj, draw_face, LV_EVENT_DRAW_MAIN, NULL);
    return s_obj;
}

void face_set_emotion(emo_t e, float intensity)
{
    if (e < 0 || e >= EMO_COUNT) return;
    g.emotion = e;
    g.poking = false;
    apply_target(e, intensity);
}

void face_poke(void)
{
    if (g.poking) return;
    g.poking = true;
    g.poke_until = lv_tick_get() + 900;
    preset_lerp(&g.target, &P[EMO_NEUTRAL], &P[EMO_SURPRISED], 1.0f);
    g.target.extra = EX_HEART;
}

void face_set_level(float level)
{
    g.level = clampf(level, 0, 1);
}

void face_set_talking(float amp)
{
    g.talking = clampf(amp, 0, 1);
}

emo_t face_emotion(void) { return g.emotion; }

bool face_is_settled(void) { return !g.poking; }

void face_tick(uint32_t now_ms)
{
    static uint32_t last;
    if (!last) last = now_ms;
    float dt = (now_ms - last) / 1000.0f;
    if (dt > 0.2f) dt = 0.2f;
    last = now_ms;

    if (g.poking && now_ms > g.poke_until) {
        g.poking = false;
        apply_target(g.emotion, 1.0f);
    }

    // Blink scheduling.
    if (g.emotion != EMO_SLEEPY && !g.poking && now_ms > g.next_blink && !g.blinking) {
        g.blinking = true;
        g.blink_start = now_ms;
        g.blink_count++;
    }
    if (g.blinking) {
        float bt = (now_ms - g.blink_start) / 140.0f;
        if (bt >= 1.0f) {
            g.blinking = false;
            uint32_t gap = 2200 + (uint32_t)(rand() % 4600);
            if (g.blink_count % 5 == 0) gap = 200;
            g.next_blink = now_ms + gap;
        }
    }

    // Idle gestures, so it never looks frozen.
    bool idle = g.emotion == EMO_NEUTRAL || g.emotion == EMO_HAPPY;
    if (idle && !g.poking && now_ms > g.next_gesture && now_ms > g.gesture_until) {
        g.gesture = 1 + (rand() % 4);
        uint32_t dur = g.gesture == 3 ? 900 : 1100;
        g.gesture_until = now_ms + dur;
        if (g.gesture == 1) {
            g.glance = (rand() & 1) ? 13.0f : -13.0f;
            if (rand() % 3 == 0) {                       // sometimes a wink
                g.wink = (rand() & 1) ? 1 : -1;
                g.wink_until = now_ms + 380;
            }
        }
        if (g.gesture == 4) g.next_blink = now_ms + 120;   // wiggle with a blink
        g.next_gesture = now_ms + dur + 4500 + (uint32_t)(rand() % 6000);
    }

    float gx = 0;
    if (now_ms < g.gesture_until) {
        switch (g.gesture) {
        case 1: gx = g.glance; break;
        case 4: gx = 9.0f * sinf(now_ms / 70.0f); break;
        default: break;
        }
    }

    // Ease parameters toward the target.
    float k = 1.0f - expf(-dt * 7.0f);
    face_params_t *c = &g.cur;
    const face_params_t *t = &g.target;
    c->eye_open = lerpf(c->eye_open, t->eye_open, k);
    c->eye_r = lerpf(c->eye_r, t->eye_r, k);
    c->eye_dx = lerpf(c->eye_dx, t->eye_dx, k);
    c->eye_dy = lerpf(c->eye_dy, t->eye_dy, k);
    c->eye_curve = lerpf(c->eye_curve, t->eye_curve, k);
    c->look_x = lerpf(c->look_x, t->look_x + gx, k);
    c->look_y = lerpf(c->look_y, t->look_y, k);
    c->brow = lerpf(c->brow, t->brow, k);
    c->brow_angle = lerpf(c->brow_angle, t->brow_angle, k);
    c->mouth_curve = lerpf(c->mouth_curve, t->mouth_curve, k);
    c->mouth_w = lerpf(c->mouth_w, t->mouth_w, k);
    c->mouth_open = lerpf(c->mouth_open, t->mouth_open, k);
    c->mouth_y = lerpf(c->mouth_y, t->mouth_y, k);
    c->glow = lerpf(c->glow, t->glow, k);
    c->bounce = lerpf(c->bounce, t->bounce, k);
    c->blush = lerpf(c->blush, t->blush, k);
    c->extra = t->extra;

    // Gesture overrides, after the easing so they read clearly.
    if (now_ms < g.gesture_until) {
        if (g.gesture == 2) { c->brow = 0.95f; c->brow_angle = -0.35f; }
        if (g.gesture == 3) { c->eye_open = 0.04f; c->mouth_open = 0.62f; }
    }

    // Talking mouth: fast attack, gentle release.
    float mt = g.mouth_talk;
    mt += (g.talking - mt) * (g.talking > mt ? 0.55f : 0.18f);
    g.mouth_talk = mt;
    if (mt > 0.03f) {
        float open = 0.12f + 0.75f * mt;
        if (open > c->mouth_open) c->mouth_open = open;
    }

    if (s_obj) lv_obj_invalidate(s_obj);
}
