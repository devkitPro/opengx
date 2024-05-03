/*****************************************************************************
Copyright (c) 2011  David Guillen Fandos (david@davidgf.net)
All rights reserved.

Attention! Contains pieces of code from others such as Mesa and GRRLib

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. Neither the name of copyright holders nor the names of its
   contributors may be used to endorse or promote products derived
   from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL COPYRIGHT HOLDERS OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

#ifndef OGX_UTILS_H
#define OGX_UTILS_H

#include <GL/gl.h>
#include <gccore.h>
#include <gctypes.h>
#include <math.h>
#include <string.h>

static inline float clampf_01(float n)
{
    if (n > 1.0f)
        return 1.0f;
    else if (n < 0.0f)
        return 0.0f;
    else
        return n;
}

static inline float clampf_11(float n)
{
    if (n > 1.0f)
        return 1.0f;
    else if (n < -1.0f)
        return -1.0f;
    else
        return n;
}

static inline void floatcpy(float *dest, const float *src, size_t count)
{
    memcpy(dest, src, count * sizeof(float));
}

static inline GXColor gxcol_new_fv(float *components)
{
    GXColor c = {
        components[0] * 255.0f,
        components[1] * 255.0f,
        components[2] * 255.0f,
        components[3] * 255.0f
    };
    return c;
}

static inline void gxcol_mulfv(GXColor *color, float *components)
{
    color->r *= components[0];
    color->g *= components[1];
    color->b *= components[2];
    color->a *= components[3];
}

static inline GXColor gxcol_cpy_mulfv(GXColor color, float *components)
{
    color.r *= components[0];
    color.g *= components[1];
    color.b *= components[2];
    color.a *= components[3];
    return color;
}

#endif /* OGX_UTILS_H */
