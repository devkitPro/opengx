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

#include "call_lists.h"
#include "debug.h"
#include "state.h"
#include "utils.h"

#include <GL/gl.h>
#include <malloc.h>
#include <type_traits>

template <typename T>
T full_color()
{
    if constexpr (std::is_floating_point<T>::value) {
        return 1.0f;
    } else {
        return std::numeric_limits<T>::max();
    }
}

template <typename T>
void set_current_color(T red, T green, T blue, T alpha = full_color<T>())
{
    float c[4];
    if constexpr (std::is_floating_point<T>::value) {
        c[0] = red;
        c[1] = green;
        c[2] = blue;
        c[3] = alpha;
    } else {
        constexpr float max = std::numeric_limits<T>::max();
        /* The OpenGL specification says that for signed components the most
         * negative value representable by T should be mapped to -1.0, whereas
         * with our conversion its getting converted to a bit less than that,
         * but let's fix this only if it turns out to be a real issue. */
        c[0] = red / max;
        c[1] = green / max;
        c[2] = blue / max;
        c[3] = alpha / max;
    }

    if (glparamstate.imm_mode.in_gl_begin) {
        glparamstate.imm_mode.has_color = 1;
    } else {
        HANDLE_CALL_LIST(COLOR, c);
    }

    floatcpy(glparamstate.imm_mode.current_color, c, 4);
    glparamstate.dirty.bits.dirty_tev = 1;
}

static inline void set_current_tex_unit_coords(int unit, float s, float t = 0)
{
    auto &c = glparamstate.imm_mode.current_texcoord[unit];
    c[0] = s;
    c[1] = t;

    if (glparamstate.imm_mode.in_gl_begin) {
        glparamstate.imm_mode.has_texcoord |= (1 << unit);
    }
}

static void set_current_tex_unit_coords(int unit, float s, float t,
                                        float r, float q = 1.0f)
{
    set_current_tex_unit_coords(unit, s, t);
    if (r != 0.0f || q != 1.0f) {
        warning("glTexCoord{3,4}* not supported");
    }
}

static inline void set_current_tex_coords(float s, float t = 0)
{
    set_current_tex_unit_coords(0, s, t);
}

static inline void set_current_tex_coords(float s, float t, float r, float q = 1.0f)
{
    set_current_tex_unit_coords(0, s, t, r, q);
}

void glVertex2d(GLdouble x, GLdouble y)
{
    glVertex3f(x, y, 0.0f);
}

void glVertex2f(GLfloat x, GLfloat y)
{
    glVertex3f(x, y, 0.0f);
}

void glVertex2i(GLint x, GLint y)
{
    glVertex3f(x, y, 0.0f);
}

void glVertex2s(GLshort x, GLshort y)
{
    glVertex2f(x, y);
}

void glVertex3d(GLdouble x, GLdouble y, GLdouble z)
{
    glVertex3f(x, y, z);
}

void glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    if (glparamstate.imm_mode.current_numverts >= glparamstate.imm_mode.current_vertices_size) {
        if (!glparamstate.imm_mode.current_vertices) return;
        int current_size = glparamstate.imm_mode.current_vertices_size;
        int new_size = current_size < 256 ? (current_size * 2) : (current_size + 256);
        void *new_buffer = realloc(glparamstate.imm_mode.current_vertices,
                                   new_size * sizeof(VertexData));
        if (!new_buffer) {
            warning("Failed to reallocate memory for vertex buffer (%d)", errno);
            set_error(GL_OUT_OF_MEMORY);
            return;
        }
        glparamstate.imm_mode.current_vertices_size = new_size;
        glparamstate.imm_mode.current_vertices = (VertexData*)new_buffer;
    }

    VertexData *vert = &glparamstate.imm_mode.current_vertices[glparamstate.imm_mode.current_numverts++];
    for (int i = 0; i < MAX_TEXTURE_UNITS; i++) {
        if (glparamstate.imm_mode.has_texcoord & (1 << i)) {
            vert->tex[i][0] = glparamstate.imm_mode.current_texcoord[i][0];
            vert->tex[i][1] = glparamstate.imm_mode.current_texcoord[i][1];
        }
    }

    vert->color = gxcol_new_fv(glparamstate.imm_mode.current_color);

    floatcpy(vert->norm, glparamstate.imm_mode.current_normal, 3);

    vert->pos[0] = x;
    vert->pos[1] = y;
    vert->pos[2] = z;
}

void glVertex3i(GLint x, GLint y, GLint z)
{
    glVertex3f(x, y, z);
}

void glVertex3s(GLshort x, GLshort y, GLshort z)
{
    glVertex3f(x, y, z);
}

void glVertex4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
    glVertex4f(x, y, z, w);
}

void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    glVertex3f(x / w, y / w, z / w);
}

void glVertex4i(GLint x, GLint y, GLint z, GLint w)
{
    glVertex4f(x, y, z, w);
}

void glVertex4s(GLshort x, GLshort y, GLshort z, GLshort w)
{
    glVertex4f(x, y, z, w);
}

void glVertex2dv(const GLdouble *v)
{
    glVertex2d(v[0], v[1]);
}

void glVertex2fv(const GLfloat *v)
{
    glVertex2f(v[0], v[1]);
}

void glVertex2iv(const GLint *v)
{
    glVertex2i(v[0], v[1]);
}

void glVertex2sv(const GLshort *v)
{
    glVertex2s(v[0], v[1]);
}

void glVertex3dv(const GLdouble *v)
{
    glVertex3d(v[0], v[1], v[2]);
}

void glVertex3fv(const GLfloat *v)
{
    glVertex3f(v[0], v[1], v[2]);
}

void glVertex3iv(const GLint *v)
{
    glVertex3i(v[0], v[1], v[2]);
}

void glVertex3sv(const GLshort *v)
{
    glVertex3s(v[0], v[1], v[2]);
}

void glVertex4dv(const GLdouble *v)
{
    glVertex4d(v[0], v[1], v[2], v[3]);
}

void glVertex4fv(const GLfloat *v)
{
    glVertex4f(v[0], v[1], v[2], v[3]);
}

void glVertex4iv(const GLint *v)
{
    glVertex4i(v[0], v[1], v[2], v[3]);
}

void glVertex4sv(const GLshort *v)
{
    glVertex4s(v[0], v[1], v[2], v[3]);
}

void glNormal3b(GLbyte nx, GLbyte ny, GLbyte nz)
{
    glNormal3f(nx, ny, nz);
}

void glNormal3d(GLdouble nx, GLdouble ny, GLdouble nz)
{
    glNormal3f(nx, ny, nz);
}

void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz)
{
    float v[3] = { nx, ny, nz };
    glNormal3fv(v);
}

void glNormal3i(GLint nx, GLint ny, GLint nz)
{
    glNormal3f(nx, ny, nz);
}

void glNormal3s(GLshort nx, GLshort ny, GLshort nz)
{
    glNormal3f(nx, ny, nz);
}

void glNormal3bv(const GLbyte *v)
{
    glNormal3b(v[0], v[1], v[2]);
}

void glNormal3dv(const GLdouble *v)
{
    glNormal3d(v[0], v[1], v[2]);
}

void glNormal3fv(const GLfloat *v)
{
    if (glparamstate.imm_mode.in_gl_begin) {
        glparamstate.imm_mode.has_normal = 1;
    } else {
        HANDLE_CALL_LIST(NORMAL, v);
    }
    floatcpy(glparamstate.imm_mode.current_normal, v, 3);
    glparamstate.dirty.bits.dirty_tev = 1;
}

void glNormal3iv(const GLint *v)
{
    glNormal3i(v[0], v[1], v[2]);
}

void glNormal3sv(const GLshort *v)
{
    glNormal3s(v[0], v[1], v[2]);
}

void glColor3b(GLbyte red, GLbyte green, GLbyte blue)
{
    set_current_color(red, green, blue);
}

void glColor3d(GLdouble red, GLdouble green, GLdouble blue)
{
    set_current_color(red, green, blue);
}

void glColor3f(GLfloat red, GLfloat green, GLfloat blue)
{
    set_current_color(red, green, blue);
}

void glColor3i(GLint red, GLint green, GLint blue)
{
    set_current_color(red, green, blue);
}

void glColor3s(GLshort red, GLshort green, GLshort blue)
{
    set_current_color(red, green, blue);
}

void glColor3ub(GLubyte red, GLubyte green, GLubyte blue)
{
    set_current_color(red, green, blue);
}

void glColor3ui(GLuint red, GLuint green, GLuint blue)
{
    set_current_color(red, green, blue);
}

void glColor3us(GLushort red, GLushort green, GLushort blue)
{
    set_current_color(red, green, blue);
}

void glColor4b(GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha)
{
    set_current_color(red, green, blue, alpha);
}

void glColor4d(GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha)
{
    set_current_color(red, green, blue, alpha);
}

void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    set_current_color(red, green, blue, alpha);
}

void glColor4i(GLint red, GLint green, GLint blue, GLint alpha)
{
    set_current_color(red, green, blue, alpha);
}

void glColor4s(GLshort red, GLshort green, GLshort blue, GLshort alpha)
{
    set_current_color(red, green, blue, alpha);
}

void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
    set_current_color(red, green, blue, alpha);
}

void glColor4ui(GLuint red, GLuint green, GLuint blue, GLuint alpha)
{
    set_current_color(red, green, blue, alpha);
}

void glColor4us(GLushort red, GLushort green, GLushort blue, GLushort alpha)
{
    set_current_color(red, green, blue, alpha);
}

void glColor3bv(const GLbyte *v) { set_current_color(v[0], v[1], v[2]); }
void glColor3dv(const GLdouble *v) { set_current_color(v[0], v[1], v[2]); }
void glColor3fv(const GLfloat *v) { set_current_color(v[0], v[1], v[2]); }
void glColor3iv(const GLint *v) { set_current_color(v[0], v[1], v[2]); }
void glColor3sv(const GLshort *v) { set_current_color(v[0], v[1], v[2]); }
void glColor3ubv(const GLubyte *v) { set_current_color(v[0], v[1], v[2]); }
void glColor3uiv(const GLuint *v) { set_current_color(v[0], v[1], v[2]); }
void glColor3usv(const GLushort *v) { set_current_color(v[0], v[1], v[2]); }

void glColor4bv(const GLbyte *v) { set_current_color(v[0], v[1], v[2], v[3]); }
void glColor4dv(const GLdouble *v) { set_current_color(v[0], v[1], v[2], v[3]); }
void glColor4fv(const GLfloat *v) { set_current_color(v[0], v[1], v[2], v[3]); }
void glColor4iv(const GLint *v) { set_current_color(v[0], v[1], v[2], v[3]); }
void glColor4sv(const GLshort *v) { set_current_color(v[0], v[1], v[2], v[3]); }
void glColor4ubv(const GLubyte *v) { set_current_color(v[0], v[1], v[2], v[3]); }
void glColor4uiv(const GLuint *v) { set_current_color(v[0], v[1], v[2], v[3]); }
void glColor4usv(const GLushort *v) { set_current_color(v[0], v[1], v[2], v[3]); }

void glTexCoord1d(GLdouble s) { set_current_tex_coords(s); }
void glTexCoord1f(GLfloat s) { set_current_tex_coords(s); }
void glTexCoord1i(GLint s) { set_current_tex_coords(s); }
void glTexCoord1s(GLshort s) { set_current_tex_coords(s); }

void glTexCoord2d(GLdouble s, GLdouble t) { set_current_tex_coords(s, t); }
void glTexCoord2f(GLfloat s, GLfloat t) { set_current_tex_coords(s, t); }
void glTexCoord2i(GLint s, GLint t) { set_current_tex_coords(s, t); }
void glTexCoord2s(GLshort s, GLshort t) { set_current_tex_coords(s, t); }

void glTexCoord3d(GLdouble s, GLdouble t, GLdouble r) { set_current_tex_coords(s, t, r); }
void glTexCoord3f(GLfloat s, GLfloat t, GLfloat r) { set_current_tex_coords(s, t, r); }
void glTexCoord3i(GLint s, GLint t, GLint r) { set_current_tex_coords(s, t, r); }
void glTexCoord3s(GLshort s, GLshort t, GLshort r) { set_current_tex_coords(s, t, r); }

void glTexCoord4d(GLdouble s, GLdouble t, GLdouble r, GLdouble q)
{
    set_current_tex_coords(s, t, r, q);
}

void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    set_current_tex_coords(s, t, r, q);
}

void glTexCoord4i(GLint s, GLint t, GLint r, GLint q)
{
    set_current_tex_coords(s, t, r, q);
}

void glTexCoord4s(GLshort s, GLshort t, GLshort r, GLshort q)
{
    set_current_tex_coords(s, t, r, q);
}

void glTexCoord1dv(const GLdouble *v) { set_current_tex_coords(v[0]); }
void glTexCoord1fv(const GLfloat *v) { set_current_tex_coords(v[0]); }
void glTexCoord1iv(const GLint *v) { set_current_tex_coords(v[0]); }
void glTexCoord1sv(const GLshort *v) { set_current_tex_coords(v[0]); }

void glTexCoord2dv(const GLdouble *v) { set_current_tex_coords(v[0], v[1]); }
void glTexCoord2fv(const GLfloat *v) { set_current_tex_coords(v[0], v[1]); }
void glTexCoord2iv(const GLint *v) { set_current_tex_coords(v[0], v[1]); }
void glTexCoord2sv(const GLshort *v) { set_current_tex_coords(v[0], v[1]); }

void glTexCoord3dv(const GLdouble *v) { set_current_tex_coords(v[0], v[1], v[2]); }
void glTexCoord3fv(const GLfloat *v) { set_current_tex_coords(v[0], v[1], v[2]); }
void glTexCoord3iv(const GLint *v) { set_current_tex_coords(v[0], v[1], v[2]); }
void glTexCoord3sv(const GLshort *v) { set_current_tex_coords(v[0], v[1], v[2]); }

void glTexCoord4dv(const GLdouble *v) { set_current_tex_coords(v[0], v[1], v[2], v[3]); }
void glTexCoord4fv(const GLfloat *v) { set_current_tex_coords(v[0], v[1], v[2], v[3]); }
void glTexCoord4iv(const GLint *v) { set_current_tex_coords(v[0], v[1], v[2], v[3]); }
void glTexCoord4sv(const GLshort *v) { set_current_tex_coords(v[0], v[1], v[2], v[3]); }

#define sctuc(target, ...) \
    set_current_tex_unit_coords(target - GL_TEXTURE0, __VA_ARGS__)
void glMultiTexCoord1d(GLenum unit, GLdouble s) { sctuc(unit, s); }
void glMultiTexCoord1f(GLenum unit, GLfloat s) { sctuc(unit, s); }
void glMultiTexCoord1i(GLenum unit, GLint s) { sctuc(unit, s); }
void glMultiTexCoord1s(GLenum unit, GLshort s) { sctuc(unit, s); }

void glMultiTexCoord2d(GLenum unit, GLdouble s, GLdouble t) {
    sctuc(unit, s, t);
}

void glMultiTexCoord2f(GLenum unit, GLfloat s, GLfloat t) {
    sctuc(unit, s, t);
}

void glMultiTexCoord2i(GLenum unit, GLint s, GLint t) {
    sctuc(unit, s, t);
}

void glMultiTexCoord2s(GLenum unit, GLshort s, GLshort t) {
    sctuc(unit, s, t);
}

void glMultiTexCoord3d(GLenum unit, GLdouble s, GLdouble t, GLdouble r) {
    sctuc(unit, s, t, r);
}

void glMultiTexCoord3f(GLenum unit, GLfloat s, GLfloat t, GLfloat r) {
    sctuc(unit, s, t, r);
}

void glMultiTexCoord3i(GLenum unit, GLint s, GLint t, GLint r) {
    sctuc(unit, s, t, r);
}

void glMultiTexCoord3s(GLenum unit, GLshort s, GLshort t, GLshort r) {
    sctuc(unit, s, t, r);
}

void glMultiTexCoord4d(GLenum unit, GLdouble s, GLdouble t, GLdouble r, GLdouble q) {
    sctuc(unit, s, t, r, q);
}

void glMultiTexCoord4f(GLenum unit, GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
    sctuc(unit, s, t, r, q);
}

void glMultiTexCoord4i(GLenum unit, GLint s, GLint t, GLint r, GLint q) {
    sctuc(unit, s, t, r, q);
}

void glMultiTexCoord4s(GLenum unit, GLshort s, GLshort t, GLshort r, GLshort q) {
    sctuc(unit, s, t, r, q);
}

void glMultiTexCoord1dv(GLenum unit, const GLdouble *v) { sctuc(unit, v[0]); }
void glMultiTexCoord1fv(GLenum unit, const GLfloat *v) { sctuc(unit, v[0]); }
void glMultiTexCoord1iv(GLenum unit, const GLint *v) { sctuc(unit, v[0]); }
void glMultiTexCoord1sv(GLenum unit, const GLshort *v) { sctuc(unit, v[0]); }

void glMultiTexCoord2dv(GLenum unit, const GLdouble *v) { sctuc(unit, v[0], v[1]); }
void glMultiTexCoord2fv(GLenum unit, const GLfloat *v) { sctuc(unit, v[0], v[1]); }
void glMultiTexCoord2iv(GLenum unit, const GLint *v) { sctuc(unit, v[0], v[1]); }
void glMultiTexCoord2sv(GLenum unit, const GLshort *v) { sctuc(unit, v[0], v[1]); }

void glMultiTexCoord3dv(GLenum unit, const GLdouble *v) { sctuc(unit, v[0], v[1], v[2]); }
void glMultiTexCoord3fv(GLenum unit, const GLfloat *v) { sctuc(unit, v[0], v[1], v[2]); }
void glMultiTexCoord3iv(GLenum unit, const GLint *v) { sctuc(unit, v[0], v[1], v[2]); }
void glMultiTexCoord3sv(GLenum unit, const GLshort *v) { sctuc(unit, v[0], v[1], v[2]); }

void glMultiTexCoord4dv(GLenum unit, const GLdouble *v) {
    sctuc(unit, v[0], v[1], v[2], v[3]);
}

void glMultiTexCoord4fv(GLenum unit, const GLfloat *v) {
    sctuc(unit, v[0], v[1], v[2], v[3]);
}

void glMultiTexCoord4iv(GLenum unit, const GLint *v) {
    sctuc(unit, v[0], v[1], v[2], v[3]);
}

void glMultiTexCoord4sv(GLenum unit, const GLshort *v) {
    sctuc(unit, v[0], v[1], v[2], v[3]);
}

void glRectd(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2)
{
    glBegin(GL_POLYGON);
    glVertex2d(x1, y1);
    glVertex2d(x2, y1);
    glVertex2d(x2, y2);
    glVertex2d(x1, y2);
    glEnd();
}

void glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2)
{
    glBegin(GL_POLYGON);
    glVertex2f(x1, y1);
    glVertex2f(x2, y1);
    glVertex2f(x2, y2);
    glVertex2f(x1, y2);
    glEnd();
}

void glRecti(GLint x1, GLint y1, GLint x2, GLint y2)
{
    glBegin(GL_POLYGON);
    glVertex2i(x1, y1);
    glVertex2i(x2, y1);
    glVertex2i(x2, y2);
    glVertex2i(x1, y2);
    glEnd();
}

void glRects(GLshort x1, GLshort y1, GLshort x2, GLshort y2)
{
    glBegin(GL_POLYGON);
    glVertex2s(x1, y1);
    glVertex2s(x2, y1);
    glVertex2s(x2, y2);
    glVertex2s(x1, y2);
    glEnd();
}

void glRectdv(const GLdouble *v1, const GLdouble *v2) { glRectd(v1[0], v1[1], v2[0], v2[1]); }
void glRectfv(const GLfloat *v1, const GLfloat *v2) { glRectf(v1[0], v1[1], v2[0], v2[1]); }
void glRectiv(const GLint *v1, const GLint *v2) { glRecti(v1[0], v1[1], v2[0], v2[1]); }
void glRectsv(const GLshort *v1, const GLshort *v2) { glRects(v1[0], v1[1], v2[0], v2[1]); }
