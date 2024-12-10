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
#include "efb.h"
#include "pixel_stream.h"
#include "pixels.h"
#include "state.h"
#include "stencil.h"
#include "texel.h"
#include "utils.h"

#include <GL/gl.h>
#include <malloc.h>
#include <memory>
#include <type_traits>

void glPixelZoom(GLfloat xfactor, GLfloat yfactor)
{
    glparamstate.pixel_zoom_x = xfactor;
    glparamstate.pixel_zoom_y = yfactor;
}

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
 * If height is negative, the image will be flipped.
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
    glparamstate.dirty.bits.dirty_tev = 1;

    GX_SetCullMode(GX_CULL_NONE);
    glparamstate.dirty.bits.dirty_cull = 1;

    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA,
                    GX_LO_CLEAR);
    glparamstate.dirty.bits.dirty_blend = 1;

    int y0, y1;
    if (height < 0) {
        y0 = screen_y + height * glparamstate.pixel_zoom_y;
        y1 = screen_y;
    } else {
        /* The first row we read from the bitmap is the bottom row, so let's take
         * this into account and flip the image vertically */
        y0 = screen_y;
        y1 = screen_y - height * glparamstate.pixel_zoom_y;
    }
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position3f32(screen_x, y0, screen_z);
    GX_TexCoord2u8(0, 0);
    GX_Position3f32(screen_x, y1, screen_z);
    GX_TexCoord2u8(0, 1);
    GX_Position3f32(screen_x + width * glparamstate.pixel_zoom_x, y1, screen_z);
    GX_TexCoord2u8(1, 1);
    GX_Position3f32(screen_x + width * glparamstate.pixel_zoom_x, y0, screen_z);
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

static const struct ReadPixelFormat {
    GLenum format;
    uint8_t gx_copy_format;
    uint8_t gx_dest_format;
    int n_components;
} s_read_pixel_formats[] = {
    { GL_RED, GX_CTF_R8, GX_TF_I8, 1 },
    { GL_GREEN, GX_CTF_G8, GX_TF_I8, 1 },
    { GL_BLUE, GX_CTF_B8, GX_TF_I8, 1 },
    { GL_ALPHA, GX_CTF_A8, GX_TF_I8, 1 },
    { GL_LUMINANCE, GX_TF_I8, GX_TF_I8, 1 },
    { GL_LUMINANCE_ALPHA, GX_TF_IA8, GX_TF_IA8, 2 },
    { GL_RGB, GX_TF_RGBA8, GX_TF_RGBA8, 3 },
    { GL_RGBA, GX_TF_RGBA8, GX_TF_RGBA8, 4 },
    { GL_DEPTH_COMPONENT, GX_TF_Z24X8, GX_TF_RGBA8, 1 },
    { 0, },
};

union PixelData {
    GXColor color;
    uint8_t component[4];
    float depth;
};

struct TextureReader {
    TextureReader(const ReadPixelFormat *read_format,
                  void *texels, int width, int height):
        m_texel(new_texel_for_format(read_format->gx_dest_format))
    {
        int pitch = m_texel->pitch_for_width(width);
        m_texel->set_area(texels, 0, 0, width, height, pitch);
    }

    Texel *new_texel_for_format(uint8_t gx_format) {
        switch (gx_format) {
        case GX_CTF_R4: return new TexelI4;
        case GX_TF_I8: return new TexelI8;
        case GX_TF_IA8: return new TexelIA8;
        case GX_TF_RGBA8: return new TexelRGBA8;
        default: return nullptr;
        }
    }

    void read(PixelData *pixel) {
        GXColor c = m_texel->read();
        pixel->color = c;
    }

    std::unique_ptr<Texel> m_texel;
};

struct PixelWriter {
    PixelWriter(void *data, int width, int height, GLenum format, GLenum type):
        m_pixel(new_pixel_for_format(format, type)),
        m_width(width), m_height(height),
        m_format(format), m_type(type) {
        m_pixel->setup_stream(data, width, height);
    }

    PixelStreamBase *new_pixel_for_format(GLenum format, GLenum type) {
        if (format == GL_DEPTH_COMPONENT) {
            switch (type) {
            case GL_UNSIGNED_BYTE:
                return new DepthPixelStream<uint8_t>(format, type);
            case GL_UNSIGNED_SHORT:
                return new DepthPixelStream<uint16_t>(format, type);
            case GL_UNSIGNED_INT:
                return new DepthPixelStream<uint32_t>(format, type);
            case GL_FLOAT:
                return new DepthPixelStream<float>(format, type);
            }
        } else if (format == GL_STENCIL_INDEX) {
            switch (type) {
            case GL_UNSIGNED_BYTE:
                return new StencilPixelStream<uint8_t>(format, type);
            case GL_UNSIGNED_SHORT:
                return new StencilPixelStream<uint16_t>(format, type);
            case GL_UNSIGNED_INT:
                return new StencilPixelStream<uint32_t>(format, type);
            case GL_FLOAT:
                return new StencilPixelStream<float>(format, type);
            }
        }
        switch (type) {
        case GL_UNSIGNED_BYTE:
            return new GenericPixelStream<uint8_t>(format, type);
        case GL_UNSIGNED_SHORT:
            return new GenericPixelStream<uint16_t>(format, type);
        case GL_UNSIGNED_INT:
            return new GenericPixelStream<uint32_t>(format, type);
        case GL_FLOAT:
            return new GenericPixelStream<float>(format, type);
        case GL_UNSIGNED_BYTE_3_3_2:
        case GL_UNSIGNED_BYTE_2_3_3_REV:
        case GL_UNSIGNED_SHORT_5_6_5:
        case GL_UNSIGNED_SHORT_5_6_5_REV:
        case GL_UNSIGNED_SHORT_4_4_4_4:
        case GL_UNSIGNED_SHORT_4_4_4_4_REV:
        case GL_UNSIGNED_SHORT_5_5_5_1:
        case GL_UNSIGNED_SHORT_1_5_5_5_REV:
        case GL_UNSIGNED_INT_8_8_8_8:
        case GL_UNSIGNED_INT_8_8_8_8_REV:
        case GL_UNSIGNED_INT_10_10_10_2:
        case GL_UNSIGNED_INT_2_10_10_10_REV:
            return new CompoundPixelStream(format, type);
        default:
            warning("Unknown texture data type %x\n", type);
            return nullptr;
        }
    }

    void write(const PixelData *pixel) {
        m_pixel->write(pixel->color);
    }

    std::unique_ptr<PixelStreamBase> m_pixel;
    int m_width;
    int m_height;
    GLenum m_format;
    GLenum m_type;
};

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, GLvoid *data)
{
    uint8_t gxformat = 0xff;
    const ReadPixelFormat *read_format = NULL;
    ReadPixelFormat stencil_format;
    int n_components;
    void *texels = nullptr;
    bool must_free_texels = false;

    for (int i = 0; s_read_pixel_formats[i].format != 0; i++) {
        if (s_read_pixel_formats[i].format == format) {
            read_format = &s_read_pixel_formats[i];
            break;
        }
    }
    if (!read_format) {
        if (format == GL_STENCIL_INDEX) {
            OgxEfbBuffer *stencil = _ogx_stencil_get_buffer();
            stencil_format = {
                format, 0,
                uint8_t(GX_GetTexObjFmt(&stencil->texobj)), 1 };
            read_format = &stencil_format;
            texels = _ogx_efb_buffer_get_texels(stencil);
        } else {
            warning("glReadPixels: unsupported format %04x", format);
            return;
        }
    }

    if (!texels) {
        u32 size = GX_GetTexBufferSize(width, height,
                                       read_format->gx_dest_format, 0, GX_FALSE);
        texels = memalign(32, size);
        _ogx_efb_save_area_to_buffer(read_format->gx_copy_format, x, y,
                                     width, height, texels, OGX_EFB_NONE);
        must_free_texels = true;
    }
    TextureReader reader(read_format, texels, width, height);
    PixelWriter writer(data, width, height, format, type);
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            PixelData pixel;
            reader.read(&pixel);
            writer.write(&pixel);
        }
    }

    if (must_free_texels) {
        free(texels);
    }
}

void glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type,
                  const GLvoid *pixels)
{
    if (width < 0 || height < 0) {
        set_error(GL_INVALID_VALUE);
        return;
    }

    if (!glparamstate.raster_pos_valid) return;

    float pos_x = int(glparamstate.raster_pos[0]);
    float pos_y = int(glparamstate.viewport[3] -
                      (glparamstate.raster_pos[1]));
    float pos_z = -glparamstate.raster_pos[2];

    uint8_t gx_format = _ogx_find_best_gx_format(format, format,
                                                 width, height);
    u32 size = GX_GetTexBufferSize(width, height, gx_format, 0, GX_FALSE);
    void *texels = memalign(32, size);
    int dstpitch = _ogx_pitch_for_width(gx_format, width);
    _ogx_bytes_to_texture(pixels, format, type,
                          width, height, texels, gx_format,
                          0, 0, dstpitch);
    DCFlushRange(texels, size);

    GXTexObj texture;
    GX_InitTexObj(&texture, texels,
                  width, height, gx_format, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&texture, GX_NEAR, GX_NEAR,
                     0.0f, 0.0f, 0, 0, 0, GX_ANISO_1);
    GX_InvalidateTexAll();

    GX_SetNumChans(0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    bool no_alpha = false;
    if (format == GL_LUMINANCE) {
        /* Set alpha to 1.0 */
        GXColor ccol = { 0, 0, 0, 255 };
        GX_SetTevColor(GX_TEVREG0, ccol);
        GX_SetTevAlphaIn(GX_TEVSTAGE0,
                         GX_CA_A0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    }
    draw_raster_texture(&texture, width, height, pos_x, pos_y, pos_z);

    /* We need to wait for the drawing to be complete before freeing the
     * texture memory */
    GX_DrawDone();

    free(texels);
}

void glCopyPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type)
{
    if (type != GL_COLOR) {
        warning("glCopyPixels() only implemented for color copies");
        return;
    }

    if (!glparamstate.raster_pos_valid) return;

    float pos_x = int(glparamstate.raster_pos[0]);
    float pos_y = int(glparamstate.viewport[3] -
                      (glparamstate.raster_pos[1]));
    float pos_z = -glparamstate.raster_pos[2];

    /* Since this operation doesn't take the alpha into account, let's use
     * RGB565. If it turns out that some applications need more precision,
     * we'll use GX_TF_RGBA8 */
    uint8_t gx_format = GX_TF_RGB565;
    u32 size = GX_GetTexBufferSize(width, height, gx_format, 0, GX_FALSE);
    void *texels = memalign(32, size);
    GX_SetCopyFilter(GX_FALSE, NULL, GX_FALSE, NULL);
    GX_SetTexCopySrc(x, glparamstate.viewport[3] - y - height, width, height);
    GX_SetTexCopyDst(width, height, gx_format, GX_FALSE);
    GX_CopyTex(texels, GX_FALSE);

    GXTexObj texture;
    GX_InitTexObj(&texture, texels,
                  width, height, gx_format, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&texture, GX_NEAR, GX_NEAR,
                     0.0f, 0.0f, 0, 0, 0, GX_ANISO_1);
    GX_InvalidateTexAll();
    GX_PixModeSync();
    DCInvalidateRange(texels, size);

    GX_SetNumChans(0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    draw_raster_texture(&texture, width, -height, pos_x, pos_y, pos_z);

    /* We need to wait for the drawing to be complete before freeing the
     * texture memory */
    GX_DrawDone();

    free(texels);
}
