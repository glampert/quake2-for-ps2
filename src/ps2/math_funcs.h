
/* ================================================================================================
 * -*- C -*-
 * File: math_funcs.h
 * Author: Guilherme R. Lampert
 * Created on: 13/10/15
 * Brief: Optimized math functions for the PS2 and replacements for the
 * C-library double precision ones that are not available on PlayStation 2.
 *
 * This source code is released under the GNU GPL v2 license.
 * Check the accompanying LICENSE file for details.
 * ================================================================================================ */

#ifndef PS2_MATH_FUNCS_H
#define PS2_MATH_FUNCS_H

/*
 * PS2 doesn't support double-precision, so all the
 * default math.h functions are not defined, instead
 * only the single-precision ones are (float, f suffix).
 * This hack fixes the problem in a quick-'n-dirty way.
 */

#include <math.h>

#define sin sinf
#define cos cosf

#define acos acosf
#define asin asinf

#define tan tanf
#define atan atanf
#define atan2 atan2f

#define sqrt sqrtf
#define floor floorf
#define ceil ceilf

#define fmod fmodf
float fmodf(float x, float y);

#endif // PS2_MATH_FUNCS_H
