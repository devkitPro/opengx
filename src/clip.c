/*****************************************************************************
Copyright (c) 2011  David Guillen Fandos (david@davidgf.net)
Copyright (c) 2024  Alberto Mardegan (mardy@users.sourceforge.net)
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

#include "clip.h"

#include "debug.h"
#include "gpu_resources.h"
#include "state.h"
#include "utils.h"

#include <GL/gl.h>
#include <malloc.h>

static GXTexObj s_clip_texture;
static uint8_t s_clip_texels[32] ATTRIBUTE_ALIGN(32) = {
    /* We only are about the top-left 2x2 corner, that is (given that pixels
     * are 4 bits wide) the first and the fourth byte only.
     * Note how the positive pixel value is set on the bottom right corner,
     * since in OpenGL the y coordinate grows upwards, but the t texture
     * coordinate grows downwards. */
    0x00, 0x00, 0x00, 0x00,
    0x0f, 0x00, 0x00, 0x00,
};

static void load_clip_texture(u8 tex_map)
{
    void *tex_data = GX_GetTexObjData(&s_clip_texture);
    if (!tex_data) {
        GX_InitTexObj(&s_clip_texture, s_clip_texels, 2, 2,
                      GX_TF_I4, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GX_InitTexObjLOD(&s_clip_texture, GX_NEAR, GX_NEAR,
                         0.0f, 0.0f, 0.0f, 0, 0, GX_ANISO_1);
        DCStoreRange(s_clip_texels, sizeof(s_clip_texels));
        GX_InvalidateTexAll();
    }

    GX_LoadTexObj(&s_clip_texture, tex_map);
}

static bool setup_tev(u8 tex_map, int plane_index0, int plane_index1)
{
    u8 stage = GX_TEVSTAGE0 + _ogx_gpu_resources->tevstage_first++;
    u8 tex_coord = GX_TEXCOORD0 + _ogx_gpu_resources->texcoord_first++;
    u8 tex_mtx = GX_TEXMTX0 + _ogx_gpu_resources->texmtx_first++ * 3;

    debug(OGX_LOG_CLIPPING, "%d TEV stages, %d tex_coords, %d tex_maps",
          stage, tex_coord, tex_map);

    /* Set a TEV stage that draws only where the clip texture is > 0 */
    GX_SetTevColorIn(stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
    GX_SetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    /* Set a logical operation: output = d + ((a OP b) ? c:0) */
    GX_SetTevAlphaIn(stage, GX_CA_TEXA, GX_CA_ZERO, GX_CA_APREV, GX_CA_ZERO);
    GX_SetTevAlphaOp(stage, GX_TEV_COMP_A8_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevOrder(stage, tex_coord, tex_map, GX_COLORNULL);

    /* Setup a texture coordinate transformation that applies the vertex
     * coordinates to the clip plane equation, therefore resulting in a texture
     * coordinate that is >= 0 if the clip equation is satisfied, and < 0 if
     * it's not. */
    Mtx m;
    Mtx planes;
    set_gx_mtx_rowv(0, planes, glparamstate.clip_planes[plane_index0]);
    if (plane_index1 >= 0) {
        set_gx_mtx_rowv(1, planes, glparamstate.clip_planes[plane_index1]);
    } else {
        /* Add an equation which is always satisfied (in theory, a plane with
         * all four coefficients set to zero is also always >=0, but with a 0
         * coordinate the TEV ends up sampling the wrong texel, since we are
         * just in the middle of two texels; returning a value strictly greater
         * than zero ensures that we end up in the right quadrant)  */
        set_gx_mtx_row(1, planes, 0.0f, 0.0f, 0.0f, 1.0f);
    }
    guMtxConcat(planes, glparamstate.modelview_matrix, m);
    /* Our texture has coordinates [0,1]x[0,1] and is made of four texels. The
     * centre of our texture is (0.5, 0.5), therefore we need to map the zero
     * point to that. We do that by translating the texture coordinates by 0.5.
     */
    guMtxTransApply(m, m, 0.5, 0.5, 0);
    GX_LoadTexMtxImm(m, tex_mtx, GX_MTX2x4);

    GX_SetTexCoordGen(tex_coord, GX_TG_MTX2x4, GX_TG_POS, tex_mtx);
    return true;
}

static void mtx44_multiply(const ClipPlane in, const Mtx44 m, ClipPlane out)
{
    for (int i = 0; i < 4; i++) {
        out[i] = in[0]*m[0][i] + in[1]*m[1][i] + in[2]*m[2][i] + in[3]*m[3][i];
    }
}

bool _ogx_clip_is_point_clipped(const guVector *p)
{
    if (glparamstate.clip_plane_mask == 0) return false;

    for (int i = 0; i < MAX_CLIP_PLANES; i++) {
        if (!(glparamstate.clip_plane_mask & (1 << i))) continue;

        guVector plane = {
            glparamstate.clip_planes[i][0],
            glparamstate.clip_planes[i][1],
            glparamstate.clip_planes[i][2],
        };
        if (guVecDotProduct(&plane, p) + glparamstate.clip_planes[i][3] < 0)
            return true;
    }
    return false;
}

void _ogx_clip_setup_tev()
{
    debug(OGX_LOG_CLIPPING, "setting up clip TEV");
    u8 tex_map = GX_TEXMAP0 + _ogx_gpu_resources->texmap_first++;
    load_clip_texture(tex_map);

    int plane_index0 = -1;
    for (int i = 0; i < MAX_CLIP_PLANES; i++) {
        if (!(glparamstate.clip_plane_mask & (1 << i))) continue;

        if (plane_index0 < 0) {
            plane_index0 = i;
        } else {
            /* We found two enabled planes, we can setup a TEV stage for them. */
            setup_tev(tex_map, plane_index0, i);
            plane_index0 = -1;
        }
    }

    if (plane_index0 >= 0) {
        /* We have an odd number of clip planes */
        setup_tev(tex_map, plane_index0, -1);
    }
}

void _ogx_clip_enabled(int plane)
{
    glparamstate.clip_plane_mask |= 1 << plane;
    glparamstate.dirty.bits.dirty_clip_planes = 1;
}

void _ogx_clip_disabled(int plane)
{
    glparamstate.stencil.enabled &= ~(1 << plane);
    glparamstate.dirty.bits.dirty_clip_planes = 1;
}

void glClipPlane(GLenum plane, const GLdouble *equation)
{
    if (plane < GL_CLIP_PLANE0 || plane - GL_CLIP_PLANE0 >= MAX_CLIP_PLANES) {
        set_error(GL_INVALID_ENUM);
        return;
    }

    ClipPlane *p = &glparamstate.clip_planes[plane - GL_CLIP_PLANE0];
    Mtx44 mv, mv_inverse;
    memcpy(mv, glparamstate.modelview_matrix, sizeof(Mtx));
    /* Fill in last row */
    mv[3][0] = mv[3][1] = mv[3][2] = 0.0f;
    mv[3][3] = 1.0f;
    /* TODO: cache the inverse matrix, since planes are likely to be specified
     * all at the same time */
    guMtx44Inverse(mv, mv_inverse);
    ClipPlane p0 = { equation[0], equation[1], equation[2], equation[3] };
    mtx44_multiply(p0, mv_inverse, *p);
}
