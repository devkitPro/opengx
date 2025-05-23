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

#ifndef OPENGX_TYPES_H
#define OPENGX_TYPES_H

#include <GL/gl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OGX_EFB_SCENE = 1,
    OGX_EFB_STENCIL,
    OGX_EFB_ACCUM,
} OgxEfbContentType;

#define MAX_VERTEX_ATTRIBS 16

/* Now we support as much as 255 VBOs, but, should we support more, we'll need
 * to use a larger type for the index. */
typedef uint8_t VboType;

typedef uint8_t FboType;

typedef float Pos3f[3];
typedef float Norm3f[3];
typedef float Tex2f[2];
typedef float Vec4f[4];

typedef struct _OgxVertexAttribArray {
    unsigned normalized : 1;
    unsigned size : 3; /* max is 4 */
    uint8_t stride;
    /* This could be stored in a union with the "pointer" field, since 24 or 16
     * bits are enough for the offset. TODO: evaluate if it's worth doing. */
    VboType vbo;
    GLenum type;
    const void *pointer;
} OgxVertexAttribArray;

typedef struct {
    const char *name;
    void *address;
} OgxProcMap;

typedef struct {
    size_t num_functions;
    const OgxProcMap *functions;
} OgxFunctions;

typedef struct _OgxProgram OgxProgram;
typedef struct _OgxShader OgxShader;
typedef struct _OgxDrawData OgxDrawData;
typedef struct _OgxDrawMode OgxDrawMode;

#ifdef __cplusplus
} // extern C
#endif

#endif /* OPENGX_TYPES_H */
