/*****************************************************************************
Copyright (c) 2024  Alberto Mardegan (mardy@users.sourceforge.net)
All rights reserved.

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

#include "texture_gen_sw.h"

#include "debug.h"
#include "utils.h"

#include <ogc/gu.h>

bool _ogx_texture_gen_sw_enabled(uint8_t unit)
{
    const OgxTextureUnit *tu = &glparamstate.texture_unit[unit];
    OgxHints hint = OGX_HINT_NONE;
    bool needs_normals = false;

    switch (tu->gen_mode) {
    case GL_SPHERE_MAP:
        hint = OGX_HINT_FAST_SPHERE_MAP;
        needs_normals = true;
        break;
    case GL_REFLECTION_MAP:
        /* We don't support a standard-compiant generation of the reflection
         * map yet, because its output should consist of three float
         * components, whereas the TEV only supports two components for
         * GX_VA_TEX*. One way to implement it would be to (ab)use the
         * GX_VA_NBT format: storing the computed texture generated coordinates
         * into the binormal part of the array, and then use them in the TEV as
         * GX_TG_BINRM.
         * But this requires yet one more refactoring of the array classes, to
         * let the normals array switch between GX_VA_NRM and GX_VA_NBT
         * depending on whether GL_REFLECTION_MAP is enabled. Let's leave this
         * as a TODO.
         */
    default:
        return false;
    }

    /* If the client prefers the inaccurate GPU implementation, let it be */
    if (hint != OGX_HINT_NONE && (glparamstate.hints & hint)) return false;

    if (needs_normals && !glparamstate.cs.normal_enabled) return false;

    return true;
}

void _ogx_texture_gen_sw_sphere_map(int index, Tex2f out)
{
    guVector pos, normal;
    _ogx_array_reader_read_pos3f(_ogx_array_reader_for_attribute(GX_VA_POS),
                                 index, (float *)&pos);
    _ogx_array_reader_read_norm3f(_ogx_array_reader_for_attribute(GX_VA_NRM),
                                  index, (float *)&normal);
    /* Transform coordinates to eye-space */
    guVecMultiply(glparamstate.modelview_matrix, &pos, &pos);
    /* For the normal, only use the scale + rotation part of the matrix */
    guVecMultiplySR(glparamstate.modelview_matrix, &normal, &normal);
    guVecNormalize(&pos);

    /* The pos vector now can be used to represent the vector from the eye
     * to the vertex. Reflect it across the normal to find the reflection
     * vector. */
    float pos_dot_normal = guVecDotProduct(&pos, &normal);

    guVector reflection;
    guVecScale(&normal, &normal, -pos_dot_normal * 2);
    guVecAdd(&pos, &normal, &reflection);
    float rx = reflection.x, ry = reflection.y, rz = reflection.z;
    float m = sqrtf(rx * rx + ry * ry + (rz + 1) * (rz + 1)) * 2;
    out[0] = rx / m + 0.5f;
    out[1] = ry / m + 0.5f;
}
