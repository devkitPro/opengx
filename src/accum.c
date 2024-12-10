/*****************************************************************************
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

#include "opengx.h"

#include "accum.h"
#include "debug.h"
#include "efb.h"
#include "state.h"
#include "utils.h"

#include <GL/gl.h>
#include <malloc.h>

static OgxEfbBuffer *s_accum_buffer = NULL;

static void draw_screen(GXTexObj *texture, float value)
{
    _ogx_setup_2D_projection();

    u16 width = glparamstate.viewport[2];
    u16 height = glparamstate.viewport[3];

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_U16, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_U8, 0);
    if (texture) {
        GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
        GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        GX_SetNumTexGens(1);
        GX_LoadTexObj(texture, GX_TEXMAP0);
    } else {
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL,
                       GX_TEXMAP_NULL, GX_COLOR0A0);
        GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
        GX_SetNumTexGens(0);
    }
    GX_SetNumTevStages(1);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX,
                   0, GX_DF_NONE, GX_AF_NONE);
    glparamstate.dirty.bits.dirty_tev = 1;

    GX_SetCullMode(GX_CULL_NONE);
    glparamstate.dirty.bits.dirty_cull = 1;

    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    glparamstate.dirty.bits.dirty_z = 1;

    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
    glparamstate.dirty.bits.dirty_alphatest = 1;

    GX_SetColorUpdate(GX_TRUE);
    glparamstate.dirty.bits.dirty_color_update = 1;

    u8 intensity = (u8)(value * 255.0f);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2u16(0, 0);
    GX_Color4u8(intensity, intensity, intensity, intensity);
    GX_TexCoord2u8(0, 0);
    GX_Position2u16(0, height);
    GX_Color4u8(intensity, intensity, intensity, intensity);
    GX_TexCoord2u8(0, 1);
    GX_Position2u16(width, height);
    GX_Color4u8(intensity, intensity, intensity, intensity);
    GX_TexCoord2u8(1, 1);
    GX_Position2u16(width, 0);
    GX_Color4u8(intensity, intensity, intensity, intensity);
    GX_TexCoord2u8(1, 0);
    GX_End();
}

static OgxEfbBuffer *save_scene_into_texture()
{
    OgxEfbBuffer *scene = NULL;

    /* Since the accumulation buffer typically combines several frames, we
     * don't need to capture the current scene with maximum precision; a 16-bit
     * format should be enough */
    _ogx_efb_buffer_prepare(&scene, GX_TF_RGB565);
    _ogx_efb_buffer_save(scene, OGX_EFB_COLOR);
    return scene;
}

void _ogx_accum_clear()
{
    if (!s_accum_buffer) return;

    void *texels;
    u8 format, unused;
    u16 width, height;
    GX_GetTexObjAll(&s_accum_buffer->texobj, &texels, &width, &height, &format,
                    &unused, &unused, &unused);
    texels = MEM_PHYSICAL_TO_K0(texels);
    u32 size = GX_GetTexBufferSize(width, height, format, 0, GX_FALSE);
    int n_blocks = size / 32;
    GXColor c = glparamstate.accum_clear_color;
    uint16_t pixcolor_ar = c.a << 8 | c.r;
    uint16_t pixcolor_gb = c.g << 8 | c.b;
    uint16_t *dst = texels;
    for (int i = 0; i < n_blocks; i++) {
        /* In RGBA8 format, the EFB alternates AR blocks with GB blocks */
        uint16_t pixcolor = (i % 2 == 0) ? pixcolor_ar : pixcolor_gb;
        for (int j = 0; j < 16; j++) {
            *dst++ = pixcolor;
        }
    }
    DCStoreRangeNoSync(texels, size);
}

void _ogx_accum_load_into_efb()
{
    GX_InvalidateTexAll();
    _ogx_efb_restore_texobj(&s_accum_buffer->texobj);
}

void _ogx_accum_save_to_efb()
{
    GX_DrawDone();
    _ogx_efb_buffer_save(s_accum_buffer, OGX_EFB_COLOR);
}

void glClearAccum(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    glparamstate.accum_clear_color.r = clampf_01(red) * 255.0f;
    glparamstate.accum_clear_color.g = clampf_01(green) * 255.0f;
    glparamstate.accum_clear_color.b = clampf_01(blue) * 255.0f;
    glparamstate.accum_clear_color.a = clampf_01(alpha) * 255.0f;
}

void glAccum(GLenum op, GLfloat value)
{
    OgxEfbBuffer *scene_buffer = NULL;
    GXTexObj *texture = NULL;
    bool must_draw = false;

    _ogx_efb_buffer_prepare(&s_accum_buffer, GX_TF_RGBA8);
    if (op == GL_ACCUM || op == GL_LOAD) {
        scene_buffer = save_scene_into_texture();
        texture = &scene_buffer->texobj;
    }

    _ogx_efb_set_content_type(OGX_EFB_ACCUM);
    if (op == GL_ACCUM || op == GL_LOAD) {
        if (op == GL_ACCUM) {
            GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_COPY);
        } else {
            GX_SetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_COPY);
        }
        must_draw = true;
    } else if (op == GL_ADD) {
        GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_COPY);
        must_draw = true;
    } else if (op == GL_MULT) {
        GX_SetBlendMode(GX_BM_BLEND, GX_BL_ZERO, GX_BL_SRCALPHA, GX_LO_COPY);
        must_draw = true;
    }

    if (must_draw) {
        draw_screen(texture, value);
        glparamstate.dirty.bits.dirty_blend = 1;
    }

    if (op == GL_RETURN && value == 1.0f) {
        /* Just leave the accumulation buffer on the scene, since it doesn't
         * leave to be altered */
        _ogx_efb_content_type = OGX_EFB_SCENE;
    } else {
        /* We must render the contents of the accumulation buffer with an
         * intensity given by "value". */
        _ogx_efb_set_content_type(OGX_EFB_SCENE);

        GX_SetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_COPY);
        glparamstate.dirty.bits.dirty_blend = 1;
        draw_screen(&s_accum_buffer->texobj, value);
    }

    if (scene_buffer) {
        free(scene_buffer);
    }
}
