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
#include "image_DXT.h"
#include "pixels.h"
#include "state.h"
#include "utils.h"

#include <malloc.h>

/* The GX API allow storing a void * for user data into the GXTexObj; but for
 * now we don't need a pointer, just some bits of information. Let's store them
 * in the space reserved for the pointer, then. */
typedef union {
    void *ptr;
    struct {
        unsigned is_reserved: 1;
        unsigned is_alpha: 1;
    } d;
} UserData;

#define TEXTURE_USER_DATA(texobj) \
    ((UserData)GX_GetTexObjUserData(texobj))
#define TEXTURE_IS_USED(texture) \
    (GX_GetTexObjData(&texture.texobj) != NULL)
#define TEXTURE_IS_RESERVED(texture) \
    (TEXTURE_USER_DATA(&texture.texobj).d.is_reserved)
#define TEXTURE_RESERVE(texture) \
    { \
        UserData ud = TEXTURE_USER_DATA(&(texture).texobj); \
        ud.d.is_reserved = 1; \
        GX_InitTexObjUserData(&(texture).texobj, ud.ptr); \
    }

typedef struct {
    void *texels;
    uint16_t width, height;
    uint8_t format, wraps, wrapt, mipmap;
    uint8_t min_filter, mag_filter;
    uint8_t minlevel, maxlevel;
    UserData ud;
} TextureInfo;

static inline int curr_tex()
{
    int unit = glparamstate.active_texture;
    return glparamstate.texture_unit[unit].glcurtex;
}

static uint32_t calc_memory(int w, int h, uint32_t format)
{
    return GX_GetTexBufferSize(w, h, format, GX_FALSE, 0);
}

// Returns the number of bytes required to store a texture with all its bitmaps
static uint32_t calc_tex_size(int w, int h, uint32_t format)
{
    return GX_GetTexBufferSize(w, h, format, GX_TRUE, 20);
}
// Deduce the original texture size given the current size and level
static int calc_original_size(int level, int s)
{
    while (level > 0) {
        s = 2 * s;
        level--;
    }
    return s;
}
// Given w,h,level,and bpp, returns the offset to the mipmap at level "level"
static uint32_t calc_mipmap_offset(int level, int w, int h, uint32_t format)
{
    return GX_GetTexBufferSize(w, h, format, GX_TRUE, level);
}

static u8 gl_filter_to_gx(GLint gl_filter)
{
    switch (gl_filter) {
    case GL_NEAREST: return GX_NEAR;
    case GL_LINEAR: return GX_LINEAR;
    case GL_NEAREST_MIPMAP_NEAREST: return GX_NEAR_MIP_NEAR;
    case GL_LINEAR_MIPMAP_NEAREST: return GX_LIN_MIP_NEAR;
    case GL_NEAREST_MIPMAP_LINEAR: return GX_NEAR_MIP_LIN;
    case GL_LINEAR_MIPMAP_LINEAR: return GX_LIN_MIP_LIN;
    }
    return GX_NEAR;
}

static unsigned char gcgl_texwrap_conv(GLint param)
{
    switch (param) {
    case GL_MIRRORED_REPEAT:
        return GX_MIRROR;
    case GL_CLAMP:
        return GX_CLAMP;
    case GL_REPEAT:
    default:
        return GX_REPEAT;
    };
}

static void texture_get_info(const GXTexObj *obj, TextureInfo *info)
{
    GX_GetTexObjAll(obj, &info->texels,
                    &info->width, &info->height,
                    &info->format,
                    &info->wraps, &info->wrapt,
                    &info->mipmap);
    if (info->texels) {
        info->texels = MEM_PHYSICAL_TO_K0(info->texels);
    }

    float minlevel, maxlevel;
    GX_GetTexObjLOD(obj, &minlevel, &maxlevel);
    info->minlevel = minlevel;
    info->maxlevel = maxlevel;
    info->ud.ptr = GX_GetTexObjUserData(obj);
    GX_GetTexObjFilterMode(obj, &info->min_filter, &info->mag_filter);

    /* Check if we wanted an alpha channel instead */
    if (info->format == GX_TF_I8 && info->ud.d.is_alpha)
        info->format = GX_TF_A8;
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    /* For the time being, all the parameters we support take integer values */
    glTexParameteri(target, pname, param);
}

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    if (target != GL_TEXTURE_2D)
        return;

    gltexture_ *currtex = &texture_list[curr_tex()];
    u8 wraps, wrapt, min_filter, mag_filter;

    switch (pname) {
    case GL_TEXTURE_WRAP_S:
        wrapt = GX_GetTexObjWrapT(&currtex->texobj);
        wraps = gcgl_texwrap_conv(param);
        GX_InitTexObjWrapMode(&currtex->texobj, wraps, wrapt);
        break;
    case GL_TEXTURE_WRAP_T:
        wraps = GX_GetTexObjWrapS(&currtex->texobj);
        wrapt = gcgl_texwrap_conv(param);
        GX_InitTexObjWrapMode(&currtex->texobj, wraps, wrapt);
        break;
    case GL_TEXTURE_MAG_FILTER:
        GX_GetTexObjFilterMode(&currtex->texobj, &min_filter, &mag_filter);
        /* Only GX_NEAR and GX_LINEAR are supported for magnification */
        mag_filter = (param == GL_NEAREST ||
                      param == GL_NEAREST_MIPMAP_NEAREST ||
                      param == GL_NEAREST_MIPMAP_LINEAR) ? GX_NEAR : GX_LINEAR;
        GX_InitTexObjFilterMode(&currtex->texobj, min_filter, mag_filter);
        break;
    case GL_TEXTURE_MIN_FILTER:
        GX_GetTexObjFilterMode(&currtex->texobj, &min_filter, &mag_filter);
        min_filter = gl_filter_to_gx(param);
        GX_InitTexObjFilterMode(&currtex->texobj, min_filter, mag_filter);
        GX_GetTexObjFilterMode(&currtex->texobj, &min_filter, &mag_filter);
        break;
    };
}

void glTexGeni(GLenum coord, GLenum pname, GLint param)
{
    /* Since in GX we cannot set different modes per texture coordinate, we
     * only look at the S coordinate, hoping that the other enabled coordinates
     * will use the same parameters. */
    if (coord != GL_S) return;

    switch (pname) {
    case GL_TEXTURE_GEN_MODE:
        glparamstate.texture_gen_mode = param;
        glparamstate.dirty.bits.dirty_texture_gen = 1;
        break;
    }
}

void glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params)
{
    switch (pname) {
    case GL_TEXTURE_GEN_MODE:
        glTexGeni(coord, pname, params[0]);
        break;
    case GL_EYE_PLANE:
        if (coord == GL_S) {
            floatcpy(glparamstate.texture_eye_plane_s, params, 4);
        } else if (coord == GL_T) {
            floatcpy(glparamstate.texture_eye_plane_t, params, 4);
        }
        glparamstate.dirty.bits.dirty_texture_gen = 1;
        break;
    case GL_OBJECT_PLANE:
        if (coord == GL_S) {
            floatcpy(glparamstate.texture_object_plane_s, params, 4);
        } else if (coord == GL_T) {
            floatcpy(glparamstate.texture_object_plane_t, params, 4);
        }
        glparamstate.dirty.bits.dirty_texture_gen = 1;
        break;
    }
}

void glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    /* For the time being, all the parameters we support take integer values */
    glTexEnvi(target, pname, param);
}

void glGetTexLevelParameteriv(GLenum target, GLint level,
                              GLenum pname, GLint *params)
{
    warning("glGetTexLevelParameteriv not implemented");
}

void glTexEnvi(GLenum target, GLenum pname, GLint param)
{
    HANDLE_CALL_LIST(TEX_ENV, target, pname, param);

    int unit = glparamstate.active_texture;
    OgxTextureUnit *tu = &glparamstate.texture_unit[unit];
    switch (pname) {
    case GL_COMBINE_ALPHA:
        tu->combine_alpha = param;
        break;
    case GL_COMBINE_RGB:
        tu->combine_rgb = param;
        break;
    case GL_OPERAND0_ALPHA:
    case GL_OPERAND1_ALPHA:
    case GL_OPERAND2_ALPHA:
        tu->operand_alpha[pname - GL_OPERAND0_ALPHA] = param;
        break;
    case GL_SOURCE0_ALPHA:
    case GL_SOURCE1_ALPHA:
    case GL_SOURCE2_ALPHA:
        tu->source_alpha[pname - GL_SOURCE0_ALPHA] = param;
        break;
    case GL_OPERAND0_RGB:
    case GL_OPERAND1_RGB:
    case GL_OPERAND2_RGB:
        tu->operand_rgb[pname - GL_OPERAND0_RGB] = param;
        break;
    case GL_SOURCE0_RGB:
    case GL_SOURCE1_RGB:
    case GL_SOURCE2_RGB:
        tu->source_rgb[pname - GL_SOURCE0_RGB] = param;
        break;
    case GL_TEXTURE_ENV_MODE:
        tu->mode = param;
        break;
    }
}

void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
    int unit = glparamstate.active_texture;
    OgxTextureUnit *tu = &glparamstate.texture_unit[unit];
    switch (pname) {
    case GL_TEXTURE_ENV_COLOR:
        tu->color = gxcol_new_fv(params);
        break;
    default:
        glTexEnvf(target, pname, params[0]);
        break;
    }
}

void glTexEnviv(GLenum target, GLenum pname, const GLint *params)
{
    switch (pname) {
    case GL_TEXTURE_ENV_COLOR:
        GLfloat p[4] = {
            scaled_int(params[0]),
            scaled_int(params[1]),
            scaled_int(params[2]),
            scaled_int(params[3]),
        };
        glTexEnvfv(target, pname, p);
        break;
    default:
        glTexEnvi(target, pname, params[0]);
        break;
    }
}

void glTexImage1D(GLenum target, GLint level, GLint internalFormat,
                  GLsizei width, GLint border, GLenum format, GLenum type,
                  const GLvoid *pixels)
{
    glTexImage2D(GL_TEXTURE_2D, level, internalFormat, width, 1,
                 border, format, type, pixels);
}

static void update_texture(const void *data, int level, GLenum format, GLenum type,
                           int width, int height,
                           GXTexObj *obj, TextureInfo *ti, int x, int y)
{
    unsigned char *dst_addr = ti->texels;
    // Inconditionally convert to 565 all inputs without alpha channel
    // Alpha inputs may be stripped if the user specifies an alpha-free internal format
    if (ti->format != GX_TF_CMPR) {
        // Calculate the offset and address of the mipmap
        uint32_t offset = calc_mipmap_offset(level, ti->width, ti->height, ti->format);
        dst_addr += offset;

        int dstpitch = _ogx_pitch_for_width(ti->format, ti->width >> level);
        _ogx_bytes_to_texture(data, format, type, width, height,
                              dst_addr, ti->format, x, y, dstpitch);
        /* GX_TF_A8 is not supported by Dolphin and it's not properly handed by
         * a real Wii either. */
        if (ti->format == GX_TF_A8) {
            ti->format = GX_TF_I8;
            ti->ud.d.is_alpha = 1; /* Remember that we wanted alpha, though */
        }
    } else {
        // Compressed texture
        if (x != 0 || y != 0 || ti->width != width) {
            warning("Update of compressed textures not implemented!");
            return;
        }

        // Calculate the offset and address of the mipmap
        uint32_t offset = calc_mipmap_offset(level, ti->width, ti->height, ti->format);
        dst_addr += offset;

        // Simplify but keep in mind the swapping
        int needswap = 0;
        if (format == GL_BGR) {
            format = GL_RGB;
            needswap = 1;
        }
        if (format == GL_BGRA) {
            format = GL_RGBA;
            needswap = 1;
        }

        _ogx_convert_rgb_image_to_DXT1((unsigned char *)data, dst_addr,
                                       width, height, needswap);
    }

    DCFlushRange(dst_addr, calc_memory(width, height, ti->format));

    // Slow but necessary! The new textures may be in the same region of some old cached textures
    GX_InvalidateTexAll();

    GX_InitTexObj(obj, ti->texels,
                  ti->width, ti->height, ti->format, ti->wraps, ti->wrapt, GX_TRUE);
    GX_InitTexObjLOD(obj, ti->min_filter, ti->mag_filter,
                     ti->minlevel, ti->maxlevel, 0, GX_ENABLE, GX_ENABLE, GX_ANISO_1);
    GX_InitTexObjUserData(obj, ti->ud.ptr);
}

void glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height,
                  GLint border, GLenum format, GLenum type, const GLvoid *data)
{
    int tex_id = curr_tex();
    // Initial checks
    if (!TEXTURE_IS_RESERVED(texture_list[tex_id]))
        return;
    if (target != GL_TEXTURE_2D)
        return; // FIXME Implement non 2D textures

    GX_DrawDone(); // Very ugly, we should have a list of used textures and only wait if we are using the curr tex.
                   // This way we are sure that we are not modifying a texture which is being drawn

    gltexture_ *currtex = &texture_list[tex_id];

    uint8_t gx_format = _ogx_find_best_gx_format(format, internalFormat,
                                                 width, height);

    // We *may* need to delete and create a new texture, depending if the user wants to add some mipmap levels
    // or wants to create a new texture from scratch
    int wi = calc_original_size(level, width);
    int he = calc_original_size(level, height);

    TextureInfo ti;
    texture_get_info(&currtex->texobj, &ti);
    ti.format = gx_format;
    ti.ud.d.is_reserved = 1;
    char onelevel = ti.minlevel == 0 && ti.maxlevel == 0;

    // Check if the texture has changed its geometry and proceed to delete it
    // If the specified level is zero, create a onelevel texture to save memory
    if (wi != ti.width || he != ti.height) {
        if (ti.texels != 0)
            free(ti.texels);
        if (level == 0) {
            uint32_t required_size = calc_memory(width, height, ti.format);
            ti.texels = memalign(32, required_size);
            onelevel = 1;
        } else {
            uint32_t required_size = calc_tex_size(wi, he, ti.format);
            ti.texels = memalign(32, required_size);
            onelevel = 0;
        }
        ti.minlevel = level;
        ti.maxlevel = level;
        ti.width = wi;
        ti.height = he;
        ti.wraps = ti.wrapt = GX_REPEAT;
    }
    if (ti.maxlevel < level)
        ti.maxlevel = level;
    if (ti.minlevel > level)
        ti.minlevel = level;

    if (onelevel == 1 && level != 0) {
        // We allocated a onelevel texture (base level 0) but now
        // we are uploading a non-zero level, so we need to create a mipmap capable buffer
        // and copy the level zero texture
        uint32_t tsize = calc_memory(wi, he, ti.format);
        unsigned char *oldbuf = ti.texels;

        uint32_t required_size = calc_tex_size(wi, he, ti.format);
        ti.texels = memalign(32, required_size);
        if (!ti.texels) {
            warning("Failed to allocate memory for texture mipmap (%d)", errno);
            set_error(GL_OUT_OF_MEMORY);
            return;
        }

        memcpy(ti.texels, oldbuf, tsize);
        free(oldbuf);
    }

    update_texture(data, level, format, type, width, height,
                   &currtex->texobj, &ti, 0, 0);
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height, GLenum format, GLenum type,
                     const GLvoid *data)
{
    int tex_id = curr_tex();
    if (!TEXTURE_IS_USED(texture_list[tex_id])) {
        set_error(GL_INVALID_OPERATION);
        return;
    }

    if (target != GL_TEXTURE_2D) {
        warning("glTexSubImage2D with target 0x%04x not supported", target);
        return;
    }

    gltexture_ *currtex = &texture_list[tex_id];

    TextureInfo ti;
    texture_get_info(&currtex->texobj, &ti);
    if (level > ti.maxlevel) {
        /* OpenGL does not specify this as an error, so we should probably
         * handle this by allocating new mipmap levels as needed. but let's
         * leave it as a TODO for now. */
        warning("glTexSubImage2D called with level %d when max is %d",
                level, ti.maxlevel);
        return;
    }

    update_texture(data, level, format, type, width, height,
                   &currtex->texobj, &ti, xoffset, yoffset);
}

void glBindTexture(GLenum target, GLuint texture)
{
    if (texture < 0 || texture >= _MAX_GL_TEX)
        return;

    HANDLE_CALL_LIST(BIND_TEXTURE, target, texture);

    if (!TEXTURE_IS_RESERVED(texture_list[texture])) {
        TEXTURE_RESERVE(texture_list[texture]);
    }

    /* We don't load the texture now, since its texels might not have been
     * defined yet. We do this when setting up the texturing TEV stage. */
    int unit = glparamstate.active_texture;
    glparamstate.texture_unit[unit].glcurtex = texture;
}

void glTexImage3D(GLenum target, GLint level, GLint internalFormat,
                  GLsizei width, GLsizei height, GLsizei depth,
                  GLint border, GLenum format, GLenum type,
                  const GLvoid *pixels)
{
    warning("glTexImage3D not implemented");
}

void glDeleteTextures(GLsizei n, const GLuint *textures)
{
    const GLuint *texlist = textures;
    GX_DrawDone();
    while (n-- > 0) {
        int i = *texlist++;
        if (!(i < 0 || i >= _MAX_GL_TEX)) {
            void *data = GX_GetTexObjData(&texture_list[i].texobj);
            if (data != 0)
                free(MEM_PHYSICAL_TO_K0(data));
            memset(&texture_list[i], 0, sizeof(texture_list[i]));
        }
    }
}

void glGenTextures(GLsizei n, GLuint *textures)
{
    GLuint *texlist = textures;
    int i;
    for (i = 0; i < _MAX_GL_TEX && n > 0; i++) {
        if (!TEXTURE_IS_RESERVED(texture_list[i])) {
            GXTexObj *texobj = &texture_list[i].texobj;
            GX_InitTexObj(texobj, NULL, 0, 0, 0, GX_REPEAT, GX_REPEAT, 0);
            TEXTURE_RESERVE(texture_list[i]);
            *texlist++ = i;
            n--;
        }
    }

    if (n > 0) {
        warning("Could not allocate %d textures", n);
        set_error(GL_OUT_OF_MEMORY);
    }
}

void glActiveTexture(GLenum texture)
{
    int index = texture - GL_TEXTURE0;
    if (index < 0 || index >= MAX_TEXTURE_UNITS) {
        set_error(GL_INVALID_ENUM);
        return;
    }
    glparamstate.active_texture = index;
}

void glClientActiveTexture(GLenum texture)
{
    int index = texture - GL_TEXTURE0;
    if (index < 0 || index >= MAX_TEXTURE_UNITS) {
        set_error(GL_INVALID_ENUM);
        return;
    }
    glparamstate.cs.active_texture = index;
}
