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

    if (tu->gen_mode != GL_SPHERE_MAP) return false;

    /* We need normal coordinates to be there */
    if (!glparamstate.cs.normal_enabled) return false;

    return true;
}

void _ogx_texture_gen_sw_sphere_map(int index, Tex2f out)
{
    guVector pos, normal;
    _ogx_array_reader_read_pos3f(&glparamstate.vertex_array,
                                 index, (float *)&pos);
    _ogx_array_reader_read_norm3f(&glparamstate.normal_array,
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
