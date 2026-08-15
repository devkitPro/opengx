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

#define BUILDING_FBO_CODE

#include "opengx.h"

#include "debug.h"
#include "efb.h"
#include "fbo.h"
#include "state.h"
#include "texture.h"
#include "utils.h"

#include <GL/gl.h>
#include <malloc.h>

typedef struct {
    enum attachment_type_t {
        ATTACHMENT_NONE = 0,
        ATTACHMENT_TEXTURE_1D,
        ATTACHMENT_TEXTURE_2D,
        ATTACHMENT_RENDERBUFFER,
    } type;
    char mipmap_level; /* For textures only */
    int16_t object_name; /* Texture ID or render buffer ID */
    /* We'd need a couple of more fields if we supported 3D textures */
} Attachment;

struct _OgxFramebuffer {
    Attachment attachments[NUM_ATTACHMENTS];
    /* These are set with glDrawBuffer[s]() and glReadBuffer() and the meaning
     * is:
     * -1: GL_NONE
     *  n: GL_COLOR_ATTACHMENTn */
    int8_t draw_buffers[MAX_COLOR_ATTACHMENTS];
    int8_t read_buffer;
    unsigned in_use : 1;
    unsigned was_bound : 1;
};

OgxFboState _ogx_fbo_state;

static OgxFramebuffer *s_framebuffers = NULL;
static int s_draw_count_at_save = 0;
static FboType s_last_fbo_loaded = 0;

static inline OgxFramebuffer *framebuffer_from_name(GLuint name)
{
    if (!s_framebuffers || name == 0 || name > MAX_FRAMEBUFFERS) return NULL;
    return &s_framebuffers[name - 1];
}

static void attach_texture(GLenum target, GLenum attachment,
                           int attachment_type, GLuint texture, GLint level)
{
    if (target == GL_FRAMEBUFFER) target = GL_DRAW_FRAMEBUFFER;

    FboType fbo = target == GL_DRAW_FRAMEBUFFER ?
        _ogx_fbo_state.draw_target : _ogx_fbo_state.read_target;
    if (fbo == 0) {
        set_error(GL_INVALID_OPERATION);
        return;
    }

    OgxFramebuffer *fb = framebuffer_from_name(fbo);
    AttachmentIndex index;
    if (attachment >= GL_COLOR_ATTACHMENT0 &&
        attachment < GL_COLOR_ATTACHMENT0 + MAX_COLOR_ATTACHMENTS) {
        index = ATTACHMENT_COLOR0 + (attachment - GL_COLOR_ATTACHMENT0);
    } else {
        /* TODO: support depth and stencil attachments */
        warning("depth and stencil attachments not supported");
        return;
    }

    fb->attachments[index].type = attachment_type;
    fb->attachments[index].mipmap_level = level;
    fb->attachments[index].object_name = texture;
    if (target == GL_DRAW_FRAMEBUFFER) {
        _ogx_fbo_state.dirty.bits.draw_target = true;
    } else {
        _ogx_fbo_state.dirty.bits.read_target = true;
    }
}

static void set_draw_target(FboType fbo)
{
    if (_ogx_fbo_state.draw_target == fbo) return;
    _ogx_fbo_state.dirty.bits.draw_target = true; /* Force saving */
    _ogx_fbo_scene_save_from_efb(OGX_EFB_SCENE);
    _ogx_fbo_state.draw_target = fbo;
    _ogx_fbo_scene_load_into_efb();
    /* We set the viewport upside down when the draw target is a texture, so
     * make sure things are up to date. */
    glparamstate.dirty.bits.dirty_viewport = 1;
}

static void set_read_target(FboType fbo)
{
    if (_ogx_fbo_state.read_target == fbo) return;
    _ogx_fbo_state.read_target = fbo;
    _ogx_fbo_state.dirty.bits.read_target = true;
}

bool _ogx_fbo_get_integerv(GLenum pname, GLint *params)
{
    switch (pname) {
    case GL_DRAW_FRAMEBUFFER_BINDING:
        *params = _ogx_fbo_state.draw_target;
        break;
    case GL_READ_FRAMEBUFFER_BINDING:
        *params = _ogx_fbo_state.read_target;
        break;
    default:
        return false;
    }
    return true;
}

void _ogx_fbo_scene_save_from_efb(OgxEfbContentType next_content_type)
{
    if (_ogx_fbo_state.draw_target == 0) {
        _ogx_scene_save_from_efb();
    } else {
        if (s_draw_count_at_save == glparamstate.draw_count) {
            /* No new draw operations occurred: no need to save again */
            return;
        }
        if (next_content_type == OGX_EFB_SCENE &&
            s_last_fbo_loaded == _ogx_fbo_state.draw_target &&
            !_ogx_fbo_state.dirty.bits.draw_target) {
            /* Already up-to-date */
            return;
        }

        OgxFramebuffer *fb = framebuffer_from_name(_ogx_fbo_state.draw_target);
        if (!fb) return;

        /* TODO: support multiple color attachments */
        const Attachment *attachment = &fb->attachments[ATTACHMENT_COLOR0];
        if (attachment->type == ATTACHMENT_TEXTURE_1D ||
            attachment->type == ATTACHMENT_TEXTURE_2D) {
            GLuint texture_name = attachment->object_name;
            OgxTextureInfo ti;
            if (!_ogx_texture_get_info(texture_name, &ti))
                return;

            _ogx_efb_save_area_to_buffer(ti.format, 0, 0,
                                         ti.width, ti.height,
                                         ti.texels, OGX_EFB_COLOR);
            s_draw_count_at_save = glparamstate.draw_count;
        } else {
            /* TODO: renderbuffers */
        }
    }
}

void _ogx_fbo_scene_load_into_efb()
{
    /* We always restore the "draw" target, not the "read" one, because we
     * assume that read operations should check if there is a FBO attached and
     * do the reading directly from there, without passing via the EFB.
     */
    if (_ogx_fbo_state.draw_target == 0) {
        _ogx_scene_load_into_efb();
    } else {
        if (_ogx_efb_content_type == OGX_EFB_SCENE &&
            s_last_fbo_loaded == _ogx_fbo_state.draw_target &&
            !_ogx_fbo_state.dirty.bits.draw_target) {
            /* Already up-to-date */
            return;
        }
        OgxFramebuffer *fb = framebuffer_from_name(_ogx_fbo_state.draw_target);
        if (!fb) return;

        /* TODO: support multiple color attachments */
        const Attachment *attachment = &fb->attachments[ATTACHMENT_COLOR0];
        if (attachment->type == ATTACHMENT_TEXTURE_1D ||
            attachment->type == ATTACHMENT_TEXTURE_2D) {
            GLuint texture_name = attachment->object_name;
            GXTexObj texobj;
            if (!_ogx_texture_get_texobj(texture_name, &texobj))
                return;

            u32 format = GX_GetTexObjFmt(&texobj);
            uint8_t desired_efb_format =
                (format == GX_TF_RGBA8 || format == GX_TF_RGB5A3) ?
                GX_PF_RGBA6_Z24 : GX_PF_RGB8_Z24;
            _ogx_efb_set_pixel_format(desired_efb_format);

            _ogx_efb_restore_texobj(&texobj);
            /* Mark the texture as up-to-date */
            s_draw_count_at_save = glparamstate.draw_count;
        } else {
            /* TODO: renderbuffers */
        }
    }
    s_last_fbo_loaded = _ogx_fbo_state.draw_target;
    _ogx_fbo_state.dirty.all = 0;
}

GLboolean glIsFramebuffer(GLuint framebuffer)
{
    OgxFramebuffer *fb = framebuffer_from_name(framebuffer);
    return fb && fb->was_bound;
}

void glBindFramebuffer(GLenum target, GLuint framebuffer)
{
    if (framebuffer == 0) {
    } else {
        OgxFramebuffer *fb = framebuffer_from_name(framebuffer);
        if (!fb || !fb->in_use) {
            set_error(GL_INVALID_OPERATION);
            return;
        }
        fb->was_bound = true;
    }
    switch (target) {
    case GL_DRAW_FRAMEBUFFER:
        set_draw_target(framebuffer);
        break;
    case GL_FRAMEBUFFER:
        set_draw_target(framebuffer);
        // fall through
    case GL_READ_FRAMEBUFFER:
        set_read_target(framebuffer);
        break;
    }
}

void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers)
{
    if (n < 0) {
        set_error(GL_INVALID_VALUE);
        return;
    }

    for (int i = 0; i < n; i++) {
        GLuint name = framebuffers[i];
        OgxFramebuffer *fb = framebuffer_from_name(name);
        if (!fb) continue;

        if (_ogx_fbo_state.draw_target == name) {
            set_draw_target(0);
        }
        if (_ogx_fbo_state.read_target == name) {
            set_read_target(0);
        }
        memset(fb, 0, sizeof(*fb));
    }
}

void glGenFramebuffers(GLsizei n, GLuint *framebuffers)
{
    if (n < 0) {
        set_error(GL_INVALID_VALUE);
        return;
    }

    if (!s_framebuffers) {
        /* Allocate MAX_FRAMEBUFFERS at once, since they are not such big
         * objects. We can optimize this later. */
        s_framebuffers = calloc(MAX_FRAMEBUFFERS, sizeof(OgxFramebuffer));
        if (!s_framebuffers) {
            set_error(GL_OUT_OF_MEMORY);
            return;
        }
    }

    GLsizei allocated = 0;
    for (int i = 0; i < MAX_FRAMEBUFFERS && allocated < n; i++) {
        if (!s_framebuffers[i].in_use) {
            s_framebuffers[i].in_use = true;
            framebuffers[allocated++] = i + 1;
        }
    }

    if (allocated < n) {
        /* No free slots. TODO: realloc */
        set_error(GL_OUT_OF_MEMORY);
        return;
    }
}

GLenum glCheckFramebufferStatus(GLenum target)
{
    if (target == GL_FRAMEBUFFER) target = GL_DRAW_FRAMEBUFFER;

    FboType fbo = target == GL_DRAW_FRAMEBUFFER ?
        _ogx_fbo_state.draw_target : _ogx_fbo_state.read_target;
    if (fbo == 0) return GL_FRAMEBUFFER_COMPLETE;

    OgxFramebuffer *fb = framebuffer_from_name(fbo);

    for (int i = 0; i < MAX_COLOR_ATTACHMENTS; i++) {
        int8_t index = fb->draw_buffers[i];
        if (i != -1 &&
            fb->attachments[ATTACHMENT_COLOR0 +
                            index].type == ATTACHMENT_NONE) {
            return GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER;
        }
    }

    if (fb->read_buffer != -1 &&
        fb->attachments[ATTACHMENT_COLOR0 +
                        fb->read_buffer].type == ATTACHMENT_NONE) {
        return GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER;
    }

    /* TODO: check that the size of the attached texture is non-zero, and that
     * the texture format is suitable for the data bound to it. */
    return GL_FRAMEBUFFER_COMPLETE;
}

void glFramebufferTexture1D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
    if (textarget != GL_TEXTURE_1D) {
        set_error(GL_INVALID_ENUM);
        return;
    }

    attach_texture(target, attachment, ATTACHMENT_TEXTURE_1D, texture, level);
}

void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
    if (textarget != GL_TEXTURE_2D) {
        set_error(GL_INVALID_ENUM);
        return;
    }

    attach_texture(target, attachment, ATTACHMENT_TEXTURE_2D, texture, level);
}

void glFramebufferTexture3D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint zoffset)
{
    warning("glFramebufferTexture3D is unsupported");
    set_error(GL_INVALID_OPERATION);
}

void ogx_enable_module_fbo() {
    _ogx_add_extension("GL_EXT_framebuffer_object");
}

/* FBOs are in OpenGL 3.0, and in older versions as an extension */
#define PROC(name) \
    { #name, name }, \
    { #name "EXT", name }
static const OgxProcMap s_proc_map[] = {
    PROC(glBindFramebuffer),
    PROC(glCheckFramebufferStatus),
    PROC(glDeleteFramebuffers),
    PROC(glFramebufferTexture1D),
    PROC(glFramebufferTexture2D),
    PROC(glFramebufferTexture3D),
    PROC(glGenFramebuffers),
    PROC(glIsFramebuffer),
};
#define NUM_PROCS (sizeof(s_proc_map) / sizeof(s_proc_map[0]))

OgxFunctions _ogx_fbo_functions = {
    NUM_PROCS,
    s_proc_map,
};
