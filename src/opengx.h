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

#ifndef OPENGX_H
#define OPENGX_H

#include <GL/gl.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ogx_initialize(void);
void *ogx_get_proc_address(const char *proc);

/* Enable or disable double buffering. This is actually a choice entirely made
 * by the integration library, but opengx must be informed about it in order to
 * select the right value for the glReadBuffer() and glDrawBuffer() functions.
 *
 * If this is not called, opengx assumes double buffering is on.
 */
int ogx_enable_double_buffering(int double_buffering);

/* The display integration library (SDL, GLUT, etc.) should call this function
 * before copying the EFB to the XFB (and optionally, drawing a cursor). The
 * opengx library might need to restore the EFB (in case it was configured into
 * some special rendering mode); if this function returns a negative number,
 * then the integration library should not proceed with the swap buffers
 * operation. This will typically happen if the GL rendering mode has been set
 * to something other than GL_RENDER.
 */
int ogx_prepare_swap_buffers(void);

/* This function can be called to register an optimized converter for the
 * texture data (used in glTex*Image* functions).
 *
 * The "format" and "internal_format" parameter corresponds to the homonymous
 * parameters passed to the glTexImage2D() function, and the "converter"
 * parameter must be one of the ogx_fast_conv_* variables declared below.
 *
 * The following fast converters are already enabled by default:
 * - ogx_fast_conv_RGB_RGB565;
 * - ogx_fast_conv_RGBA_RGBA8;
 * - ogx_fast_conv_Intensity_I8;
 */
void ogx_register_tex_conversion(GLenum format, GLenum internal_format,
                                 uintptr_t converter);

extern uintptr_t ogx_fast_conv_RGBA_I8;
extern uintptr_t ogx_fast_conv_RGBA_A8;
extern uintptr_t ogx_fast_conv_RGBA_IA8;
extern uintptr_t ogx_fast_conv_RGBA_RGB565;
extern uintptr_t ogx_fast_conv_RGBA_RGBA8;
extern uintptr_t ogx_fast_conv_RGB_I8;
extern uintptr_t ogx_fast_conv_RGB_IA8;
extern uintptr_t ogx_fast_conv_RGB_RGB565;
extern uintptr_t ogx_fast_conv_RGB_RGBA8;
extern uintptr_t ogx_fast_conv_LA_I8;
extern uintptr_t ogx_fast_conv_LA_A8;
extern uintptr_t ogx_fast_conv_LA_IA8;
extern uintptr_t ogx_fast_conv_Intensity_I8;
extern uintptr_t ogx_fast_conv_Alpha_A8;

typedef enum {
    OGX_STENCIL_NONE = 0,
    /* Don't worry about Z buffer being updated even if a fragment fails the
     * stencil test. */
    OGX_STENCIL_DIRTY_Z = 1 << 0,
    OGX_STENCIL_8BIT = 1 << 1,
} OgxStencilFlags;

void ogx_stencil_create(OgxStencilFlags flags);

typedef struct _OgxDrawMode {
    uint8_t mode;
    bool loop;
} OgxDrawMode;

typedef struct _OgxDrawData {
    OgxDrawMode gxmode;
    GLsizei count;

    /* for drawing arrays: */
    GLint first;

    /* for drawing elements: */
    GLenum type;
    const GLvoid *indices;
} OgxDrawData;

#ifdef __cplusplus
} // extern C
#endif

#endif /* OPENGX_H */
