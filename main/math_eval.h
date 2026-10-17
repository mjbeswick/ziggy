// Spoken arithmetic: a small expression parser over normalised speech.
//
// Supports + - * / ^, parentheses, factorial, percent, and the libm functions
// sqrt, cbrt, sin, cos, tan, asin, acos, atan, log (natural), log10, exp,
// abs, floor, ceil, round, plus pi and e.  Trigonometry is in degrees unless
// the sentence says "radians".
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Evaluate `text`.  On success returns true and stores the value in *out.
// On failure returns false and puts a spoken explanation in msg.
bool math_eval(const char *text, double *out, char *msg, size_t msg_len);

// True when the sentence looks like arithmetic rather than conversation.
bool math_looks_like(const char *text);

// Evaluate and return a complete spoken reply ("That is 92.", "x equals ...").
bool math_say(const char *text, char *out, size_t out_len);

// Format a result for speech: integers plainly, big/small values as
// "X times ten to the power of N".
void math_format(double v, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
