/*****************************************************************************
Copyright (c) 2025  Alberto Mardegan (mardy@users.sourceforge.net)
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

#ifndef OPENGX_FBO_H
#define OPENGX_FBO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "state.h"

#include <GL/gl.h>
#include <stdbool.h>
#include <stdint.h>

/* The maximum number of color attachments to a framebuffer object. The
 * standard says that this should be at least 8, therefore allowing a fragment
 * shader to output at least 8 variables (until OpenGL 3.0 a fragment shader
 * could output only one, gl_FragColor). Since GX has only one framebuffer, it
 * means that we need to render the geometry as many times as the number of
 * color attachments.
 * Let's leave that as a TODO and for the time being support only one color
 * attachment. */
#define MAX_COLOR_ATTACHMENTS 1

/* We can increase this as needed, but we must remember to switch the FboType
 * from uint8_t to uint16_t if this gets bigger than 255 */
#define MAX_FRAMEBUFFERS 254

typedef uint8_t FboType;

typedef enum {
    ATTACHMENT_COLOR0 = 0,
    ATTACHMENT_DEPTH = ATTACHMENT_COLOR0 + MAX_COLOR_ATTACHMENTS,
    ATTACHMENT_STENCIL,
    NUM_ATTACHMENTS
} AttachmentIndex;

typedef struct _OgxFramebuffer OgxFramebuffer;

typedef struct {
    FboType draw_target;
    FboType read_target;
    union {
        struct {
            /* These are set if a different framebuffer got bound, or if the
             * attachments changed on the active framebuffer */
            unsigned draw_target : 1;
            unsigned read_target : 1;
        } bits;
        uint32_t all;
    } dirty;
} OgxFboState;

extern OgxFboState _ogx_fbo_state;

bool _ogx_fbo_get_integerv(GLenum pname, GLint *params);
void _ogx_fbo_scene_save_from_efb(OgxEfbContentType next_content_type);
void _ogx_fbo_scene_load_into_efb(void);

#ifdef BUILDING_FBO_CODE

extern OgxFunctions _ogx_fbo_functions;

#else /* BUILDING_FBO_CODE not defined */

OgxFboState _ogx_fbo_state __attribute__((weak)) = { 0, 0 };

bool __attribute__((weak)) _ogx_fbo_get_integerv(GLenum pname, GLint *params)
{
    return false;
}

void __attribute__((weak)) _ogx_fbo_scene_save_from_efb(OgxEfbContentType next_content_type)
{
    _ogx_scene_save_from_efb();
}

void __attribute__((weak)) _ogx_fbo_scene_load_into_efb()
{
    _ogx_scene_load_into_efb();
}

OgxFunctions _ogx_fbo_functions __attribute__((weak)) = { 0, NULL };

#endif /* BUILDING_FBO_CODE */

#ifdef __cplusplus
} // extern C
#endif

#endif /* OPENGX_FBO_H */
