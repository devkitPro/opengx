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

#include "debug.h"
#include "utils.h"
#include "state.h"

#include <GL/gl.h>
#include <string.h>

static const GLubyte gl_null_string[1] = { 0 };

GLenum glGetError(void)
{
    GLenum error = glparamstate.error;
    glparamstate.error = GL_NO_ERROR;
    return error;
}

const GLubyte *glGetString(GLenum name)
{
    switch (name) {
    case GL_VENDOR:
        return "opengx";
    case GL_RENDERER:
        return "libogc";
    case GL_VERSION:
        return "1.1";
    case GL_EXTENSIONS:
        return gl_null_string;
    default:
        set_error(GL_INVALID_ENUM);
        return gl_null_string;
    }
}

void glGetDoublev(GLenum pname, GLdouble *params)
{
    float paramsf[16];
    int n = 1;

    glGetFloatv(pname, paramsf);
    switch (pname) {
    case GL_MODELVIEW_MATRIX:
    case GL_PROJECTION_MATRIX:
        n = 16; break;
    };
    for (int i = 0; i < n; i++) {
        params[i] = paramsf[i];
    }
}

void glGetFloatv(GLenum pname, GLfloat *params)
{
    switch (pname) {
    case GL_MODELVIEW_MATRIX:
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 4; j++)
                params[j * 4 + i] = glparamstate.modelview_matrix[i][j];
        params[3] = params[7] = params[11] = 0.0f;
        params[15] = 1.0f;
        return;
    case GL_PROJECTION_MATRIX:
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                params[j * 4 + i] = glparamstate.modelview_matrix[i][j];
        return;
    default:
        return;
    };
}

// XXX: Need to finish glGets, important!!!
void glGetIntegerv(GLenum pname, GLint *params)
{
    switch (pname) {
    case GL_CLIP_PLANE0:
    case GL_CLIP_PLANE1:
    case GL_CLIP_PLANE2:
    case GL_CLIP_PLANE3:
    case GL_CLIP_PLANE4:
    case GL_CLIP_PLANE5:
        *params =
            glparamstate.clip_plane_mask & (1 << (pname - GL_CLIP_PLANE0));
        return;
    case GL_MAX_CLIP_PLANES:
        *params = MAX_CLIP_PLANES;
        return;
    case GL_MAX_TEXTURE_SIZE:
        *params = 1024;
        return;
    case GL_MODELVIEW_STACK_DEPTH:
        *params = MAX_MODV_STACK;
        return;
    case GL_PROJECTION_STACK_DEPTH:
        *params = MAX_PROJ_STACK;
        return;
    case GL_MAX_NAME_STACK_DEPTH:
        *params = MAX_NAME_STACK_DEPTH;
        return;
    case GL_NAME_STACK_DEPTH:
        *params = glparamstate.name_stack_depth;
        return;
    case GL_PACK_SWAP_BYTES:
        *params = glparamstate.pack_swap_bytes;
        break;
    case GL_PACK_LSB_FIRST:
        *params = glparamstate.pack_lsb_first;
        break;
    case GL_PACK_ROW_LENGTH:
        *params = glparamstate.pack_row_length;
        break;
    case GL_PACK_IMAGE_HEIGHT:
        *params = glparamstate.pack_image_height;
        break;
    case GL_PACK_SKIP_ROWS:
        *params = glparamstate.pack_skip_rows;
        break;
    case GL_PACK_SKIP_PIXELS:
        *params = glparamstate.pack_skip_pixels;
        break;
    case GL_PACK_SKIP_IMAGES:
        *params = glparamstate.pack_skip_images;
        break;
    case GL_PACK_ALIGNMENT:
        *params = glparamstate.pack_alignment;
        break;
    case GL_UNPACK_SWAP_BYTES:
        *params = glparamstate.unpack_swap_bytes;
        break;
    case GL_UNPACK_LSB_FIRST:
        *params = glparamstate.unpack_lsb_first;
        break;
    case GL_UNPACK_ROW_LENGTH:
        *params = glparamstate.unpack_row_length;
        break;
    case GL_UNPACK_IMAGE_HEIGHT:
        *params = glparamstate.unpack_image_height;
        break;
    case GL_UNPACK_SKIP_ROWS:
        *params = glparamstate.unpack_skip_rows;
        break;
    case GL_UNPACK_SKIP_PIXELS:
        *params = glparamstate.unpack_skip_pixels;
        break;
    case GL_UNPACK_SKIP_IMAGES:
        *params = glparamstate.unpack_skip_images;
        break;
    case GL_UNPACK_ALIGNMENT:
        *params = glparamstate.unpack_alignment;
        break;
    case GL_VIEWPORT:
        memcpy(params, glparamstate.viewport, 4 * sizeof(int));
        return;
    case GL_RENDER_MODE:
        *params = glparamstate.render_mode;
        return;
    default:
        return;
    };
}

