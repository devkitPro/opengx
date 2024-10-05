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

#include "clip.h"
#include "debug.h"
#include "pixels.h"
#include "state.h"
#include "utils.h"

#include <GL/gl.h>
#include <malloc.h>
#include <type_traits>

static void set_current_raster_pos(const guVector *pos)
{
    guVector pos_mv;
    guVecMultiply(glparamstate.modelview_matrix, pos, &pos_mv);

    if (_ogx_clip_is_point_clipped(&pos_mv)) {
        glparamstate.raster_pos_valid = false;
        return;
    }

    /* Apply the projection transformation */
    guVector pos_pj;
    mtx44project(glparamstate.projection_matrix, &pos_mv, &pos_pj);

    /* And the viewport transformation */
    float ox = glparamstate.viewport[2] / 2 + glparamstate.viewport[0];
    float oy = glparamstate.viewport[3] / 2 + glparamstate.viewport[1];
    glparamstate.raster_pos[0] =
        (glparamstate.viewport[2] * pos_pj.x) / 2 + ox;
    glparamstate.raster_pos[1] =
        (glparamstate.viewport[3] * pos_pj.y) / 2 + oy;
    const float n = glparamstate.depth_near;
    const float f = glparamstate.depth_far;
    glparamstate.raster_pos[2] = (pos_pj.z * (f - n) + (f + n)) / 2;
    glparamstate.raster_pos_valid = true;
}

static inline void set_pos(float x, float y, float z = 1.0)
{
    guVector p = { x, y, z };
    set_current_raster_pos(&p);
}

static inline void set_pos(float x, float y, float z, float w)
{
    set_pos(x / w, y / w, z / w);
}

void glRasterPos2d(GLdouble x, GLdouble y) { set_pos(x, y); }
void glRasterPos2f(GLfloat x, GLfloat y) { set_pos(x, y); }
void glRasterPos2i(GLint x, GLint y) { set_pos(x, y); }
void glRasterPos2s(GLshort x, GLshort y) { set_pos(x, y); }
void glRasterPos3d(GLdouble x, GLdouble y, GLdouble z) { set_pos(x, y, z); }
void glRasterPos3f(GLfloat x, GLfloat y, GLfloat z) { set_pos(x, y, z); }
void glRasterPos3i(GLint x, GLint y, GLint z) { set_pos(x, y, z); }
void glRasterPos3s(GLshort x, GLshort y, GLshort z) { set_pos(x, y, z); }
void glRasterPos4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w) { set_pos(x, y, z, w); }
void glRasterPos4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) { set_pos(x, y, z, w); }
void glRasterPos4i(GLint x, GLint y, GLint z, GLint w) { set_pos(x, y, z, w); }
void glRasterPos4s(GLshort x, GLshort y, GLshort z, GLshort w) { set_pos(x, y, z, w); }
void glRasterPos2dv(const GLdouble *v) { set_pos(v[0], v[1]); }
void glRasterPos2fv(const GLfloat *v) { set_pos(v[0], v[1]); }
void glRasterPos2iv(const GLint *v) { set_pos(v[0], v[1]); }
void glRasterPos2sv(const GLshort *v) { set_pos(v[0], v[1]); }
void glRasterPos3dv(const GLdouble *v) { set_pos(v[0], v[1], v[2]); }
void glRasterPos3fv(const GLfloat *v) { set_pos(v[0], v[1], v[2]); }
void glRasterPos3iv(const GLint *v) { set_pos(v[0], v[1], v[2]); }
void glRasterPos3sv(const GLshort *v) { set_pos(v[0], v[1], v[2]); }
void glRasterPos4dv(const GLdouble *v) { set_pos(v[0], v[1], v[2], v[3]); }
void glRasterPos4fv(const GLfloat *v) { set_pos(v[0], v[1], v[2], v[3]); }
void glRasterPos4iv(const GLint *v) { set_pos(v[0], v[1], v[2], v[3]); }
void glRasterPos4sv(const GLshort *v) { set_pos(v[0], v[1], v[2], v[3]); }

static void set_pixel_map(GLenum map, GLsizei mapsize, uint8_t *values)
{
    int index = map - GL_PIXEL_MAP_I_TO_I_SIZE;
    if (index < 0 || index >= 10) {
        set_error(GL_INVALID_ENUM);
        return;
    }

    if (!glparamstate.pixel_maps) {
        glparamstate.pixel_maps =
            (OgxPixelMapTables*)malloc(sizeof(OgxPixelMapTables));
        memset(glparamstate.pixel_maps->sizes, 0,
               sizeof(glparamstate.pixel_maps->sizes));
    }

    glparamstate.pixel_maps->sizes[index] = mapsize;
    memcpy(glparamstate.pixel_maps->maps[index], values, mapsize);
}

void glPixelMapfv(GLenum map, GLsizei mapsize, const GLfloat *values)
{
    uint8_t bytevalues[MAX_PIXEL_MAP_TABLE];
    for (int i = 0; i < mapsize; i++) {
        bytevalues[i] = values[i] * 255;
    }
    set_pixel_map(map, mapsize, bytevalues);
}

void glPixelMapuiv(GLenum map, GLsizei mapsize, const GLuint *values)
{
    uint8_t bytevalues[MAX_PIXEL_MAP_TABLE];
    for (int i = 0; i < mapsize; i++) {
        bytevalues[i] = values[i] >> 24;
    }
    set_pixel_map(map, mapsize, bytevalues);
}

void glPixelMapusv(GLenum map, GLsizei mapsize, const GLushort *values)
{
    uint8_t bytevalues[MAX_PIXEL_MAP_TABLE];
    for (int i = 0; i < mapsize; i++) {
        bytevalues[i] = values[i] >> 8;
    }
    set_pixel_map(map, mapsize, bytevalues);
}

template <typename T>
void get_pixel_map(GLenum map, T *values)
{
    int index = map - GL_PIXEL_MAP_I_TO_I_SIZE;
    if (index < 0 || index >= 10) {
        set_error(GL_INVALID_ENUM);
        return;
    }

    if (!glparamstate.pixel_maps) {
        *values = 0;
        return;
    }

    uint8_t map_size = glparamstate.pixel_maps->sizes[index];
    for (int i = 0; i < map_size; i++) {
        T value = glparamstate.pixel_maps->maps[index][i];
        /* We must map value to the target type: use full range for integer
         * types, and 0.0-1.0 for floats */
        if constexpr (std::is_floating_point<T>::value) {
            values[i] = value / 255.0f;
        } else {
            for (int b = 1; b < sizeof(T); b++) {
                value |= value << 8;
            }
            values[i] = value;
        }
    }
}

void glGetPixelMapfv(GLenum map, GLfloat *values)
{
    get_pixel_map(map, values);
}

void glGetPixelMapuiv(GLenum map, GLuint *values)
{
    get_pixel_map(map, values);
}

void glGetPixelMapusv(GLenum map, GLushort *values)
{
    get_pixel_map(map, values);
}

/* Blits a texture at the desired screen position, with fogging and blending
 * enabled, as suitable for the raster functions.
 * Since the color channel and the TEV setup differs between the various
 * functions, it's left up to the caller.
 */
static void draw_raster_texture(GXTexObj *texture, int width, int height,
                                int screen_x, int screen_y, int screen_z)
{
    _ogx_apply_state();
    _ogx_setup_2D_projection();

    GX_LoadTexObj(texture, GX_TEXMAP0);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_U8, 0);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetNumTexGens(1);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);

    GX_SetCullMode(GX_CULL_NONE);
    glparamstate.dirty.bits.dirty_cull = 1;

    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA,
                    GX_LO_CLEAR);
    glparamstate.dirty.bits.dirty_blend = 1;

    /* The first row we read from the bitmap is the bottom row, so let's take
     * this into account and flip the image vertically */
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position3f32(screen_x, screen_y, screen_z);
    GX_TexCoord2u8(0, 0);
    GX_Position3f32(screen_x, screen_y - height, screen_z);
    GX_TexCoord2u8(0, 1);
    GX_Position3f32(screen_x + width, screen_y - height, screen_z);
    GX_TexCoord2u8(1, 1);
    GX_Position3f32(screen_x + width, screen_y, screen_z);
    GX_TexCoord2u8(1, 0);
    GX_End();
}

void glBitmap(GLsizei width, GLsizei height,
              GLfloat xorig, GLfloat yorig,
              GLfloat xmove, GLfloat ymove,
              const GLubyte *bitmap)
{
    if (width < 0 || height < 0) {
        set_error(GL_INVALID_VALUE);
        return;
    }

    if (!glparamstate.raster_pos_valid) return;

    float pos_x = int(glparamstate.raster_pos[0] - xorig);
    float pos_y = int(glparamstate.viewport[3] -
                      (glparamstate.raster_pos[1] - yorig));
    float pos_z = -glparamstate.raster_pos[2];

    /* We don't have a 1-bit format in GX, so use a 4-bit format */
    u32 size = GX_GetTexBufferSize(width, height, GX_TF_I4, 0, GX_FALSE);
    void *texels = memalign(32, size);
    memset(texels, 0, size);
    int dstpitch = _ogx_pitch_for_width(GX_TF_I4, width);
    _ogx_bytes_to_texture(bitmap, GL_COLOR_INDEX, GL_BITMAP,
                          width, height, texels, GX_TF_I4,
                          0, 0, dstpitch);
    DCFlushRange(texels, size);

    GXTexObj texture;
    GX_InitTexObj(&texture, texels,
                  width, height, GX_TF_I4, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&texture, GX_NEAR, GX_NEAR,
                     0.0f, 0.0f, 0, 0, 0, GX_ANISO_1);
    GX_InvalidateTexAll();

    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_REG,
                   0, GX_DF_NONE, GX_AF_NONE);
    GXColor ccol = gxcol_new_fv(glparamstate.imm_mode.current_color);
    GX_SetTevColor(GX_TEVREG0, ccol);

    /* In data: d: Raster Color */
    GX_SetTevColorIn(GX_TEVSTAGE0,
                     GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
    /* Multiply the alpha from the texture with the alpha from the raster
     * color. */
    GX_SetTevAlphaIn(GX_TEVSTAGE0,
                     GX_CA_ZERO, GX_CA_TEXA, GX_CA_A0, GX_CA_ZERO);
    GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                     GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                     GX_TRUE, GX_TEVPREV);
    draw_raster_texture(&texture, width, height, pos_x, pos_y, pos_z);

    /* We need to wait for the drawing to be complete before freeing the
     * texture memory */
    GX_SetDrawDone();

    glparamstate.raster_pos[0] += xmove;
    glparamstate.raster_pos[1] += ymove;

    GX_WaitDrawDone();
    free(texels);
}