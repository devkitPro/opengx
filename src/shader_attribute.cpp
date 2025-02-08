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
#include <limits>
#include <type_traits>

template <typename T>
static void set_attribute(GLuint index, T v0, T v1 = 0, T v2 = 0, T v3 = 1)
{
    _ogx_shader_state.vertex_attrib_data[index][0] = v0;
    _ogx_shader_state.vertex_attrib_data[index][1] = v1;
    _ogx_shader_state.vertex_attrib_data[index][2] = v2;
    _ogx_shader_state.vertex_attrib_data[index][3] = v3;
}

template <char N, typename T>
static void set_attribute(GLuint index, const T *v)
{
    if constexpr (N == 1)
        set_attribute(index, v[0]);
    else if constexpr (N == 2)
        set_attribute(index, v[0], v[1]);
    else if constexpr (N == 3)
        set_attribute(index, v[0], v[1], v[2]);
    else
        set_attribute(index, v[0], v[1], v[2], v[3]);
}


template <typename T>
static inline float normalize(T v) {
    if constexpr (std::is_signed_v<T>) {
        return v >= 0 ?
            (float(v) / std::numeric_limits<T>::max()) :
            (float(v) / -std::numeric_limits<T>::min());
    } else {
        return float(v) / std::numeric_limits<T>::max();
    }
}

/* Integer values are normalized to [-1,1] or [0,1] */
template <typename T>
static void set_attribute_n(GLuint index, T v0, T v1, T v2, T v3)
{
    set_attribute(index,
                  normalize(v0), normalize(v1), normalize(v2), normalize(v3));
}

template <typename T>
static void set_attribute_n(GLuint index, const T *v)
{
    set_attribute_n(index, v[0], v[1], v[2], v[3]);
}

void glVertexAttrib1d(GLuint index, GLdouble x)
{
    set_attribute(index, x);
}

void glVertexAttrib1dv(GLuint index, const GLdouble *v)
{
    set_attribute<1>(index, v);
}

void glVertexAttrib1f(GLuint index, GLfloat x)
{
    set_attribute(index, x);
}

void glVertexAttrib1fv(GLuint index, const GLfloat *v)
{
    set_attribute<1>(index, v);
}

void glVertexAttrib1s(GLuint index, GLshort x)
{
    set_attribute(index, x);
}

void glVertexAttrib1sv(GLuint index, const GLshort *v)
{
    set_attribute<1>(index, v);
}

void glVertexAttrib2d(GLuint index, GLdouble x, GLdouble y)
{
    set_attribute(index, x, y);
}

void glVertexAttrib2dv(GLuint index, const GLdouble *v)
{
    set_attribute<2>(index, v);
}

void glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y)
{
    set_attribute(index, x, y);
}

void glVertexAttrib2fv(GLuint index, const GLfloat *v)
{
    set_attribute<2>(index, v);
}

void glVertexAttrib2s(GLuint index, GLshort x, GLshort y)
{
    set_attribute(index, x, y);
}

void glVertexAttrib2sv(GLuint index, const GLshort *v)
{
    set_attribute<2>(index, v);
}

void glVertexAttrib3d(GLuint index, GLdouble x, GLdouble y, GLdouble z)
{
    set_attribute(index, x, y, z);
}

void glVertexAttrib3dv(GLuint index, const GLdouble *v)
{
    set_attribute<3>(index, v);
}

void glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z)
{
    set_attribute(index, x, y, z);
}

void glVertexAttrib3fv(GLuint index, const GLfloat *v)
{
    set_attribute<3>(index, v);
}

void glVertexAttrib3s(GLuint index, GLshort x, GLshort y, GLshort z)
{
    set_attribute(index, x, y, z);
}

void glVertexAttrib3sv(GLuint index, const GLshort *v)
{
    set_attribute<3>(index, v);
}

void glVertexAttrib4Nbv(GLuint index, const GLbyte *v)
{
    set_attribute_n(index, v);
}

void glVertexAttrib4Niv(GLuint index, const GLint *v)
{
    set_attribute_n(index, v);
}

void glVertexAttrib4Nsv(GLuint index, const GLshort *v)
{
    set_attribute_n(index, v);
}

void glVertexAttrib4Nub(GLuint index, GLubyte x, GLubyte y, GLubyte z, GLubyte w)
{
    set_attribute_n(index, x, y, z, w);
}

void glVertexAttrib4Nubv(GLuint index, const GLubyte *v)
{
    set_attribute_n(index, v);
}

void glVertexAttrib4Nuiv(GLuint index, const GLuint *v)
{
    set_attribute_n(index, v);
}

void glVertexAttrib4Nusv(GLuint index, const GLushort *v)
{
    set_attribute_n(index, v);
}

void glVertexAttrib4bv(GLuint index, const GLbyte *v)
{
    set_attribute<4>(index, v);
}

void glVertexAttrib4d(GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
    set_attribute(index, x, y, z, w);
}

void glVertexAttrib4dv(GLuint index, const GLdouble *v)
{
    set_attribute<4>(index, v);
}

void glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    set_attribute(index, x, y, z, w);
}

void glVertexAttrib4fv(GLuint index, const GLfloat *v)
{
    set_attribute<4>(index, v);
}

void glVertexAttrib4iv(GLuint index, const GLint *v)
{
    set_attribute<4>(index, v);
}

void glVertexAttrib4s(GLuint index, GLshort x, GLshort y, GLshort z, GLshort w)
{
    set_attribute(index, x, y, z, w);
}

void glVertexAttrib4sv(GLuint index, const GLshort *v)
{
    set_attribute<4>(index, v);
}

void glVertexAttrib4ubv(GLuint index, const GLubyte *v)
{
    set_attribute<4>(index, v);
}

void glVertexAttrib4uiv(GLuint index, const GLuint *v)
{
    set_attribute<4>(index, v);
}

void glVertexAttrib4usv(GLuint index, const GLushort *v)
{
    set_attribute<4>(index, v);
}
