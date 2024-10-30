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

#include "debug.h"
#include "state.h"
#include "utils.h"

#include <malloc.h>

typedef struct {
    size_t size;
    unsigned mapped : 1;
    /* The buffer data are stored in the same memory block at the end of this
     * struct */
    _Alignas(4) uint8_t data[0];
} VertexBuffer;

#define MAX_VBOS 256 /* Check the size of _ogx_state.bound_vbo_* members if
                        increasing this! */

static VertexBuffer *s_buffers[MAX_VBOS];

#define RESERVED_PTR ((void*)0x1)
#define VBO_IS_USED(vbo) \
    (s_buffers[vbo] != NULL && s_buffers[vbo] != RESERVED_PTR)
#define VBO_IS_RESERVED(vbo) (s_buffers[vbo] == RESERVED_PTR)
#define VBO_IS_RESERVED_OR_USED(vbo) (s_buffers[vbo] != NULL)
#define VBO_RESERVE(vbo) s_buffers[vbo] = RESERVED_PTR;

static VboType *get_buffer_for_target(GLenum target)
{
    VboType *buffer = NULL;
    switch (target) {
    case GL_ARRAY_BUFFER:
        buffer = &_ogx_state.bound_vbo_array;
        break;
    case GL_ELEMENT_ARRAY_BUFFER:
        buffer = &_ogx_state.bound_vbo_element_array;
        break;
    default:
        warning("Unsupported target for glBindBuffer: %04x", target);
        set_error(GL_INVALID_ENUM);
    }
    return buffer;
}

static int get_index_for_target(GLenum target)
{
    VboType *target_buffer = get_buffer_for_target(target);
    if (!target_buffer) return -1;

    VboType active_vbo = *target_buffer;
    if (active_vbo == 0) {
        set_error(GL_INVALID_OPERATION);
    }

    return active_vbo - 1;
}

void glBindBuffer(GLenum target, GLuint buffer)
{
    VboType *target_buffer = get_buffer_for_target(target);
    if (target_buffer) *target_buffer = buffer;
}

void glDeleteBuffers(GLsizei n, const GLuint *buffers)
{
    const GLuint *vbolist = buffers;
    GX_DrawDone();
    while (n-- > 0) {
        int i = *vbolist++ - 1;
        if (i >= 0 && i < MAX_VBOS && VBO_IS_USED(i)) {
            free(s_buffers[i]);
            s_buffers[i] = NULL;
        }
    }
}

void glGenBuffers(GLsizei n, GLuint *buffers)
{
    GLuint *vbolist = buffers;
    int reserved = 0;
    for (int i = 0; i < MAX_VBOS && reserved < n; i++) {
        if (!VBO_IS_RESERVED_OR_USED(i)) {
            VBO_RESERVE(i);
            *vbolist++ = i + 1;
            reserved++;
        }
    }

    if (reserved < n) {
        warning("Could not allocate %d buffers", n);
        set_error(GL_OUT_OF_MEMORY);
        /* Unreserve the elements that we reserved just now */
        for (int i = 0; i < reserved; i++) {
            s_buffers[buffers[i] - 1] = NULL;
        }
    }
}

GLboolean glIsBuffer(GLuint buffer)
{
    if (buffer == 0 || buffer > MAX_VBOS) {
        return false;
    }

    int index = buffer - 1;
    return VBO_IS_RESERVED_OR_USED(index);
}

static void set_buffer_data(GLenum target, GLintptr offset, GLsizeiptr size,
                            const void *data, bool must_allocate)
{
    int index = get_index_for_target(target);
    if (index < 0) return;

    if (size <= 0) {
        if (size < 0) set_error(GL_INVALID_VALUE);
        return;
    }

    VertexBuffer *buffer = s_buffers[index];
    if (must_allocate) {
        if (buffer && buffer != RESERVED_PTR) free(buffer);
        buffer = s_buffers[index] = malloc(sizeof(VertexBuffer) + size);
        if (!buffer) {
            warning("Out of memory allocating a VBO");
            set_error(GL_OUT_OF_MEMORY);
            return;
        }
        buffer->size = size;
        buffer->mapped = false;
    }

    if (!buffer) {
        set_error(GL_INVALID_VALUE);
        return;
    }
    if (data) {
        memcpy(buffer->data + offset, data, size);
        DCStoreRangeNoSync(buffer->data + offset, size);
    }
}

void glBufferData(GLenum target, GLsizeiptr size, const void *data,
                  GLenum usage)
{
    set_buffer_data(target, 0, size, data, true);
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                     const void *data)
{
    set_buffer_data(target, offset, size, data, false);
}

void glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, void *data)
{
    int index = get_index_for_target(target);
    if (index < 0) return;

    if (VBO_IS_USED(index)) {
        memcpy(data, s_buffers[index]->data + offset, size);
    } else {
        set_error(GL_INVALID_VALUE);
    }
}

void *glMapBuffer(GLenum target, GLenum access)
{
    int index = get_index_for_target(target);
    if (index < 0) return NULL;

    if (!VBO_IS_USED(index)) {
        set_error(GL_INVALID_VALUE);
        return NULL;
    }

    VertexBuffer *buffer = s_buffers[index];
    buffer->mapped = true;
    return buffer->data;
}

GLboolean glUnmapBuffer(GLenum target)
{
    int index = get_index_for_target(target);
    if (index < 0) return GL_FALSE;

    if (!VBO_IS_USED(index)) {
        set_error(GL_INVALID_VALUE);
        return GL_FALSE;
    }

    VertexBuffer *buffer = s_buffers[index];
    if (!buffer->mapped) {
        set_error(GL_INVALID_OPERATION);
        return GL_FALSE;
    }

    DCStoreRangeNoSync(buffer->data, buffer->size);
    return GL_TRUE;
}

void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params)
{
    int index = get_index_for_target(target);
    if (index < 0) return;

    switch (pname) {
    case GL_BUFFER_MAPPED:
        *params = s_buffers[index]->mapped;
        break;
    case GL_BUFFER_SIZE:
        *params = s_buffers[index]->size;
        break;
    default:
        warning("Unhandled buffer parameter %04x", pname);
    }
}

void glGetBufferPointerv(GLenum target, GLenum pname, void **params)
{
    if (pname != GL_BUFFER_MAP_POINTER) {
        set_error(GL_INVALID_ENUM);
        return;
    }

    int index = get_index_for_target(target);
    if (index < 0) return;

    if (!VBO_IS_USED(index)) {
        set_error(GL_INVALID_VALUE);
        return;
    }
    VertexBuffer *buffer = s_buffers[index];
    *params = buffer->mapped ? buffer->data : NULL;
}

void *_ogx_vbo_get_data(VboType vbo, const void *offset)
{
    return s_buffers[vbo - 1]->data + (int)offset;
}
