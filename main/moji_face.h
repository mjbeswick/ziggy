// moji_face -- GERTY-style emoticon renderer for the round 466x466 AMOLED.
//
// Everything is drawn as vector strokes in one LVGL object: a thin amber
// ring, two eyes, brows, a mouth, plus small glyph accents.  Emotions are
// parameter presets; a 20 ms tick eases the live parameters toward the
// preset and adds breathing, blinking, pupil drift and a talking mouth.
//
// Palette and layout deliberately echo GERTY from "Moon" (2009): warm amber
// line art on near-black, a faint scanline texture, a calm circular face.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EMO_NEUTRAL = 0,
    EMO_HAPPY,
    EMO_EXCITED,
    EMO_LISTENING,
    EMO_THINKING,
    EMO_CONFUSED,
    EMO_SAD,
    EMO_SLEEPY,
    EMO_SURPRISED,
    EMO_STERN,
    EMO_COUNT
} emo_t;

const char *emo_name(emo_t e);

// Create the face object on `parent` (usually the active screen).
lv_obj_t *face_create(lv_obj_t *parent);

// Ease toward an emotion. intensity 0..1 scales the expression.
void face_set_emotion(emo_t e, float intensity);

// Immediate expression override for one-off reactions.
void face_poke(void);

// 0..1: microphone level (drives the listening ring).
void face_set_level(float level);

// 0..1: current speech amplitude (drives the mouth while speaking).
void face_set_talking(float amp);

// Call from an LVGL timer, ~20 ms.
void face_tick(uint32_t now_ms);

emo_t face_emotion(void);
bool face_is_settled(void);

#ifdef __cplusplus
}
#endif
