// ES7210 microphone in, ES8311 speaker out, on the board's shared I2S bus.
// Only one direction is ever open: the codecs share the bus clock, and the
// board routes mic and speaker through the same I2S pins.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int audio_init(void);

// Open the mic for 16 kHz capture.  Returns 0 on success.
int audio_mic_start(void);
void audio_mic_stop(void);
// Read `frames` mono samples.  Blocking.
int audio_mic_read(int16_t *mono, int frames);

// Play 24 kHz mono PCM, updating the face with the speech amplitude.
int audio_play(const int16_t *pcm, int n);
void audio_stop(void);

void audio_set_volume(int percent);

#ifdef __cplusplus
}
#endif
