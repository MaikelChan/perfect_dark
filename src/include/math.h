#ifndef IN_MATH_H
#define IN_MATH_H

#ifndef PLATFORM_N64
#include_next <math.h>
#undef M_PI
#undef M_TAU
// HACK: for some reason the #include_next above doesn't really do anything, so
float fabsf(float x);
float roundf(float x);
#endif

// @bug?
#define M_BADPI 3.141092641f
#define M_PI    3.141592741f

#define M_BADTAU (M_BADPI * 2)
#define M_TAU    (M_PI * 2)

#define BADDEG2RAD(deg) ((deg) * (M_BADPI / 180.0f))
#define DEG2RAD(deg)    ((deg) * (M_PI / 180.0f))

#define RAD2DEG(rad) ((rad) * (180.0f / M_PI))
#define RAD2DEG2(rad) ((rad) * 180.0f / M_PI)
#define BADRAD2DEG(rad) ((rad) * (180.0f / M_BADPI))

#endif
