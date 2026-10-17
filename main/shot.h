#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Render the active screen to a PSRAM buffer and dump it on stdout as
//   SHOT <w> <h> RGB565\n
//   <w*h*2 bytes>
//   \nEND\n
// Used by tools/shot.py for development screenshots.
void shot_take(void);

#ifdef __cplusplus
}
#endif
