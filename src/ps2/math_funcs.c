
/* ================================================================================================
 * -*- C -*-
 * File: math_funcs.c
 * Author: Guilherme R. Lampert
 * Created on: 13/10/15
 * Brief: Optimized math functions for the PS2 and replacements for the
 * C-library double precision ones that are not available on PlayStation 2.
 *
 * This source code is released under the GNU GPL v2 license.
 * Check the accompanying LICENSE file for details.
 * ================================================================================================ */

#include "ps2/math_funcs.h"

/*
=================
fmodf for the PS2
=================
*/
float fmodf(float x, float y)
{
    /*
	 * Portable fmod(x,y) implementation for systems
	 * that don't have it. Adapted from code found here:
	 *  http://www.opensource.apple.com/source/python/python-3/python/Python/fmod.c
	 */
    float i, f;

    if (y == 0.0f)
    {
        return 0.0f;
    }

    i = floorf(x / y);
    f = x - i * y;

    if ((x < 0.0f) != (y < 0.0f))
    {
        f = f - y;
    }

    return f;
}
