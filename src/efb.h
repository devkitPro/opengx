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

#ifndef OPENGX_EFB_H
#define OPENGX_EFB_H

#include "fbo.h"

#include <GL/gl.h>
#include <malloc.h>
#include <ogc/gx.h>
#include <ogc/system.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OGX_EFB_NONE = 0,
    OGX_EFB_CLEAR = 1 << 0,
    OGX_EFB_COLOR = 1 << 1,
    OGX_EFB_ZBUFFER = 1 << 2,
} OgxEfbFlags;

extern OgxEfbContentType _ogx_efb_content_type;

bool _ogx_efb_set_pixel_format(uint8_t pixel_format);

void _ogx_efb_save_to_buffer(uint8_t format, uint16_t width, uint16_t height,
                             void *texels, OgxEfbFlags flags);
void _ogx_efb_save_area_to_buffer(uint8_t format,
                                  uint16_t x, uint16_t y,
                                  uint16_t width, uint16_t height,
                                  void *texels, OgxEfbFlags flags);
void _ogx_efb_restore_texobj(GXTexObj *texobj);

typedef struct {
    GXTexObj texobj;
    /* buffer-specific counter indicating what was the last draw operation
     * saved into this buffer */
    int draw_count;
    /* The texel data are stored in the same memory block at the end of this
     * struct */
    _Alignas(32) uint8_t texels[0];
} OgxEfbBuffer;

void _ogx_efb_buffer_prepare(OgxEfbBuffer **buffer, uint8_t format);
void _ogx_efb_buffer_save(OgxEfbBuffer *buffer, OgxEfbFlags flags);
static inline void *_ogx_efb_buffer_get_texels(OgxEfbBuffer *buffer) {
    void *texels = GX_GetTexObjData(&buffer->texobj);
    return texels ? MEM_PHYSICAL_TO_K0(texels) : NULL;
}

void _ogx_efb_set_content_type_real(OgxEfbContentType content_type);

/* We inline this part since most of the times the desired content type will be
 * the one already active */
static inline void _ogx_efb_set_content_type(OgxEfbContentType content_type) {
    if (content_type == _ogx_efb_content_type)
        return;
    _ogx_efb_set_content_type_real(content_type);
}

#ifdef __cplusplus
} // extern C
#endif

#endif /* OPENGX_EFB_H */
