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

#include "efb.h"

#include "debug.h"
#include "state.h"
#include "utils.h"

#include <GL/gl.h>
#include <malloc.h>

OgxEfbContentType _ogx_efb_content_type = OGX_EFB_SCENE;

void _ogx_efb_save_area_to_buffer(uint8_t format,
                                  uint16_t x, uint16_t y,
                                  uint16_t width, uint16_t height,
                                  void *texels, OgxEfbFlags flags)
{
    GX_SetCopyFilter(GX_FALSE, NULL, GX_FALSE, NULL);
    GX_SetTexCopySrc(x,
                     y,
                     width,
                     height);
    GX_SetTexCopyDst(width, height, format, GX_FALSE);
    GX_CopyTex(texels, flags & OGX_EFB_CLEAR ? GX_TRUE : GX_FALSE);
    /* TODO: check if all of these sync functions are needed */
    GX_PixModeSync();
    GX_SetDrawDone();
    u32 size = GX_GetTexBufferSize(width, height, format, 0, GX_FALSE);
    DCInvalidateRange(texels, size);
    GX_WaitDrawDone();
}

void _ogx_efb_save_to_buffer(uint8_t format, uint16_t width, uint16_t height,
                             void *texels, OgxEfbFlags flags)
{
    _ogx_efb_save_area_to_buffer(format,
                                 glparamstate.viewport[0],
                                 glparamstate.viewport[1],
                                 width, height, texels, flags);
}

void _ogx_efb_restore_texobj(GXTexObj *texobj)
{
    _ogx_setup_2D_projection();
    u16 width = GX_GetTexObjWidth(texobj);
    u16 height = GX_GetTexObjHeight(texobj);
    GX_LoadTexObj(texobj, GX_TEXMAP0);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_U16, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_U8, 0);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetNumTexGens(1);
    GX_SetNumTevStages(1);
    GX_SetNumChans(0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
    glparamstate.dirty.bits.dirty_tev = 1;

    GX_SetCullMode(GX_CULL_NONE);
    glparamstate.dirty.bits.dirty_cull = 1;

    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    glparamstate.dirty.bits.dirty_z = 1;

    GX_SetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_COPY);
    glparamstate.dirty.bits.dirty_blend = 1;

    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
    glparamstate.dirty.bits.dirty_alphatest = 1;

    GX_SetColorUpdate(GX_TRUE);
    glparamstate.dirty.bits.dirty_color_update = 1;

    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2u16(0, 0);
    GX_TexCoord2u8(0, 0);
    GX_Position2u16(0, height);
    GX_TexCoord2u8(0, 1);
    GX_Position2u16(width, height);
    GX_TexCoord2u8(1, 1);
    GX_Position2u16(width, 0);
    GX_TexCoord2u8(1, 0);
    GX_End();
}

void _ogx_efb_buffer_prepare(OgxEfbBuffer **buffer, uint8_t format)
{
    if (*buffer) return;

    u16 width = glparamstate.viewport[2];
    u16 height = glparamstate.viewport[3];
    u32 size = GX_GetTexBufferSize(width, height, format, 0, GX_FALSE);
    OgxEfbBuffer *b = memalign(32, size + sizeof(OgxEfbBuffer));
    void *texels = &(b->texels[0]);
    DCInvalidateRange(texels, size);

    GX_InitTexObj(&b->texobj, texels, width, height, format,
                  GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&b->texobj, GX_NEAR, GX_NEAR,
                     0.0f, 0.0f, 0.0f, 0, 0, GX_ANISO_1);
    b->draw_count = 0;
    *buffer = b;
}

void _ogx_efb_buffer_handle_resize(OgxEfbBuffer **buffer)
{
    /* If no buffer was allocated, nothing to do */
    if (!*buffer) return;

    void *texels;
    u8 format, unused;
    u16 oldwidth, oldheight;
    GX_GetTexObjAll(&(*buffer)->texobj, &texels, &oldwidth, &oldheight, &format,
                    &unused, &unused, &unused);

    u16 width = glparamstate.viewport[2];
    u16 height = glparamstate.viewport[3];
    if (width != oldwidth || height != oldheight) {
        free(*buffer);
        *buffer = NULL;
        _ogx_efb_buffer_prepare(buffer, format);
    }
}

void _ogx_efb_buffer_save(OgxEfbBuffer *buffer, OgxEfbFlags flags)
{
    void *texels;
    u8 format, unused;
    u16 width, height;
    GX_GetTexObjAll(&buffer->texobj, &texels, &width, &height, &format,
                    &unused, &unused, &unused);
    texels = MEM_PHYSICAL_TO_K0(texels);
    _ogx_efb_save_to_buffer(format, width, height, texels, flags);
}
