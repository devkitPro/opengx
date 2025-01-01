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

#define GL_GLEXT_PROTOTYPES
#define BUILDING_SHADER_CODE
#include "debug.h"
#include "shader.h"
#include "utils.h"

#include <GL/gl.h>
#include <GL/glext.h>

static OgxUniformData *get_program_uniform_data(GLuint program, GLint location)
{
    if (!program) {
        set_error(GL_INVALID_OPERATION);
        return NULL;
    }

    if (location == -1) return NULL;

    OgxProgram *p = PROGRAM_FROM_INT(program);
    if (location >= p->uniform_location_count) {
        set_error(GL_INVALID_OPERATION);
        return NULL;
    }
    size_t offset = p->uniform_location_offsets[location];
    void *data = (char*)p->uniform_data_base + offset;
    return data;
}

static inline OgxUniformData *get_uniform_data(GLint location)
{
    return get_program_uniform_data(glparamstate.current_program, location);
}

void glGetUniformfv(GLuint program, GLint location, GLfloat *params)
{
    OgxUniformData *data = get_program_uniform_data(program, location);
    if (!data) return;

    /* TODO: only works if the uniform is storing floats */
    memcpy(params, &data->data, _ogx_size_for_type(data->uniform->type));
}

void glGetUniformiv(GLuint program, GLint location, GLint *params)
{
    OgxUniformData *data = get_program_uniform_data(program, location);
    if (!data) return;

    switch (data->uniform->type) {
    case GL_INT_VEC4:
    case GL_UNSIGNED_INT_VEC4:
        params[3] = data->data.vec4i[3];
        // fall through
    case GL_INT_VEC3:
    case GL_UNSIGNED_INT_VEC3:
        params[2] = data->data.vec4i[2];
        // fall through
    case GL_INT_VEC2:
    case GL_UNSIGNED_INT_VEC2:
        params[1] = data->data.vec4i[1];
        // fall through
    case GL_INT:
    case GL_UNSIGNED_INT:
        params[0] = data->data.vec4i[0];
        break;
    case GL_BOOL_VEC4:
        params[3] = data->data.vec4b[3];
        // fall through
    case GL_BOOL_VEC3:
        params[2] = data->data.vec4b[2];
        // fall through
    case GL_BOOL_VEC2:
        params[1] = data->data.vec4b[1];
        // fall through
    case GL_BOOL:
        params[0] = data->data.vec4b[0];
        break;
    default:
        warning("glGetUniformiv unsupported type %04x", data->uniform->type);
    }
}

static void set_uniform_values(GLint location, GLsizei count,
                               const void *src, size_t value_size)
{
    for (int i = 0; i < count; i++) {
        OgxUniformData *data = get_uniform_data(location + i);
        memcpy(&data->data, src, value_size);
    }
}

static void set_uniform_matrices(GLint location, GLsizei count,
                                 GLboolean transpose,
                                 const float *src,
                                 char cols, char rows)
{
    float m[16];
    const float *matrix;
    size_t elements = rows * cols;
    for (int i = 0; i < count; i++) {
        OgxUniformData *data = get_uniform_data(location + i);
        if (transpose) {
            for (int r = 0; r < rows; r++)
                for (int c = 0; c < cols; c++) {
                m[c * rows + r] = src[r * cols + c];
            }
            matrix = m;
        } else {
            matrix = src;
        }
        floatcpy(&data->data.mat4[0], matrix, elements);
        src += elements;
    }
}

void glUniform1f(GLint location, GLfloat v0)
{
    set_uniform_values(location, 1, &v0, sizeof(v0));
}

void glUniform2f(GLint location, GLfloat v0, GLfloat v1)
{
    float value[2] = { v0, v1 };
    set_uniform_values(location, 1, value, sizeof(value));
}

void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2)
{
    float value[3] = { v0, v1, v2 };
    set_uniform_values(location, 1, value, sizeof(value));
}

void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
    float value[4] = { v0, v1, v2, v3 };
    set_uniform_values(location, 1, value, sizeof(value));
}

void glUniform1i(GLint location, GLint v0)
{
    set_uniform_values(location, 1, &v0, sizeof(v0));
}

void glUniform2i(GLint location, GLint v0, GLint v1)
{
    int value[2] = { v0, v1 };
    set_uniform_values(location, 1, value, sizeof(value));
}

void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2)
{
    int value[3] = { v0, v1, v2 };
    set_uniform_values(location, 1, value, sizeof(value));
}

void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3)
{
    int value[4] = { v0, v1, v2, v3 };
    set_uniform_values(location, 1, value, sizeof(value));
}

void glUniform1fv(GLint location, GLsizei count, const GLfloat *value)
{
    set_uniform_values(location, count, value, 1 * sizeof(*value));
}

void glUniform2fv(GLint location, GLsizei count, const GLfloat *value)
{
    set_uniform_values(location, count, value, 2 * sizeof(*value));
}

void glUniform3fv(GLint location, GLsizei count, const GLfloat *value)
{
    set_uniform_values(location, count, value, 3 * sizeof(*value));
}

void glUniform4fv(GLint location, GLsizei count, const GLfloat *value)
{
    set_uniform_values(location, count, value, 4 * sizeof(*value));
}

void glUniform1iv(GLint location, GLsizei count, const GLint *value)
{
    set_uniform_values(location, count, value, 1 * sizeof(*value));
}

void glUniform2iv(GLint location, GLsizei count, const GLint *value)
{
    set_uniform_values(location, count, value, 2 * sizeof(*value));
}

void glUniform3iv(GLint location, GLsizei count, const GLint *value)
{
    set_uniform_values(location, count, value, 3 * sizeof(*value));
}

void glUniform4iv(GLint location, GLsizei count, const GLint *value)
{
    set_uniform_values(location, count, value, 4 * sizeof(*value));
}

void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    set_uniform_matrices(location, count, transpose, value, 2, 2);
}

void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    set_uniform_matrices(location, count, transpose, value, 3, 3);
}

void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    set_uniform_matrices(location, count, transpose, value, 4, 4);
}
