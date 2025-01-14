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

#ifndef OGX_UTILS_H
#define OGX_UTILS_H

#include "state.h"

#include <gctypes.h>
#include <limits.h>
#include <math.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline float clampf_01(float n)
{
    if (n > 1.0f)
        return 1.0f;
    else if (n < 0.0f)
        return 0.0f;
    else
        return n;
}

static inline float clampf_11(float n)
{
    if (n > 1.0f)
        return 1.0f;
    else if (n < -1.0f)
        return -1.0f;
    else
        return n;
}

static inline float scaled_int(int v)
{
    return ((float)v) / INT_MAX;
}

static inline void floatcpy(float *dest, const float *src, size_t count)
{
    memcpy(dest, src, count * sizeof(float));
}

static inline void normalize(GLfloat v[3])
{
    GLfloat r;
    r = (GLfloat)sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (r == 0.0f)
        return;

    v[0] /= r;
    v[1] /= r;
    v[2] /= r;
}

static inline void cross(GLfloat v1[3], GLfloat v2[3], GLfloat result[3])
{
    result[0] = v1[1] * v2[2] - v1[2] * v2[1];
    result[1] = v1[2] * v2[0] - v1[0] * v2[2];
    result[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

// We have to reverse the product, as we store the matrices as colmajor (otherwise said trasposed)
// we need to compute C = A*B (as rowmajor, "regular") then C = B^T * A^T = (A*B)^T
// so we compute the product and transpose the result (for storage) in one go.
//                                        Reversed operands a and b
static inline void gl_matrix_multiply(float *dst, float *b, float *a)
{
    dst[0] = a[0] * b[0] + a[1] * b[4] + a[2] * b[8] + a[3] * b[12];
    dst[1] = a[0] * b[1] + a[1] * b[5] + a[2] * b[9] + a[3] * b[13];
    dst[2] = a[0] * b[2] + a[1] * b[6] + a[2] * b[10] + a[3] * b[14];
    dst[3] = a[0] * b[3] + a[1] * b[7] + a[2] * b[11] + a[3] * b[15];
    dst[4] = a[4] * b[0] + a[5] * b[4] + a[6] * b[8] + a[7] * b[12];
    dst[5] = a[4] * b[1] + a[5] * b[5] + a[6] * b[9] + a[7] * b[13];
    dst[6] = a[4] * b[2] + a[5] * b[6] + a[6] * b[10] + a[7] * b[14];
    dst[7] = a[4] * b[3] + a[5] * b[7] + a[6] * b[11] + a[7] * b[15];
    dst[8] = a[8] * b[0] + a[9] * b[4] + a[10] * b[8] + a[11] * b[12];
    dst[9] = a[8] * b[1] + a[9] * b[5] + a[10] * b[9] + a[11] * b[13];
    dst[10] = a[8] * b[2] + a[9] * b[6] + a[10] * b[10] + a[11] * b[14];
    dst[11] = a[8] * b[3] + a[9] * b[7] + a[10] * b[11] + a[11] * b[15];
    dst[12] = a[12] * b[0] + a[13] * b[4] + a[14] * b[8] + a[15] * b[12];
    dst[13] = a[12] * b[1] + a[13] * b[5] + a[14] * b[9] + a[15] * b[13];
    dst[14] = a[12] * b[2] + a[13] * b[6] + a[14] * b[10] + a[15] * b[14];
    dst[15] = a[12] * b[3] + a[13] * b[7] + a[14] * b[11] + a[15] * b[15];
}

static inline void mtx44project(const Mtx44 p, const guVector *v,
                                guVector *out)
{
    /* We skip all the matrix elements that we know are unused in a projection
     * matrix */
    out->x = p[0][0] * v->x + p[0][2] * v->z + p[0][3];
    out->y = p[1][1] * v->y + p[1][2] * v->z + p[1][3];
    out->z = p[2][2] * v->z + p[2][3];
    if (p[3][2] != 0) {
        out->x /= -v->z;
        out->y /= -v->z;
        out->z /= -v->z;
    }
}

static inline OgxTextureUnit *active_tex_unit()
{
    int unit = glparamstate.active_texture;
    return &glparamstate.texture_unit[unit];
}

static inline Mtx *current_tex_matrix()
{
    OgxTextureUnit *tu = active_tex_unit();
    return &tu->matrix[tu->matrix_index];
}

static inline bool gxcol_equal(GXColor a, GXColor b)
{
    return *(int32_t*)&a == *(int32_t*)&b;
}

static inline GXColor gxcol_new_fv(const float *components)
{
    GXColor c = {
        (u8)(components[0] * 255.0f),
        (u8)(components[1] * 255.0f),
        (u8)(components[2] * 255.0f),
        (u8)(components[3] * 255.0f)
    };
    return c;
}

static inline void gxcol_mulfv(GXColor *color, const float *components)
{
    color->r *= components[0];
    color->g *= components[1];
    color->b *= components[2];
    color->a *= components[3];
}

static inline GXColor gxcol_cpy_mulfv(GXColor color, const float *components)
{
    color.r *= components[0];
    color.g *= components[1];
    color.b *= components[2];
    color.a *= components[3];
    return color;
}

static inline void set_error(GLenum code)
{
    /* OpenGL mandates that the oldest unretrieved error must be preserved. */
    if (!glparamstate.error) {
        glparamstate.error = code;
    }
}

extern uint16_t _ogx_draw_sync_token;
static inline uint16_t send_draw_sync_token()
{
    uint16_t token = ++_ogx_draw_sync_token;
    GX_SetDrawSync(token);
    return token;
}

static inline size_t sizeof_gl_type(GLenum type)
{
    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        return 1;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
        return 2;
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_FLOAT:
        return 4;
    case GL_DOUBLE:
        return 8;
    default:
        return 0;
    }
}

typedef void (*ForeachCb)(GLuint value);


static inline void foreach_u8(GLsizei n, const GLbyte *data, ForeachCb cb)
{
    for (int i = 0; i < n; i++)
        cb(data[i]);
}

static inline void foreach_u16(GLsizei n, const GLshort *data, ForeachCb cb)
{
    for (int i = 0; i < n; i++)
        cb(data[i]);
}

static inline void foreach_u32(GLsizei n, const GLuint *data, ForeachCb cb)
{
    for (int i = 0; i < n; i++)
        cb(data[i]);
}

static inline void foreach_float(GLsizei n, const GLfloat *data, ForeachCb cb)
{
    for (int i = 0; i < n; i++)
        cb((GLuint)data[i]);
}

static inline void foreach(GLsizei n, GLenum type, const GLvoid *data,
                           ForeachCb cb)
{
    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        foreach_u8(n, (const GLbyte *)data, cb);
        break;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
        foreach_u16(n, (const GLshort *)data, cb);
        break;
    case GL_INT:
    case GL_UNSIGNED_INT:
        foreach_u32(n, (const GLuint *)data, cb);
        break;
    case GL_FLOAT:
        foreach_float(n, (const GLfloat *)data, cb);
        break;
    }
}

static inline int read_index(const GLvoid *indices, GLenum type, int i)
{
    switch (type) {
    case GL_UNSIGNED_BYTE:
        return ((uint8_t*)indices)[i];
    case GL_UNSIGNED_SHORT:
        return ((uint16_t*)indices)[i];
    case GL_UNSIGNED_INT:
        return ((uint32_t*)indices)[i];
    }
}

static inline void set_gx_mtx_rowv(int row, Mtx m, const float *values)
{
    m[row][0] = values[0];
    m[row][1] = values[1];
    m[row][2] = values[2];
    m[row][3] = values[3];
}

static inline void set_gx_mtx_row(int row, Mtx m,
                                  float c0, float c1, float c2, float c3)
{
    m[row][0] = c0;
    m[row][1] = c1;
    m[row][2] = c2;
    m[row][3] = c3;
}

static inline void gl_matrix_to_gx(const GLfloat *source, Mtx mv)
{
    float w = source[15];
    if (w != 1.0 && w != 0.0) {
        for (int i = 0; i < 16; i++) {
            if (i % 4 == 3) continue;
            mv[i%4][i/4] = source[i] / w;
        }
    } else {
        for (int i = 0; i < 16; i++) {
            if (i % 4 == 3) continue;
            mv[i%4][i/4] = source[i];
        }
    }
}

static inline void gl_matrix_to_gx44(const GLfloat *source, Mtx44 mv)
{
    for (int i = 0; i < 16; i++) {
        mv[i%4][i/4] = source[i];
    }
}

static inline uint8_t gx_compare_from_gl(GLenum func)
{
    switch (func) {
    case GL_NEVER: return GX_NEVER;
    case GL_LESS: return GX_LESS;
    case GL_EQUAL: return GX_EQUAL;
    case GL_LEQUAL: return GX_LEQUAL;
    case GL_GREATER: return GX_GREATER;
    case GL_NOTEQUAL: return GX_NEQUAL;
    case GL_GEQUAL: return GX_GEQUAL;
    case GL_ALWAYS: return GX_ALWAYS;
    default: return 0xff;
    }
}

static inline GLenum gl_compare_from_gx(uint8_t func)
{
    switch (func) {
    case GX_NEVER: return GL_NEVER;
    case GX_LESS: return GL_LESS;
    case GX_EQUAL: return GL_EQUAL;
    case GX_LEQUAL: return GL_LEQUAL;
    case GX_GREATER: return GL_GREATER;
    case GX_NEQUAL: return GL_NOTEQUAL;
    case GX_GEQUAL: return GL_GEQUAL;
    case GX_ALWAYS: return GL_ALWAYS;
    default: return GL_NEVER;
    }
}

OgxDrawMode _ogx_draw_mode(GLenum mode);

/* Set up the matrices for 2D pixel-perfect drawing */
void _ogx_setup_2D_projection(void);

/* Set the OpenGL 3D modelview and projection matrices */
void _ogx_setup_3D_projection(void);

bool _ogx_setup_render_stages(void);
void _ogx_update_vertex_array_readers(OgxDrawMode mode);

#ifdef __cplusplus
} // extern C
#endif

#endif /* OGX_UTILS_H */
