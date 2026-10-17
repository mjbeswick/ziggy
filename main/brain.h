#pragma once

#include <stdbool.h>
#include "moji_face.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    emo_t emotion;
    const char *reply;   // "" means: stay quiet
} brain_reply_t;

// Decide what Ziggy says and how it feels.  The reply string stays valid
// until the next call.
brain_reply_t brain_think(const char *text);

// A short unprompted remark while the room is quiet.  Empty reply = stay quiet.
brain_reply_t brain_idle_line(int idle_seconds);

// Load remembered notes from NVS and reset the session counters.
void brain_init(void);

#ifdef __cplusplus
}
#endif
