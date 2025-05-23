
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

*****************************************************************************

             BASIC WII/GC OPENGL-LIKE IMPLEMENTATION

     This is a very basic OGL-like implementation. Don't expect any
     advanced (or maybe basic) features from the OGL spec.
     The support is very limited in some cases, you shoud read the
     README file which comes with the source to have an idea of the
     limits and how you can tune or modify this file to adapt the
     source to your neeeds.
     Take in mind this is not very fast. The code is intended to be
     tiny and much as portable as possible and easy to compile so
     there's lot of room for improvement.

*****************************************************************************/

#include "accum.h"
#include "call_lists.h"
#include "clip.h"
#include "debug.h"
#include "efb.h"
#include "gpu_resources.h"
#include "opengx.h"
#include "selection.h"
#include "shader.h"
#include "state.h"
#include "stencil.h"
#include "texture_gen_sw.h"
#include "texture_unit.h"
#include "utils.h"
#include "vbo.h"

#include <GL/gl.h>
#include <gctypes.h>
#include <malloc.h>
#include <math.h>
#include <ogc/machine/processor.h>
#include <stdlib.h>
#include <string.h>

glparams_ _ogx_state;

typedef struct
{
    uint8_t ambient_mask;
    uint8_t diffuse_mask;
    uint8_t specular_mask;
} LightMasks;

char _ogx_log_level = 0;
uint16_t _ogx_draw_sync_token = 0;
uint16_t _ogx_draw_sync_token_received = 0;
static OgxEfbBuffer *s_efb_scene_buffer = NULL;
static GXTexObj s_zbuffer_texture;
static uint8_t s_zbuffer_texels[2 * 32] ATTRIBUTE_ALIGN(32);
static bool s_point_sprites_was_enabled = false;
/* Force the inclusion of functions.c's TU in the build when GL functions are
 * used. In this way, if a client library (such as SDL) defines weak symbols
 * for the opengx functions it uses, a client application which actually uses
 * opengx will link and use its real implementation; at the same time, a client
 * which does not use OpenGL is not forced to link with opengx. */
extern int _ogx_functions_c;
void *_ogx_force_proctable = &_ogx_functions_c;

typedef void (*ProcessVertex)(int index);

static inline void update_modelview_matrix()
{
    GX_LoadPosMtxImm(glparamstate.modelview_matrix, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);
}

/* Deduce the projection type (perspective vs orthogonal) and the values of the
 * near and far clipping plane from the projection matrix. */
static void get_projection_info(const Mtx44 matrix, u8 *type, float *near, float *far)
{
    float A, B;

    A = matrix[2][2];
    B = matrix[2][3];

    if (matrix[3][3] == 0) {
        *type = GX_PERSPECTIVE;
        *near = B / (A - 1.0f);
        if (A != -1.0f) {
            *far = B / (A + 1.0f);
        } else {
            *far = 1.0f;
        }
    } else {
        *type = GX_ORTHOGRAPHIC;
        *near = (B + 1.0f) / A;
        *far = (B - 1.0f) / A;
    }
}

void _ogx_set_projection(const Mtx44 matrix)
{
    /* OpenGL's projection matrix transform the scene into a clip space where
     * all the coordinates lie in the range [-1, 1]. Nintendo's GX, however,
     * for the z coordinates expects a range of [-1, 0], so the projection
     * matrix needs to be adjusted. We do that by extracting the near and far
     * planes from the GL projection matrix and by recomputing the related two
     * matrix entries according to the formulas used by guFrustum() and
     * guOrtho(). */
    Mtx44 proj;
    u8 type;
    float near, far;
    get_projection_info(matrix, &type, &near, &far);
    memcpy(proj, matrix, sizeof(Mtx44));
    float tmp = 1.0f / (far - near);
    /* TODO: also use the polygon_offset_factor variable */
    float zoffset = glparamstate.polygon_offset_fill ?
        (glparamstate.polygon_offset_units * 0.00001f) : 0.0;
    if (type == GX_ORTHOGRAPHIC) {
        proj[2][2] = -tmp;
        proj[2][3] = -far * tmp + zoffset;
    } else {
        proj[2][2] = -near * tmp;
        proj[2][3] = -near * far * tmp + zoffset;
    }
    GX_LoadProjectionMtx(proj, type);
}

static inline void update_projection_matrix()
{
    _ogx_set_projection(*glparamstate.proj_ptr);
}

static inline void update_normal_matrix()
{
    Mtx mvinverse, normalm;
    guMtxInverse(glparamstate.modelview_matrix, mvinverse);
    guMtxTranspose(mvinverse, normalm);
    GX_LoadNrmMtxImm(normalm, GX_PNMTX0);
}

static void setup_cull_mode()
{
    if (glparamstate.cullenabled) {
        switch (glparamstate.glcullmode) {
        case GL_FRONT:
            if (glparamstate.frontcw)
                GX_SetCullMode(GX_CULL_FRONT);
            else
                GX_SetCullMode(GX_CULL_BACK);
            break;
        case GL_BACK:
            if (glparamstate.frontcw)
                GX_SetCullMode(GX_CULL_BACK);
            else
                GX_SetCullMode(GX_CULL_FRONT);
            break;
        case GL_FRONT_AND_BACK:
            GX_SetCullMode(GX_CULL_ALL);
            break;
        }
    } else {
        GX_SetCullMode(GX_CULL_NONE);
    }
}

static void update_scissor()
{
    int *params, y;
    if (glparamstate.scissor_enabled) {
        params = glparamstate.scissor;
        /* Take into account that OpenGL Y coordinates are inverted */
        y = glparamstate.viewport[3] - (params[3] + params[1]);
    } else {
        params = glparamstate.viewport;
        y = params[1];
    }
    GX_SetScissor(params[0], y, params[2], params[3]);

    glparamstate.dirty.bits.dirty_scissor = 0;
}

static void update_viewport()
{
    int x = glparamstate.viewport[0];
    int y = glparamstate.viewport[1];
    int width = glparamstate.viewport[2];
    int height = glparamstate.viewport[3];
    if (_ogx_fbo_state.draw_target != 0) {
        /* When rendering to a texture, we need to flip the picture vertically,
         * since OpenGL textures have the Y coordinate growing from the bottom.
         * It would be nicer if GX provided a way to flip the data during the
         * GX_CopyTex() operation, but alas, it doesn't. */
        y = height - y;
        height = -height;
    }
    GX_SetViewport(x, y, width, height, 0.0f, 1.0f);
    glparamstate.dirty.bits.dirty_viewport = 0;
}

int ogx_enable_double_buffering(int double_buffering)
{
    int had_double_buffering = glparamstate.active_buffer == GL_BACK;
    glparamstate.active_buffer = double_buffering ? GL_BACK : GL_FRONT;
    return had_double_buffering;
}

int ogx_prepare_swap_buffers()
{
    if (glparamstate.render_mode != GL_RENDER) return -1;
    _ogx_draw_sync_token = 0;
    GX_SetDrawSync(0);
    _ogx_vbo_clear_unbound_buffers();
    return 0;
}

static int parse_hints()
{
    OgxHints hints = OGX_HINT_NONE;

    /* comma separated list of operations for which a faster (but inaccurate)
     * implementation is to be preferred over a more standard-compliant one.
     * By default, we always prefer standard-compliance over speed. */
    const char *env = getenv("OPENGX_FAST_OPS");
    if (env) {
        if (strstr(env, "sphere_map") != NULL)
            hints |= OGX_HINT_FAST_SPHERE_MAP;
    }

    glparamstate.hints = hints;
}

static void draw_sync_callback(u16 token)
{
    _ogx_draw_sync_token_received = token;
}

void ogx_initialize()
{
    _ogx_log_init();

    _ogx_gpu_resources_init();
    parse_hints();

    glparamstate.current_call_list.index = -1;
    GX_SetDispCopyGamma(GX_GM_1_0);
    int i;

    glparamstate.blendenabled = 0;
    glparamstate.srcblend = GX_BL_ONE;
    glparamstate.dstblend = GX_BL_ZERO;

    glparamstate.clear_color.r = 0; // Black as default
    glparamstate.clear_color.g = 0;
    glparamstate.clear_color.b = 0;
    glparamstate.clear_color.a = 1;
    glparamstate.accum_clear_color.r = 0;
    glparamstate.accum_clear_color.g = 0;
    glparamstate.accum_clear_color.b = 0;
    glparamstate.accum_clear_color.a = 0;
    glparamstate.clearz = 1.0f;

    glparamstate.ztest = GX_FALSE; // Depth test disabled but z write enabled
    glparamstate.zfunc = GX_LESS;  // Although write is efectively disabled
    glparamstate.zwrite = GX_TRUE; // unless test is enabled

    glparamstate.matrixmode = 1; // Modelview default mode
    GX_SetNumChans(1);           // One modulation color (as glColor)
    glDisable(GL_TEXTURE_2D);

    glparamstate.glcullmode = GL_BACK;
    glparamstate.render_mode = GL_RENDER;
    glparamstate.cullenabled = 0;
    glparamstate.polygon_mode = GL_FILL;
    glparamstate.color_update = true;
    glparamstate.alpha_func = GX_ALWAYS;
    glparamstate.alpha_ref = 0;
    glparamstate.alphatest_enabled = 0;
    glparamstate.frontcw = 0; // By default front is CCW
    for (int i = 0; i < MAX_TEXTURE_UNITS; i++) {
        OgxTextureUnit *tu = &glparamstate.texture_unit[i];
        tu->mode = GL_MODULATE;
        tu->combine_rgb = tu->combine_alpha = GL_MODULATE;
        tu->source_rgb[0] = tu->source_alpha[0] = GL_TEXTURE;
        tu->source_rgb[1] = tu->source_alpha[1] = GL_PREVIOUS;
        tu->source_rgb[2] = tu->source_alpha[2] = GL_CONSTANT;
        tu->operand_rgb[0] = tu->operand_rgb[1] = GL_SRC_COLOR;
        tu->operand_alpha[0] = tu->operand_alpha[1] = GL_SRC_ALPHA;
        // This is not a mistake, op 2 RGB is also SRC_ALPHA!
        tu->operand_rgb[2] = tu->operand_alpha[2] = GL_SRC_ALPHA;
        tu->color.r = tu->color.g = tu->color.b = tu->color.a = 0;

        tu->matrix_index = 0;
        guMtxIdentity(tu->matrix[0]);

        tu->gen_mode = GL_EYE_LINEAR;
        tu->gen_enabled = 0;
        /* All the other plane elements should be set to 0.0f */
        tu->texture_eye_plane_s[0] = 1.0f;
        tu->texture_eye_plane_t[1] = 1.0f;
        tu->texture_object_plane_s[0] = 1.0f;
        tu->texture_object_plane_t[1] = 1.0f;
    }
    glparamstate.active_texture = 0;
    glparamstate.point_sprites_enabled = 0;
    glparamstate.point_sprites_coord_replace = 0;

    glparamstate.cur_proj_mat = -1;
    glparamstate.cur_modv_mat = -1;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glparamstate.mv_ptr = &glparamstate.modelview_matrix;
    glparamstate.proj_ptr = &glparamstate.projection_matrix;
    glparamstate.update_matrices = _ogx_update_matrices_fixed_pipeline;

    glparamstate.scissor[0] = glparamstate.scissor[1] = 0;
    /* Scissor width and height are initialized when a window is attached */
    glparamstate.scissor[2] = glparamstate.scissor[3] = -1;
    glparamstate.scissor_enabled = 0;

    glparamstate.imm_mode.current_color[0] = 1.0f; // Default imm data, could be wrong
    glparamstate.imm_mode.current_color[1] = 1.0f;
    glparamstate.imm_mode.current_color[2] = 1.0f;
    glparamstate.imm_mode.current_color[3] = 1.0f;
    memset(glparamstate.imm_mode.current_texcoord, 0,
           sizeof(glparamstate.imm_mode.current_texcoord));
    glparamstate.imm_mode.current_normal[0] = 0;
    glparamstate.imm_mode.current_normal[1] = 0;
    glparamstate.imm_mode.current_normal[2] = 1.0f;
    glparamstate.imm_mode.current_numverts = 0;
    glparamstate.imm_mode.in_gl_begin = 0;

    glparamstate.cs.as_int = 0; // DisableClientState on everything

    glparamstate.texture_enabled = 0;
    glparamstate.pack_alignment = 4;
    glparamstate.unpack_alignment = 4;

    glparamstate.raster_pos[0] = 0.0f;
    glparamstate.raster_pos[1] = 0.0f;
    glparamstate.raster_pos[2] = 0.0f;
    glparamstate.raster_pos[3] = 1.0f;
    glparamstate.raster_pos_valid = true;
    glparamstate.pixel_zoom_x = 1.0f;
    glparamstate.pixel_zoom_y = 1.0f;

    glparamstate.depth_near = 0.0f;
    glparamstate.depth_far = 1.0f;

    glparamstate.pixel_maps = NULL;

    // Set up lights default states
    glparamstate.lighting.enabled = 0;
    for (i = 0; i < MAX_LIGHTS; i++) {
        glparamstate.lighting.lights[i].enabled = false;

        glparamstate.lighting.lights[i].atten[0] = 1;
        glparamstate.lighting.lights[i].atten[1] = 0;
        glparamstate.lighting.lights[i].atten[2] = 0;

        /* The default value for light position is (0, 0, 1), but since it's a
         * directional light we need to transform it to 100000. */
        glparamstate.lighting.lights[i].position[0] = 0;
        glparamstate.lighting.lights[i].position[1] = 0;
        glparamstate.lighting.lights[i].position[2] = 100000;
        glparamstate.lighting.lights[i].position[3] = 0;

        glparamstate.lighting.lights[i].direction[0] = 0;
        glparamstate.lighting.lights[i].direction[1] = 0;
        glparamstate.lighting.lights[i].direction[2] = -1;

        glparamstate.lighting.lights[i].spot_direction[0] = 0;
        glparamstate.lighting.lights[i].spot_direction[1] = 0;
        glparamstate.lighting.lights[i].spot_direction[2] = -1;

        glparamstate.lighting.lights[i].ambient_color[0] = 0;
        glparamstate.lighting.lights[i].ambient_color[1] = 0;
        glparamstate.lighting.lights[i].ambient_color[2] = 0;
        glparamstate.lighting.lights[i].ambient_color[3] = 1;

        if (i == 0) {
            glparamstate.lighting.lights[i].diffuse_color[0] = 1;
            glparamstate.lighting.lights[i].diffuse_color[1] = 1;
            glparamstate.lighting.lights[i].diffuse_color[2] = 1;

            glparamstate.lighting.lights[i].specular_color[0] = 1;
            glparamstate.lighting.lights[i].specular_color[1] = 1;
            glparamstate.lighting.lights[i].specular_color[2] = 1;
        } else {
            glparamstate.lighting.lights[i].diffuse_color[0] = 0;
            glparamstate.lighting.lights[i].diffuse_color[1] = 0;
            glparamstate.lighting.lights[i].diffuse_color[2] = 0;

            glparamstate.lighting.lights[i].specular_color[0] = 0;
            glparamstate.lighting.lights[i].specular_color[1] = 0;
            glparamstate.lighting.lights[i].specular_color[2] = 0;
        }
        glparamstate.lighting.lights[i].diffuse_color[3] = 1;
        glparamstate.lighting.lights[i].specular_color[3] = 1;

        glparamstate.lighting.lights[i].spot_cutoff = 180.0f;
        glparamstate.lighting.lights[i].spot_exponent = 0;
    }

    glparamstate.lighting.globalambient[0] = 0.2f;
    glparamstate.lighting.globalambient[1] = 0.2f;
    glparamstate.lighting.globalambient[2] = 0.2f;
    glparamstate.lighting.globalambient[3] = 1.0f;

    glparamstate.lighting.matambient[0] = 0.2f;
    glparamstate.lighting.matambient[1] = 0.2f;
    glparamstate.lighting.matambient[2] = 0.2f;
    glparamstate.lighting.matambient[3] = 1.0f;

    glparamstate.lighting.matdiffuse[0] = 0.8f;
    glparamstate.lighting.matdiffuse[1] = 0.8f;
    glparamstate.lighting.matdiffuse[2] = 0.8f;
    glparamstate.lighting.matdiffuse[3] = 1.0f;

    glparamstate.lighting.matemission[0] = 0.0f;
    glparamstate.lighting.matemission[1] = 0.0f;
    glparamstate.lighting.matemission[2] = 0.0f;
    glparamstate.lighting.matemission[3] = 1.0f;

    glparamstate.lighting.matspecular[0] = 0.0f;
    glparamstate.lighting.matspecular[1] = 0.0f;
    glparamstate.lighting.matspecular[2] = 0.0f;
    glparamstate.lighting.matspecular[3] = 1.0f;
    glparamstate.lighting.matshininess = 0.0f;

    glparamstate.lighting.color_material_enabled = 0;
    glparamstate.lighting.color_material_mode = GL_AMBIENT_AND_DIFFUSE;

    glparamstate.fog.enabled = false;
    glparamstate.fog.mode = GL_EXP;
    glparamstate.fog.color[0] = 0.0f;
    glparamstate.fog.color[1] = 0.0f;
    glparamstate.fog.color[2] = 0.0f;
    glparamstate.fog.color[3] = 0.0f;
    glparamstate.fog.density = 1.0f;
    glparamstate.fog.start = 0.0f;
    glparamstate.fog.end = 1.0f;

    glparamstate.stencil.enabled = false;
    glparamstate.stencil.func = GX_ALWAYS;
    glparamstate.stencil.ref = 0;
    glparamstate.stencil.mask = 0xff;
    glparamstate.stencil.wmask = 0xff;
    glparamstate.stencil.clear = 0;
    glparamstate.stencil.op_fail = GL_KEEP;
    glparamstate.stencil.op_zfail = GL_KEEP;
    glparamstate.stencil.op_zpass = GL_KEEP;

    glparamstate.active_buffer = GL_BACK;

    glparamstate.current_program = 0;

    glparamstate.error = GL_NO_ERROR;
    glparamstate.draw_count = 0;

    // Setup data types for every possible attribute

    // Typical straight float
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);

    // Mark all the hardware data as dirty, so it will be recalculated
    // and uploaded again to the hardware
    glparamstate.dirty.all = ~0;

    /* Initialize the Z-buffer 1x1 texture that we use in glClear() */
    GX_InitTexObj(&s_zbuffer_texture, s_zbuffer_texels, 1, 1,
                  GX_TF_Z24X8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&s_zbuffer_texture, GX_NEAR, GX_NEAR,
                     0.0f, 0.0f, 0.0f, 0, 0, GX_ANISO_1);

    /* Bind default texture */
    glBindTexture(GL_TEXTURE_2D, 0);

    GX_SetDrawSyncCallback(draw_sync_callback);

    _ogx_shader_initialize();
}

void _ogx_setup_2D_projection()
{
    GX_SetCurrentMtx(GX_IDENTITY);

    Mtx44 proj;
    /* The 0.5f is to center the drawing into the pixels. */
    float left = glparamstate.viewport[0] - 0.5f;
    float top = glparamstate.viewport[1] - 0.5f;
    guOrtho(proj,
            top, top + glparamstate.viewport[3],
            left, left + glparamstate.viewport[2],
            glparamstate.depth_near, glparamstate.depth_far);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);

    glparamstate.dirty.bits.dirty_matrices = 1;
}

void _ogx_setup_3D_projection()
{
    /* Assume that the modelview matrix has already been updated to GX_PNMTX0
     */
    GX_SetCurrentMtx(GX_PNMTX0);
    update_projection_matrix();
}

/* This is called by _ogx_fbo_scene_save_from_efb() */
void _ogx_scene_save_from_efb()
{
    _ogx_efb_buffer_prepare(&s_efb_scene_buffer, GX_TF_RGBA8);
    if (s_efb_scene_buffer->draw_count == glparamstate.draw_count) return;

    GX_DrawDone();
    _ogx_efb_buffer_save(s_efb_scene_buffer, OGX_EFB_COLOR);
    s_efb_scene_buffer->draw_count = glparamstate.draw_count;
}

/* This is called by _ogx_fbo_scene_load_into_efb() */
void _ogx_scene_load_into_efb()
{
    if (!s_efb_scene_buffer) return;
    if (_ogx_efb_content_type == OGX_EFB_SCENE &&
        s_efb_scene_buffer->draw_count == glparamstate.draw_count) {
        /* Up to date */
        return;
    }
    _ogx_efb_set_pixel_format(GX_PF_RGB8_Z24);
    if (glparamstate.dirty.bits.dirty_viewport) {
        update_viewport();
    }
    _ogx_efb_restore_texobj(&s_efb_scene_buffer->texobj);
    s_efb_scene_buffer->draw_count = glparamstate.draw_count;
    _ogx_setup_3D_projection();
}

/* This function might fit best in efb.c, but since it uses symbols from other
 * files, it's better to define it here to avoid cross-dependencies (which are
 * mostly harmless, but not clean). */
void _ogx_efb_set_content_type_real(OgxEfbContentType content_type)
{
    /* Save existing EFB contents, if needed */
    switch (_ogx_efb_content_type) {
    case OGX_EFB_SCENE:
        _ogx_fbo_scene_save_from_efb(content_type);
        break;
    case OGX_EFB_STENCIL:
        _ogx_stencil_save_from_efb();
        break;
    case OGX_EFB_ACCUM:
        _ogx_accum_save_from_efb();
        break;
    }

    /* Restore data from previously stored EFB for this content type */
    switch (content_type) {
    case OGX_EFB_SCENE:
        _ogx_fbo_scene_load_into_efb();
        break;
    case OGX_EFB_STENCIL:
        _ogx_stencil_load_into_efb();
        break;
    case OGX_EFB_ACCUM:
        _ogx_accum_load_into_efb();
        break;
    }
    _ogx_efb_content_type = content_type;
}

void glDrawBuffer(GLenum mode)
{
    if (mode != glparamstate.active_buffer) {
        warning("Change the read or write buffer is not implemented");
        set_error(GL_INVALID_OPERATION);
    }
}

void glReadBuffer(GLenum mode)
{
    /* We currently don't support changing read/write buffers, so the
     * implementation can be the same. */
    glDrawBuffer(mode);
}

void glEnable(GLenum cap)
{ // TODO
    HANDLE_CALL_LIST(ENABLE, cap);

    switch (cap) {
    case GL_SCISSOR_TEST:
        glparamstate.scissor_enabled = 1;
        glparamstate.dirty.bits.dirty_scissor = 1;
        break;
    case GL_TEXTURE_2D:
        glparamstate.texture_enabled |= (1 << glparamstate.active_texture);
        glparamstate.dirty.bits.dirty_attributes = 1;
        glparamstate.dirty.bits.dirty_tev = 1;
        break;
    case GL_TEXTURE_GEN_S:
    case GL_TEXTURE_GEN_T:
    case GL_TEXTURE_GEN_R:
    case GL_TEXTURE_GEN_Q:
        {
            OgxTextureUnit *tu = active_tex_unit();
            tu->gen_enabled |= (1 << (cap - GL_TEXTURE_GEN_S));
        }
        glparamstate.dirty.bits.dirty_attributes = 1;
        glparamstate.dirty.bits.dirty_tev = 1;
        break;
    case GL_COLOR_MATERIAL:
        glparamstate.lighting.color_material_enabled = 1;
        glparamstate.dirty.bits.dirty_tev = 1;
        break;
    case GL_CULL_FACE:
        glparamstate.cullenabled = 1;
        glparamstate.dirty.bits.dirty_cull = 1;
        break;
    case GL_ALPHA_TEST:
        glparamstate.alphatest_enabled = 1;
        glparamstate.dirty.bits.dirty_alphatest = 1;
        break;
    case GL_BLEND:
        glparamstate.blendenabled = 1;
        glparamstate.dirty.bits.dirty_blend = 1;
        break;
    case GL_CLIP_PLANE0:
    case GL_CLIP_PLANE1:
    case GL_CLIP_PLANE2:
    case GL_CLIP_PLANE3:
    case GL_CLIP_PLANE4:
    case GL_CLIP_PLANE5:
        _ogx_clip_enabled(cap - GL_CLIP_PLANE0);
        break;
    case GL_DEPTH_TEST:
        glparamstate.ztest = GX_TRUE;
        glparamstate.dirty.bits.dirty_z = 1;
        break;
    case GL_STENCIL_TEST:
        _ogx_stencil_enabled();
        break;
    case GL_FOG:
        glparamstate.fog.enabled = 1;
        glparamstate.dirty.bits.dirty_fog = 1;
        break;
    case GL_LIGHTING:
        glparamstate.lighting.enabled = 1;
        glparamstate.dirty.bits.dirty_tev = 1;
        break;
    case GL_LIGHT0:
    case GL_LIGHT1:
    case GL_LIGHT2:
    case GL_LIGHT3:
        glparamstate.lighting.lights[cap - GL_LIGHT0].enabled = 1;
        glparamstate.dirty.bits.dirty_tev = 1;
        break;
    case GL_POINT_SPRITE:
        glparamstate.point_sprites_enabled = 1;
        glparamstate.dirty.bits.dirty_attributes = 1;
        break;
    case GL_POLYGON_OFFSET_FILL:
        glparamstate.polygon_offset_fill = 1;
        glparamstate.dirty.bits.dirty_matrices = 1;
        break;
    default:
        break;
    }
}

void glDisable(GLenum cap)
{ // TODO
    HANDLE_CALL_LIST(DISABLE, cap);

    switch (cap) {
    case GL_SCISSOR_TEST:
        glparamstate.scissor_enabled = 0;
        glparamstate.dirty.bits.dirty_scissor = 1;
        break;
    case GL_TEXTURE_2D:
        glparamstate.texture_enabled &= ~(1 << glparamstate.active_texture);
        glparamstate.dirty.bits.dirty_attributes = 1;
        glparamstate.dirty.bits.dirty_tev = 1;
        break;
    case GL_TEXTURE_GEN_S:
    case GL_TEXTURE_GEN_T:
    case GL_TEXTURE_GEN_R:
    case GL_TEXTURE_GEN_Q:
        {
            OgxTextureUnit *tu = active_tex_unit();
            tu->gen_enabled &= ~(1 << (cap - GL_TEXTURE_GEN_S));
        }
        glparamstate.dirty.bits.dirty_attributes = 1;
        glparamstate.dirty.bits.dirty_tev = 1;
        break;
    case GL_COLOR_MATERIAL:
        glparamstate.lighting.color_material_enabled = 0;
        glparamstate.dirty.bits.dirty_tev = 1;
        break;
    case GL_CULL_FACE:
        glparamstate.cullenabled = 0;
        glparamstate.dirty.bits.dirty_cull = 1;
        break;
    case GL_ALPHA_TEST:
        glparamstate.alphatest_enabled = 0;
        glparamstate.dirty.bits.dirty_alphatest = 1;
        break;
    case GL_BLEND:
        glparamstate.blendenabled = 0;
        glparamstate.dirty.bits.dirty_blend = 1;
        break;
    case GL_CLIP_PLANE0:
    case GL_CLIP_PLANE1:
    case GL_CLIP_PLANE2:
    case GL_CLIP_PLANE3:
    case GL_CLIP_PLANE4:
    case GL_CLIP_PLANE5:
        _ogx_clip_disabled(cap - GL_CLIP_PLANE0);
        break;
    case GL_DEPTH_TEST:
        glparamstate.ztest = GX_FALSE;
        glparamstate.dirty.bits.dirty_z = 1;
        break;
    case GL_STENCIL_TEST:
        _ogx_stencil_disabled();
        break;
    case GL_LIGHTING:
        glparamstate.lighting.enabled = 0;
        glparamstate.dirty.bits.dirty_tev = 1;
        break;
    case GL_LIGHT0:
    case GL_LIGHT1:
    case GL_LIGHT2:
    case GL_LIGHT3:
        glparamstate.lighting.lights[cap - GL_LIGHT0].enabled = 0;
        glparamstate.dirty.bits.dirty_tev = 1;
        break;
    case GL_POINT_SPRITE:
        glparamstate.point_sprites_enabled = 0;
        break;
    case GL_POLYGON_OFFSET_FILL:
        glparamstate.polygon_offset_fill = 0;
        glparamstate.dirty.bits.dirty_matrices = 1;
        break;
    default:
        break;
    }
}

void glFogf(GLenum pname, GLfloat param)
{
    switch (pname) {
    case GL_FOG_MODE:
        glFogi(pname, (int)param);
        break;
    case GL_FOG_DENSITY:
        glparamstate.fog.density = param;
        break;
    case GL_FOG_START:
        glparamstate.fog.start = param;
        break;
    case GL_FOG_END:
        glparamstate.fog.end = param;
        break;
    }
    glparamstate.dirty.bits.dirty_fog = 1;
}

void glFogi(GLenum pname, GLint param)
{
    switch (pname) {
    case GL_FOG_MODE:
        glparamstate.fog.mode = param;
        break;
    case GL_FOG_DENSITY:
    case GL_FOG_START:
    case GL_FOG_END:
        glFogf(pname, param);
        return;
    }
    glparamstate.dirty.bits.dirty_fog = 1;
}

void glFogfv(GLenum pname, const GLfloat *params)
{
    switch (pname) {
    case GL_FOG_MODE:
    case GL_FOG_DENSITY:
    case GL_FOG_START:
    case GL_FOG_END:
        glFogf(pname, params[0]);
        return;
    case GL_FOG_COLOR:
        floatcpy(glparamstate.fog.color, params, 4);
        break;
    }
    glparamstate.dirty.bits.dirty_fog = 1;
}

void glLightf(GLenum light, GLenum pname, GLfloat param)
{
    HANDLE_CALL_LIST(LIGHT, light, pname, &param);

    int lnum = light - GL_LIGHT0;

    switch (pname) {
    case GL_CONSTANT_ATTENUATION:
        glparamstate.lighting.lights[lnum].atten[0] = param;
        break;
    case GL_LINEAR_ATTENUATION:
        glparamstate.lighting.lights[lnum].atten[1] = param;
        break;
    case GL_QUADRATIC_ATTENUATION:
        glparamstate.lighting.lights[lnum].atten[2] = param;
        break;
    case GL_SPOT_CUTOFF:
        glparamstate.lighting.lights[lnum].spot_cutoff = param;
        break;
    case GL_SPOT_EXPONENT:
        glparamstate.lighting.lights[lnum].spot_exponent = (int)param;
        break;
    default:
        break;
    }
    glparamstate.dirty.bits.dirty_tev = 1;
}

void glLightfv(GLenum light, GLenum pname, const GLfloat *params)
{
    HANDLE_CALL_LIST(LIGHT, light, pname, params);

    int lnum = light - GL_LIGHT0;
    switch (pname) {
    case GL_SPOT_DIRECTION:
        floatcpy(glparamstate.lighting.lights[lnum].spot_direction, params, 3);
        break;
    case GL_POSITION:
        if (params[3] == 0) {
            // Push the light far away, calculate the direction and normalize it
            glparamstate.lighting.lights[lnum].position[0] = params[0] * 100000;
            glparamstate.lighting.lights[lnum].position[1] = params[1] * 100000;
            glparamstate.lighting.lights[lnum].position[2] = params[2] * 100000;
        } else {
            glparamstate.lighting.lights[lnum].position[0] = params[0];
            glparamstate.lighting.lights[lnum].position[1] = params[1];
            glparamstate.lighting.lights[lnum].position[2] = params[2];
        }
        glparamstate.lighting.lights[lnum].position[3] = params[3];
        guVecMultiply(glparamstate.modelview_matrix,
                      (guVector *)glparamstate.lighting.lights[lnum].position,
                      (guVector *)glparamstate.lighting.lights[lnum].position);
        break;
    case GL_DIFFUSE:
        floatcpy(glparamstate.lighting.lights[lnum].diffuse_color, params, 4);
        break;
    case GL_AMBIENT:
        floatcpy(glparamstate.lighting.lights[lnum].ambient_color, params, 4);
        break;
    case GL_SPECULAR:
        floatcpy(glparamstate.lighting.lights[lnum].specular_color, params, 4);
        break;
    }
    glparamstate.dirty.bits.dirty_tev = 1;
}

void glLightModelfv(GLenum pname, const GLfloat *params)
{
    switch (pname) {
    case GL_LIGHT_MODEL_AMBIENT:
        floatcpy(glparamstate.lighting.globalambient, params, 4);
        break;
    }
    glparamstate.dirty.bits.dirty_tev = 1;
};

void glMaterialf(GLenum face, GLenum pname, GLfloat param)
{
    glMaterialfv(face, pname, &param);
}

void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
    HANDLE_CALL_LIST(MATERIAL, face, pname, params);

    switch (pname) {
    case GL_DIFFUSE:
        floatcpy(glparamstate.lighting.matdiffuse, params, 4);
        break;
    case GL_AMBIENT:
        floatcpy(glparamstate.lighting.matambient, params, 4);
        break;
    case GL_AMBIENT_AND_DIFFUSE:
        floatcpy(glparamstate.lighting.matambient, params, 4);
        floatcpy(glparamstate.lighting.matdiffuse, params, 4);
        break;
    case GL_EMISSION:
        floatcpy(glparamstate.lighting.matemission, params, 4);
        break;
    case GL_SPECULAR:
        floatcpy(glparamstate.lighting.matspecular, params, 4);
        break;
    case GL_SHININESS:
        glparamstate.lighting.matshininess = params[0];
        break;
    default:
        break;
    }
    glparamstate.dirty.bits.dirty_tev = 1;
};

void glColorMaterial(GLenum face, GLenum mode)
{
    /* TODO: support the face parameter */
    glparamstate.lighting.color_material_mode = mode;
    glparamstate.dirty.bits.dirty_tev = 1;
}

void glPixelStoref(GLenum pname, GLfloat param)
{
    glPixelStorei(pname, param);
}

void glPixelStorei(GLenum pname, GLint param)
{
    switch (pname) {
    case GL_PACK_SWAP_BYTES:
        glparamstate.pack_swap_bytes = param;
        break;
    case GL_PACK_LSB_FIRST:
        glparamstate.pack_lsb_first = param;
        break;
    case GL_PACK_ROW_LENGTH:
        glparamstate.pack_row_length = param;
        break;
    case GL_PACK_IMAGE_HEIGHT:
        glparamstate.pack_image_height = param;
        break;
    case GL_PACK_SKIP_ROWS:
        glparamstate.pack_skip_rows = param;
        break;
    case GL_PACK_SKIP_PIXELS:
        glparamstate.pack_skip_pixels = param;
        break;
    case GL_PACK_SKIP_IMAGES:
        glparamstate.pack_skip_images = param;
        break;
    case GL_PACK_ALIGNMENT:
        glparamstate.pack_alignment = param;
        break;
    case GL_UNPACK_SWAP_BYTES:
        glparamstate.unpack_swap_bytes = param;
        break;
    case GL_UNPACK_LSB_FIRST:
        glparamstate.unpack_lsb_first = param;
        break;
    case GL_UNPACK_ROW_LENGTH:
        glparamstate.unpack_row_length = param;
        break;
    case GL_UNPACK_IMAGE_HEIGHT:
        glparamstate.unpack_image_height = param;
        break;
    case GL_UNPACK_SKIP_ROWS:
        glparamstate.unpack_skip_rows = param;
        break;
    case GL_UNPACK_SKIP_PIXELS:
        glparamstate.unpack_skip_pixels = param;
        break;
    case GL_UNPACK_SKIP_IMAGES:
        glparamstate.unpack_skip_images = param;
        break;
    case GL_UNPACK_ALIGNMENT:
        glparamstate.unpack_alignment = param;
        break;
    }
}

void glCullFace(GLenum mode)
{
    glparamstate.glcullmode = mode;
    glparamstate.dirty.bits.dirty_cull = 1;
}

void glFrontFace(GLenum mode)
{
    HANDLE_CALL_LIST(FRONT_FACE, mode);

    bool frontcw = mode == GL_CW;
    if (frontcw != glparamstate.frontcw) {
        glparamstate.frontcw = frontcw;
        glparamstate.dirty.bits.dirty_cull = 1;
    }
}

void glBegin(GLenum mode)
{
    // Just discard all the data!
    glparamstate.imm_mode.current_numverts = 0;
    glparamstate.imm_mode.prim_type = mode;
    glparamstate.imm_mode.in_gl_begin = 1;
    glparamstate.imm_mode.has_color = 0;
    glparamstate.imm_mode.has_normal = 0;
    glparamstate.imm_mode.has_texcoord = 0;
    if (!glparamstate.imm_mode.current_vertices) {
        int count = 64;
        warning("First malloc %d", errno);
        void *buffer = malloc(count * sizeof(VertexData));
        if (buffer) {
            glparamstate.imm_mode.current_vertices = buffer;
            glparamstate.imm_mode.current_vertices_size = count;
        } else {
            warning("Failed to allocate memory for vertex buffer (%d)", errno);
            set_error(GL_OUT_OF_MEMORY);
        }
    }
}

void glEnd()
{
    union client_state cs_backup = glparamstate.cs;
    VertexData *base = glparamstate.imm_mode.current_vertices;
    int stride = sizeof(VertexData);

    OgxVertexAttribArray arrays_backup[OGX_ATTR_INDEX_COUNT];
    memcpy(arrays_backup, glparamstate.arrays, sizeof(arrays_backup));

    glVertexPointer(3, GL_FLOAT, stride, base->pos);

    if (glparamstate.imm_mode.has_normal) {
        glNormalPointer(GL_FLOAT, stride, base->norm);
    }

    if (glparamstate.imm_mode.has_color) {
        glColorPointer(4, GL_UNSIGNED_BYTE, stride, &base->color);
    }

    for (int i = 0; i < MAX_TEXTURE_UNITS; i++) {
        if (glparamstate.imm_mode.has_texcoord & (1 << i)) {
            glparamstate.cs.active_texture = i;
            glTexCoordPointer(2, GL_FLOAT, stride, base->tex[i]);
        }
    }

    glparamstate.cs.texcoord_enabled = glparamstate.imm_mode.has_texcoord;
    glparamstate.cs.color_enabled = glparamstate.imm_mode.has_color;
    glparamstate.cs.normal_enabled = glparamstate.imm_mode.has_normal;
    glparamstate.cs.vertex_enabled = 1;
    glDrawArrays(glparamstate.imm_mode.prim_type, 0, glparamstate.imm_mode.current_numverts);
    glparamstate.cs = cs_backup;
    memcpy(glparamstate.arrays, arrays_backup, sizeof(arrays_backup));
    glparamstate.imm_mode.in_gl_begin = 0;

    glparamstate.dirty.bits.dirty_attributes = 1;
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    if (_ogx_fbo_state.draw_target == 0 && width > 640)
        width = 640;

    glparamstate.viewport[0] = x;
    glparamstate.viewport[1] = y;
    glparamstate.viewport[2] = width;
    glparamstate.viewport[3] = height;
    if (glparamstate.scissor[2] < 0) {
        glparamstate.scissor[2] = width;
        glparamstate.scissor[3] = height;
    }
    glparamstate.dirty.bits.dirty_viewport = 1;
    glparamstate.dirty.bits.dirty_scissor = 1;
    if (_ogx_fbo_state.draw_target == 0) {
        _ogx_stencil_update();
    }
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    if (width > 640) width = 640;
    glparamstate.scissor[0] = x;
    glparamstate.scissor[1] = y;
    glparamstate.scissor[2] = width;
    glparamstate.scissor[3] = height;
    glparamstate.dirty.bits.dirty_scissor = 1;
}

void glMatrixMode(GLenum mode)
{
    switch (mode) {
    case GL_MODELVIEW:
        glparamstate.matrixmode = 1;
        break;
    case GL_PROJECTION:
        glparamstate.matrixmode = 0;
        break;
    case GL_TEXTURE:
        glparamstate.matrixmode = 2;
        break;
    default:
        glparamstate.matrixmode = -1;
        break;
    }
}
void glPopMatrix(void)
{
    HANDLE_CALL_LIST(POP_MATRIX);

    switch (glparamstate.matrixmode) {
    case 0:
        if (glparamstate.cur_proj_mat < 0) {
            set_error(GL_STACK_UNDERFLOW);
            return;
        }
        memcpy(glparamstate.projection_matrix, glparamstate.projection_stack[glparamstate.cur_proj_mat], sizeof(Mtx44));
        glparamstate.cur_proj_mat--;
        break;
    case 1:
        if (glparamstate.cur_modv_mat < 0) {
            set_error(GL_STACK_UNDERFLOW);
            return;
        }
        memcpy(glparamstate.modelview_matrix, glparamstate.modelview_stack[glparamstate.cur_modv_mat], sizeof(Mtx));
        glparamstate.cur_modv_mat--;
        break;
    case 2:
        {
            OgxTextureUnit *tu = active_tex_unit();
            if (tu->matrix_index <= 0) {
                set_error(GL_STACK_UNDERFLOW);
                return;
            }
            tu->matrix_index--;
        }
        glparamstate.dirty.bits.dirty_tev = 1;
        break;
    default:
        break;
    }
    glparamstate.dirty.bits.dirty_matrices = 1;
}

void glPushMatrix(void)
{
    HANDLE_CALL_LIST(PUSH_MATRIX);

    switch (glparamstate.matrixmode) {
    case 0:
        if (glparamstate.cur_proj_mat == MAX_PROJ_STACK - 1) {
            set_error(GL_STACK_OVERFLOW);
            return;
        }
        glparamstate.cur_proj_mat++;
        memcpy(glparamstate.projection_stack[glparamstate.cur_proj_mat], glparamstate.projection_matrix, sizeof(Mtx44));
        break;
    case 1:
        if (glparamstate.cur_modv_mat == MAX_MODV_STACK - 1) {
            set_error(GL_STACK_OVERFLOW);
            return;
        }
        glparamstate.cur_modv_mat++;
        memcpy(glparamstate.modelview_stack[glparamstate.cur_modv_mat], glparamstate.modelview_matrix, sizeof(Mtx));
        break;
    case 2:
        {
            OgxTextureUnit *tu = active_tex_unit();
            if (tu->matrix_index >= MAX_TEXTURE_MAT_STACK - 1) {
                set_error(GL_STACK_OVERFLOW);
                return;
            }
            memcpy(tu->matrix[tu->matrix_index + 1],
                   tu->matrix[tu->matrix_index], sizeof(Mtx));
            tu->matrix_index++;
        }
    default:
        break;
    }
}

void glLoadMatrixf(const GLfloat *m)
{
    switch (glparamstate.matrixmode) {
    case 0:
        gl_matrix_to_gx44(m, glparamstate.projection_matrix);
        break;
    case 1:
        gl_matrix_to_gx(m, glparamstate.modelview_matrix);
        break;
    case 2:
        {
            OgxTextureUnit *tu = active_tex_unit();
            gl_matrix_to_gx(m, tu->matrix[tu->matrix_index]);
        }
        glparamstate.dirty.bits.dirty_tev = 1;
        break;
    default:
        return;
    }
    glparamstate.dirty.bits.dirty_matrices = 1;
}

void glMultMatrixd(const GLdouble *m)
{
    GLfloat mf[16];
    for (int i = 0; i < 16; i++) {
        mf[i] = m[i];
    }
    glMultMatrixf(mf);
}

void glMultMatrixf(const GLfloat *m)
{
    HANDLE_CALL_LIST(MULT_MATRIX, m);

    Mtx *target = NULL;

    switch (glparamstate.matrixmode) {
    case 0:
        Mtx44 mtx44;
        gl_matrix_to_gx44(m, mtx44);
        guMtx44Concat(glparamstate.projection_matrix, mtx44,
                      glparamstate.projection_matrix);
        break;
    case 1:
        target = &glparamstate.modelview_matrix;
        break;
    case 2:
        target = current_tex_matrix();
        glparamstate.dirty.bits.dirty_tev = 1;
        break;
    default:
        break;
    }
    glparamstate.dirty.bits.dirty_matrices = 1;
    if (target) {
        Mtx mtx;
        gl_matrix_to_gx(m, mtx);
        guMtxConcat(*target, mtx, *target);
    }
}

void glLoadIdentity()
{
    HANDLE_CALL_LIST(LOAD_IDENTITY);

    switch (glparamstate.matrixmode) {
    case 0:
        guMtx44Identity(glparamstate.projection_matrix);
        break;
    case 1:
        guMtxIdentity(glparamstate.modelview_matrix);
        break;
    case 2:
        {
            OgxTextureUnit *tu = active_tex_unit();
            guMtxIdentity(tu->matrix[tu->matrix_index]);
        }
        glparamstate.dirty.bits.dirty_tev = 1;
        break;
    default:
        return;
    }
    glparamstate.dirty.bits.dirty_matrices = 1;
}

void glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    HANDLE_CALL_LIST(SCALE, x, y, z);

    Mtx *target = NULL;

    switch (glparamstate.matrixmode) {
    case 0:
        guMtxApplyScale(glparamstate.projection_matrix,
                        glparamstate.projection_matrix,
                        x, y, z);
        break;
    case 1:
        target = &glparamstate.modelview_matrix;
        break;
    case 2:
        target = current_tex_matrix();
        glparamstate.dirty.bits.dirty_tev = 1;
        break;
    default:
        break;
    }

    glparamstate.dirty.bits.dirty_matrices = 1;
    if (target) {
        guMtxApplyScale(*target, *target, x, y, z);
    }
}

void glScaled(GLdouble x, GLdouble y, GLdouble z)
{
    glScalef(x, y, z);
}

void glTranslated(GLdouble x, GLdouble y, GLdouble z)
{
    glTranslatef(x, y, z);
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    HANDLE_CALL_LIST(TRANSLATE, x, y, z);

    Mtx *target = NULL;

    switch (glparamstate.matrixmode) {
    case 0:
        guMtxApplyTrans(glparamstate.projection_matrix,
                        glparamstate.projection_matrix,
                        x, y, z);
        break;
    case 1:
        target = &glparamstate.modelview_matrix;
        break;
    case 2:
        target = current_tex_matrix();
        glparamstate.dirty.bits.dirty_tev = 1;
        break;
    default:
        break;
    }

    glparamstate.dirty.bits.dirty_matrices = 1;
    if (target) {
        guMtxApplyTrans(*target, *target, x, y, z);
    }
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    HANDLE_CALL_LIST(ROTATE, angle, x, y, z);

    Mtx *target = NULL;
    Mtx44 rot;
    guVector axis = { x, y, z };
    if (angle == 0.0f || (x == 0.0f && y == 0.0f && z == 0.0f)) return;
    guMtxRotAxisDeg(rot, &axis, angle);

    switch (glparamstate.matrixmode) {
    case 0:
        rot[3][0] = rot[3][1] = rot[3][2] = 0.0f;
        rot[3][3] = 1.0f;
        guMtx44Concat(glparamstate.projection_matrix, rot, glparamstate.projection_matrix);
        break;
    case 1:
        target = &glparamstate.modelview_matrix;
        break;
    case 2:
        target = current_tex_matrix();
        glparamstate.dirty.bits.dirty_tev = 1;
        break;
    default:
        break;
    }

    glparamstate.dirty.bits.dirty_matrices = 1;
    if (target) {
        guMtxConcat(*target, rot, *target);
    }
}

void glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z)
{
    glRotatef(angle, x, y, z);
}

void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    glparamstate.clear_color.r = clampf_01(red) * 255.0f;
    glparamstate.clear_color.g = clampf_01(green) * 255.0f;
    glparamstate.clear_color.b = clampf_01(blue) * 255.0f;
    glparamstate.clear_color.a = clampf_01(alpha) * 255.0f;
}
void glClearDepth(GLclampd depth)
{
    float clearz = clampf_01(depth);
    if (clearz != glparamstate.clearz) {
        glparamstate.clearz = clearz;
        glparamstate.dirty.bits.dirty_clearz = 1;
    }
}

// Clearing is simulated by rendering a big square with the depth value
// and the desired color
void glClear(GLbitfield mask)
{
    if (glparamstate.render_mode == GL_SELECT) {
        return;
    }

    /* Since this function is typically called at the beginning of a frame, and
     * the integration library might have draw something on the screen right
     * before (typically, a mouse cursor), we assume the scissor to be dirty
     * and reset it. */
    update_scissor();
    if (glparamstate.dirty.bits.dirty_viewport) {
        update_viewport();
    }

    _ogx_efb_set_content_type(OGX_EFB_SCENE);

    if (mask & GL_STENCIL_BUFFER_BIT) {
        _ogx_stencil_clear();
    }

    if (mask & GL_ACCUM_BUFFER_BIT) {
        _ogx_accum_clear();
    }

    if (mask & GL_DEPTH_BUFFER_BIT) {
        GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
        GX_SetZCompLoc(GX_DISABLE);
        GX_SetZTexture(GX_ZT_REPLACE, GX_TF_Z24X8, 0);
        GX_SetNumTexGens(1);

        /* Create a 1x1 Z-texture to set the desired depth */
        if (glparamstate.dirty.bits.dirty_clearz) {
            /* Our z-buffer depth is 24 bits */
            uint32_t depth = glparamstate.clearz * ((1 << 24) - 1);
            s_zbuffer_texels[0] = 0xff; // ignored
            s_zbuffer_texels[1] = (depth >> 16) & 0xff;
            s_zbuffer_texels[32] = (depth >> 8) & 0xff;
            s_zbuffer_texels[33] = depth & 0xff;
            DCStoreRange(s_zbuffer_texels, sizeof(s_zbuffer_texels));
            GX_InvalidateTexAll();
            glparamstate.dirty.bits.dirty_clearz = 0;
        }
        GX_LoadTexObj(&s_zbuffer_texture, GX_TEXMAP0);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    } else {
        GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
        GX_SetNumTexGens(0);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    }

    if (mask & GL_COLOR_BUFFER_BIT)
        GX_SetColorUpdate(GX_TRUE);
    else
        GX_SetColorUpdate(GX_TRUE);

    GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);

    _ogx_setup_2D_projection();

    GX_SetNumChans(1);
    GX_SetNumTevStages(1);

    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_U16, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_U8, 0);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_InvVtxCache();

    if (glparamstate.fog.enabled) {
        /* Disable fog while clearing */
        GX_SetFog(GX_FOG_NONE, 0.0, 0.0, 0.0, 0.0, glparamstate.clear_color);
        glparamstate.dirty.bits.dirty_fog = 1;
    }

    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2u16(0, 0);
    GX_Color4u8(glparamstate.clear_color.r, glparamstate.clear_color.g, glparamstate.clear_color.b, glparamstate.clear_color.a);
    GX_TexCoord2u8(0, 0);
    GX_Position2u16(0, glparamstate.viewport[3]);
    GX_Color4u8(glparamstate.clear_color.r, glparamstate.clear_color.g, glparamstate.clear_color.b, glparamstate.clear_color.a);
    GX_TexCoord2u8(0, 1);
    GX_Position2u16(glparamstate.viewport[2], glparamstate.viewport[3]);
    GX_Color4u8(glparamstate.clear_color.r, glparamstate.clear_color.g, glparamstate.clear_color.b, glparamstate.clear_color.a);
    GX_TexCoord2u8(1, 1);
    GX_Position2u16(glparamstate.viewport[2], 0);
    GX_Color4u8(glparamstate.clear_color.r, glparamstate.clear_color.g, glparamstate.clear_color.b, glparamstate.clear_color.a);
    GX_TexCoord2u8(1, 0);
    GX_End();

    GX_SetZTexture(GX_ZT_DISABLE, GX_TF_Z24X8, 0);

    glparamstate.dirty.bits.dirty_alphatest = 1;
    glparamstate.dirty.bits.dirty_blend = 1;
    glparamstate.dirty.bits.dirty_z = 1;
    glparamstate.dirty.bits.dirty_color_update = 1;
    glparamstate.dirty.bits.dirty_matrices = 1;
    glparamstate.dirty.bits.dirty_tev = 1;
    glparamstate.dirty.bits.dirty_cull = 1;

    glparamstate.draw_count++;
}

void glDepthFunc(GLenum func)
{
    uint8_t gx_func = gx_compare_from_gl(func);
    if (gx_func == 0xff) return;
    glparamstate.zfunc = gx_func;
    glparamstate.dirty.bits.dirty_z = 1;
}

void glDepthMask(GLboolean flag)
{
    if (flag == GL_FALSE || flag == 0)
        glparamstate.zwrite = GX_FALSE;
    else
        glparamstate.zwrite = GX_TRUE;
    glparamstate.dirty.bits.dirty_z = 1;
}

void glDepthRange(GLclampd near_val, GLclampd far_val)
{
    glparamstate.depth_near = near_val;
    glparamstate.depth_far = far_val;
}

GLint glRenderMode(GLenum mode)
{
    int hit_count;

    switch (mode) {
    case GL_RENDER:
    case GL_SELECT:
        hit_count = _ogx_selection_mode_changing(mode);
        break;
    default:
        warning("Unsupported render mode 0x%04x", mode);
        return 0;
    }
    glparamstate.render_mode = mode;
    return hit_count;
}

void glFlush() {} // All commands are sent immediately to draw, no queue, so pointless

// Waits for all the commands to be successfully executed
void glFinish()
{
    GX_DrawDone(); // Be careful, WaitDrawDone waits for the DD command, this sends AND waits for it
}

void glAlphaFunc(GLenum func, GLclampf ref)
{
    uint8_t gx_func = gx_compare_from_gl(func);
    if (gx_func == 0xff) return;

    glparamstate.alpha_func = gx_func;
    glparamstate.alpha_ref = ref * 255;
    glparamstate.dirty.bits.dirty_alphatest = 1;
}

void glBlendFunc(GLenum sfactor, GLenum dfactor)
{
    HANDLE_CALL_LIST(BLEND_FUNC, sfactor, dfactor);

    switch (sfactor) {
    case GL_ZERO:
        glparamstate.srcblend = GX_BL_ZERO;
        break;
    case GL_ONE:
        glparamstate.srcblend = GX_BL_ONE;
        break;
    case GL_SRC_COLOR:
        glparamstate.srcblend = GX_BL_SRCCLR;
        break;
    case GL_ONE_MINUS_SRC_COLOR:
        glparamstate.srcblend = GX_BL_INVSRCCLR;
        break;
    case GL_DST_COLOR:
        glparamstate.srcblend = GX_BL_DSTCLR;
        break;
    case GL_ONE_MINUS_DST_COLOR:
        glparamstate.srcblend = GX_BL_INVDSTCLR;
        break;
    case GL_SRC_ALPHA:
        glparamstate.srcblend = GX_BL_SRCALPHA;
        break;
    case GL_ONE_MINUS_SRC_ALPHA:
        glparamstate.srcblend = GX_BL_INVSRCALPHA;
        break;
    case GL_DST_ALPHA:
        glparamstate.srcblend = GX_BL_DSTALPHA;
        break;
    case GL_ONE_MINUS_DST_ALPHA:
        glparamstate.srcblend = GX_BL_INVDSTALPHA;
        break;
    case GL_CONSTANT_COLOR:
    case GL_ONE_MINUS_CONSTANT_COLOR:
    case GL_CONSTANT_ALPHA:
    case GL_ONE_MINUS_CONSTANT_ALPHA:
    case GL_SRC_ALPHA_SATURATE:
        break; // Not supported
    }

    switch (dfactor) {
    case GL_ZERO:
        glparamstate.dstblend = GX_BL_ZERO;
        break;
    case GL_ONE:
        glparamstate.dstblend = GX_BL_ONE;
        break;
    case GL_SRC_COLOR:
        glparamstate.dstblend = GX_BL_SRCCLR;
        break;
    case GL_ONE_MINUS_SRC_COLOR:
        glparamstate.dstblend = GX_BL_INVSRCCLR;
        break;
    case GL_DST_COLOR:
        glparamstate.dstblend = GX_BL_DSTCLR;
        break;
    case GL_ONE_MINUS_DST_COLOR:
        glparamstate.dstblend = GX_BL_INVDSTCLR;
        break;
    case GL_SRC_ALPHA:
        glparamstate.dstblend = GX_BL_SRCALPHA;
        break;
    case GL_ONE_MINUS_SRC_ALPHA:
        glparamstate.dstblend = GX_BL_INVSRCALPHA;
        break;
    case GL_DST_ALPHA:
        glparamstate.dstblend = GX_BL_DSTALPHA;
        break;
    case GL_ONE_MINUS_DST_ALPHA:
        glparamstate.dstblend = GX_BL_INVDSTALPHA;
        break;
    case GL_CONSTANT_COLOR:
    case GL_ONE_MINUS_CONSTANT_COLOR:
    case GL_CONSTANT_ALPHA:
    case GL_ONE_MINUS_CONSTANT_ALPHA:
    case GL_SRC_ALPHA_SATURATE:
        break; // Not supported
    }

    glparamstate.dirty.bits.dirty_blend = 1;
}

void glPointSize(GLfloat size)
{
    unsigned int gxsize = size;
    if (gxsize > 255) gxsize = 255;
    GX_SetPointSize(gxsize, GX_TO_ONE);
}

void glLineWidth(GLfloat width)
{
    GX_SetLineWidth((unsigned int)(width * 16), GX_TO_ZERO);
}

void glPolygonMode(GLenum face, GLenum mode)
{
    if (face != GL_FRONT_AND_BACK) {
        warning("glPolygonMode: face selection is unsupported");
        return;
    }

    glparamstate.polygon_mode = mode;
}

void glPolygonOffset(GLfloat factor, GLfloat units)
{
    glparamstate.polygon_offset_factor = factor;
    glparamstate.polygon_offset_units = units;
    glparamstate.dirty.bits.dirty_matrices = 1;
}

void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    glparamstate.color_update = (red | green | blue | alpha) != 0;
    glparamstate.dirty.bits.dirty_color_update = 1;
}

/*

  Render setup code.

*/

void glDisableClientState(GLenum cap)
{
    glparamstate.dirty.bits.dirty_attributes = 1;
    switch (cap) {
    case GL_COLOR_ARRAY:
        glparamstate.cs.color_enabled = 0;
        break;
    case GL_INDEX_ARRAY:
        glparamstate.cs.index_enabled = 0;
        break;
    case GL_NORMAL_ARRAY:
        glparamstate.cs.normal_enabled = 0;
        break;
    case GL_TEXTURE_COORD_ARRAY:
        {
            int unit = glparamstate.cs.active_texture;
            glparamstate.cs.texcoord_enabled &= ~(1 << unit);
        }
        break;
    case GL_VERTEX_ARRAY:
        glparamstate.cs.vertex_enabled = 0;
        break;
    case GL_EDGE_FLAG_ARRAY:
    case GL_FOG_COORD_ARRAY:
    case GL_SECONDARY_COLOR_ARRAY:
        return;
    }
}
void glEnableClientState(GLenum cap)
{
    glparamstate.dirty.bits.dirty_attributes = 1;
    switch (cap) {
    case GL_COLOR_ARRAY:
        glparamstate.cs.color_enabled = 1;
        break;
    case GL_INDEX_ARRAY:
        glparamstate.cs.index_enabled = 1;
        break;
    case GL_NORMAL_ARRAY:
        glparamstate.cs.normal_enabled = 1;
        break;
    case GL_TEXTURE_COORD_ARRAY:
        {
            int unit = glparamstate.cs.active_texture;
            glparamstate.cs.texcoord_enabled |= (1 << unit);
        }
        break;
    case GL_VERTEX_ARRAY:
        glparamstate.cs.vertex_enabled = 1;
        break;
    case GL_EDGE_FLAG_ARRAY:
    case GL_FOG_COORD_ARRAY:
    case GL_SECONDARY_COLOR_ARRAY:
        return;
    }
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    STATE_ARRAY(POS).vbo = glparamstate.bound_vbo_array;
    STATE_ARRAY(POS).size = size;
    STATE_ARRAY(POS).type = type;
    STATE_ARRAY(POS).stride = stride;
    STATE_ARRAY(POS).pointer = pointer;
    glparamstate.dirty.bits.dirty_attributes = 1;
}

void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
    STATE_ARRAY(NRM).vbo = glparamstate.bound_vbo_array;
    STATE_ARRAY(NRM).size = 3;
    STATE_ARRAY(NRM).type = type;
    STATE_ARRAY(NRM).stride = stride;
    STATE_ARRAY(NRM).pointer = pointer;
    glparamstate.dirty.bits.dirty_attributes = 1;
}

void glColorPointer(GLint size, GLenum type,
                    GLsizei stride, const GLvoid *pointer)
{
    STATE_ARRAY(CLR).vbo = glparamstate.bound_vbo_array;
    STATE_ARRAY(CLR).size = size;
    STATE_ARRAY(CLR).type = type;
    STATE_ARRAY(CLR).stride = stride;
    STATE_ARRAY(CLR).pointer = pointer;
    glparamstate.dirty.bits.dirty_attributes = 1;
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    int unit = glparamstate.cs.active_texture;
    STATE_ARRAY_TEX(unit).vbo = glparamstate.bound_vbo_array;
    STATE_ARRAY_TEX(unit).size = size;
    STATE_ARRAY_TEX(unit).type = type;
    STATE_ARRAY_TEX(unit).stride = stride;
    STATE_ARRAY_TEX(unit).pointer = pointer;
    glparamstate.dirty.bits.dirty_attributes = 1;
}

void glInterleavedArrays(GLenum format, GLsizei stride, const GLvoid *pointer)
{
    OgxVertexAttribArray *vertex = &STATE_ARRAY(POS);
    OgxVertexAttribArray *normal = &STATE_ARRAY(NRM);
    OgxVertexAttribArray *color = &STATE_ARRAY(CLR);
    int unit = glparamstate.cs.active_texture;
    OgxVertexAttribArray *texcoord = &STATE_ARRAY_TEX(unit);

    glparamstate.cs.index_enabled = 0;
    glparamstate.cs.normal_enabled = 0;
    glparamstate.cs.texcoord_enabled = 0;
    glparamstate.cs.vertex_enabled = 1; /* This is mandatory */
    glparamstate.cs.color_enabled = 0;

    vertex->vbo = 0;
    vertex->type = GL_FLOAT;
    vertex->size = 3;
    color->type = GL_FLOAT;
    switch (format) {
    case GL_V2F:
        vertex->size = 2;
        break;
    case GL_V3F:
        break;
    case GL_N3F_V3F:
        glparamstate.cs.normal_enabled = 1;
        break;
    case GL_T2F_V3F:
        glparamstate.cs.texcoord_enabled = (1 << unit);
        break;
    case GL_T2F_N3F_V3F:
        glparamstate.cs.normal_enabled = 1;
        glparamstate.cs.texcoord_enabled = (1 << unit);
        break;
    case GL_C4F_N3F_V3F:
        glparamstate.cs.normal_enabled = 1;
        glparamstate.cs.color_enabled = 1;
        color->size = 4;
        break;
    case GL_C3F_V3F:
        glparamstate.cs.color_enabled = 1;
        color->size = 3;
        break;
    case GL_T2F_C3F_V3F:
        glparamstate.cs.color_enabled = 1;
        glparamstate.cs.texcoord_enabled = (1 << unit);
        color->size = 3;
        break;
    case GL_T2F_C4F_N3F_V3F: // Complete type
        glparamstate.cs.normal_enabled = 1;
        glparamstate.cs.color_enabled = 1;
        glparamstate.cs.texcoord_enabled = (1 << unit);
        color->size = 4;
        break;

    case GL_C4UB_V2F:
    case GL_C4UB_V3F:
    case GL_T2F_C4UB_V3F:
    case GL_T4F_C4F_N3F_V4F:
    case GL_T4F_V4F:
        // TODO: Implement T4F! And UB color!
        return;
    }

    const char *ptr = pointer;
    if (glparamstate.cs.texcoord_enabled) {
        texcoord->vbo = 0;
        texcoord->pointer = ptr;
        texcoord->type = GL_FLOAT;
        texcoord->size = 2;
        ptr += 2 * sizeof(float);
    }
    if (glparamstate.cs.color_enabled) {
        color->vbo = 0;
        color->pointer = ptr;
        /* TODO: use other type when implementing UB color support */
        ptr += color->size * sizeof(float);
    }
    if (glparamstate.cs.normal_enabled) {
        normal->vbo = 0;
        normal->pointer = ptr;
        normal->type = GL_FLOAT;
        normal->size = 3;
        ptr += 3 * sizeof(float);
    }
    /* Vertices are always enabled */
    vertex->pointer = ptr;
    ptr += vertex->size * sizeof(float);
    if (stride == 0) stride = ptr - (const char *)pointer;
    vertex->stride = normal->stride = texcoord->stride =
        color->stride = stride;

    glparamstate.dirty.bits.dirty_attributes = 1;
}

/*

  Render code. All the renderer calls should end calling this one.

*/

/*****************************************************

        LIGHTING IMPLEMENTATION EXPLAINED

   GX differs in some aspects from OGL lighting.
    - It shares the same material for ambient
      and diffuse components
    - Lights can be specular or diffuse, not both
    - The ambient component is NOT attenuated by
      distance

   GX hardware can do lights with:
    - Distance based attenuation
    - Angle based attenuation (for diffuse lights)

   We simulate each light this way:

    - Ambient: Using distance based attenuation, disabling
      angle-based attenuation (GX_DF_NONE).
    - Diffuse: Using distance based attenuation, enabling
      angle-based attenuation in clamp mode (GX_DF_CLAMP)
    - Specular: Specular based attenuation (GX_AF_SPEC)

   As each channel is configured for all the TEV stages
   we CANNOT emulate the three types of light at once.
   So we emulate two types only.

   For unlit scenes the setup is:
     - TEV 0: Modulate vertex color with texture
              Speed hack: use constant register
              If no tex, just pass color
   For ambient+diffuse lights:
     - TEV 0: Pass RAS0 color with material color
          set to vertex color (to modulate vert color).
          Set the ambient value for this channel to 0.
         Speed hack: Use material register for constant
          color
     - TEV 1: Sum RAS1 color with material color
          set to vertex color (to modulate vert color)
          to the previous value. Also set the ambient
          value to the global ambient value.
         Speed hack: Use material register for constant
          color
     - TEV 2: If texture is enabled multiply the texture
          rasterized color with the previous value.
      The result is:

     Color = TexC * (VertColor*AmbientLightColor*Atten
      + VertColor*DiffuseLightColor*Atten*DifAtten)

     As we use the material register for vertex color
     the material colors will be multiplied with the
     light color and uploaded as light color.

     We'll be using 0-3 lights for ambient and 4-7 lights
     for diffuse

******************************************************/

static inline bool is_black(const float *color)
{
    return color[0] == 0.0f && color[1] == 0.0f && color[2] == 0.0f;
}

static void allocate_lights()
{
    /* For the time being, just allocate the lights using a first come, first
     * served algorithm.
     * TODO: take the light impact into account: privilege stronger lights, and
     * light types in this order (probably): directional, ambient, diffuse,
     * specular. */
    char lights_needed = 0;
    bool global_ambient_off = is_black(glparamstate.lighting.globalambient);
    for (int i = 0; i < MAX_LIGHTS; i++) {
        if (!glparamstate.lighting.lights[i].enabled)
            continue;

        if (!is_black(glparamstate.lighting.lights[i].ambient_color) &&
            !global_ambient_off) {
            /* This ambient light is needed, allocate it */
            char gx_light = lights_needed++;
            glparamstate.lighting.lights[i].gx_ambient =
                gx_light < MAX_GX_LIGHTS ? gx_light : -1;
        } else {
            glparamstate.lighting.lights[i].gx_ambient = -1;
        }

        if (!is_black(glparamstate.lighting.lights[i].diffuse_color)) {
            /* This diffuse light is needed, allocate it */
            char gx_light = lights_needed++;
            glparamstate.lighting.lights[i].gx_diffuse =
                gx_light < MAX_GX_LIGHTS ? gx_light : -1;
        } else {
            glparamstate.lighting.lights[i].gx_diffuse = -1;
        }

        /* GX support specular light only for directional light sources. For
         * this reason we enable the specular light only if the "w" component
         * of the position is 0. */
        if (!is_black(glparamstate.lighting.lights[i].specular_color) &&
            !is_black(glparamstate.lighting.matspecular) &&
            glparamstate.lighting.matshininess > 0.0 &&
            glparamstate.lighting.lights[i].position[3] == 0.0f) {
            /* This specular light is needed, allocate it */
            char gx_light = lights_needed++;
            glparamstate.lighting.lights[i].gx_specular =
                gx_light < MAX_GX_LIGHTS ? gx_light : -1;
        } else {
            glparamstate.lighting.lights[i].gx_specular = -1;
        }
    }

    if (lights_needed > MAX_GX_LIGHTS) {
        warning("Excluded %d lights since max is 8", lights_needed - MAX_GX_LIGHTS);
    }
}

static LightMasks prepare_lighting()
{
    LightMasks masks = { 0, 0 };
    int i;

    allocate_lights();

    for (i = 0; i < MAX_LIGHTS; i++) {
        if (!glparamstate.lighting.lights[i].enabled)
            continue;

        int8_t gx_ambient_idx = glparamstate.lighting.lights[i].gx_ambient;
        int8_t gx_diffuse_idx = glparamstate.lighting.lights[i].gx_diffuse;
        int8_t gx_specular_idx = glparamstate.lighting.lights[i].gx_specular;
        GXLightObj *gx_ambient = gx_ambient_idx >= 0 ?
            &glparamstate.lighting.lightobj[gx_ambient_idx] : NULL;
        GXLightObj *gx_diffuse = gx_diffuse_idx >= 0 ?
            &glparamstate.lighting.lightobj[gx_diffuse_idx] : NULL;
        GXLightObj *gx_specular = gx_specular_idx >= 0 ?
            &glparamstate.lighting.lightobj[gx_specular_idx] : NULL;

        if (gx_ambient) {
            // Multiply the light color by the material color and set as light color
            GXColor amb_col = gxcol_new_fv(glparamstate.lighting.lights[i].ambient_color);
            GX_InitLightColor(gx_ambient, amb_col);
            GX_InitLightPosv(gx_ambient, &glparamstate.lighting.lights[i].position[0]);
        }

        if (gx_diffuse) {
            GXColor diff_col = gxcol_new_fv(glparamstate.lighting.lights[i].diffuse_color);
            GX_InitLightColor(gx_diffuse, diff_col);
            GX_InitLightPosv(gx_diffuse, &glparamstate.lighting.lights[i].position[0]);
        }

        // FIXME: Need to consider spotlights
        if (glparamstate.lighting.lights[i].position[3] == 0) {
            // Directional light, it's a point light very far without attenuation
            if (gx_ambient) {
                GX_InitLightAttn(gx_ambient, 1, 0, 0, 1, 0, 0);
            }
            if (gx_diffuse) {
                GX_InitLightAttn(gx_diffuse, 1, 0, 0, 1, 0, 0);
            }
            if (gx_specular) {
                GXColor spec_col = gxcol_new_fv(glparamstate.lighting.lights[i].specular_color);

                /* We need to compute the normals of the direction */
                float normal[3] = {
                    -glparamstate.lighting.lights[i].position[0],
                    -glparamstate.lighting.lights[i].position[1],
                    -glparamstate.lighting.lights[i].position[2],
                };
                normalize(normal);
                GX_InitSpecularDirv(gx_specular, normal);
                GX_InitLightShininess(gx_specular, glparamstate.lighting.matshininess);
                GX_InitLightColor(gx_specular, spec_col);
            }
        } else {
            // Point light
            if (gx_ambient) {
                GX_InitLightAttn(gx_ambient, 1, 0, 0,
                                 glparamstate.lighting.lights[i].atten[0],
                                 glparamstate.lighting.lights[i].atten[1],
                                 glparamstate.lighting.lights[i].atten[2]);
                GX_InitLightDir(gx_ambient, 0, -1, 0);
            }
            if (gx_diffuse) {
                GX_InitLightAttn(gx_diffuse, 1, 0, 0,
                                 glparamstate.lighting.lights[i].atten[0],
                                 glparamstate.lighting.lights[i].atten[1],
                                 glparamstate.lighting.lights[i].atten[2]);
                GX_InitLightDir(gx_diffuse, 0, -1, 0);
            }
        }

        if (gx_ambient) {
            GX_LoadLightObj(gx_ambient, 1 << gx_ambient_idx);
            masks.ambient_mask |= (1 << gx_ambient_idx);
        }
        if (gx_diffuse) {
            GX_LoadLightObj(gx_diffuse, 1 << gx_diffuse_idx);
            masks.diffuse_mask |= (1 << gx_diffuse_idx);
        }
        if (gx_specular) {
            GX_LoadLightObj(gx_specular, 1 << gx_specular_idx);
            masks.specular_mask |= (1 << gx_specular_idx);
        }
    }
    debug(OGX_LOG_LIGHTING,
          "Ambient mask 0x%02x, diffuse 0x%02x, specular 0x%02x",
          masks.ambient_mask, masks.diffuse_mask, masks.specular_mask);
    return masks;
}

static int count_color_channels()
{
    int color_provide = 0;
    if (glparamstate.cs.color_enabled &&
        (!glparamstate.lighting.enabled || glparamstate.lighting.color_material_enabled)) { // Vertex colouring
        if (glparamstate.lighting.enabled)
            color_provide = 2; // Lighting requires two color channels
        else
            color_provide = 1;
    }
    return color_provide;
}

static inline bool point_sprites_texcoord_replace(uint8_t gxmode)
{
    return gxmode == GX_POINTS &&
        glparamstate.point_sprites_enabled &&
        glparamstate.point_sprites_coord_replace;
}

static inline bool point_sprites_changed(uint8_t gxmode)
{
    bool enabled = point_sprites_texcoord_replace(gxmode);
    if (enabled != s_point_sprites_was_enabled) {
        s_point_sprites_was_enabled = enabled;
        return true;
    } else {
        return false;
    }
}

/* "fp" stands for "fixed pipeline" */
void _ogx_fp_update_vertex_array_readers(OgxDrawMode mode)
{
    if (glparamstate.cs.vertex_enabled) {
        _ogx_array_add(GX_VA_POS, &STATE_ARRAY(POS));
    }

    if (glparamstate.cs.normal_enabled) {
        _ogx_array_add(GX_VA_NRM, &STATE_ARRAY(NRM));
    }

    if (glparamstate.cs.color_enabled) {
        int num_channels = count_color_channels();
        for (int i = 0; i < num_channels; i++)
            _ogx_array_add(GX_VA_CLR0, &STATE_ARRAY(CLR));
    }

    for (int unit = 0; unit < MAX_TEXTURE_UNITS; unit++) {
        if (!(glparamstate.texture_enabled & (1 << unit))) continue;

        OgxTextureUnit *tu = &glparamstate.texture_unit[unit];

        if (point_sprites_texcoord_replace(mode.mode)) {
            /* We assume that GL_POINT_SPRITE_COORD_ORIGIN is set to
             * GL_UPPER_LEFT */
            float coords[2] = { 0.0f, 0.0f };
            tu->array_reader =
                _ogx_array_add_constant_fv(GX_VA_TEX0, 2, coords);
        } else if (tu->gen_enabled &&
                   _ogx_texture_gen_sw_enabled(unit)) {
            /* Some kinds of texture generation cannot be performed by the GPU,
             * and we have to generate the texture coordinates in software. */
            OgxGenerator_fv generator = NULL;
            switch (tu->gen_mode) {
            case GL_SPHERE_MAP:
                generator = _ogx_texture_gen_sw_sphere_map;
                break;
            }
            tu->array_reader =
                _ogx_array_add_generator_fv(GX_VA_TEX0, 2, generator);
        } else if (glparamstate.cs.texcoord_enabled & (1 << unit)) {
            tu->array_reader =
                _ogx_array_add(GX_VA_TEX0, &STATE_ARRAY_TEX(unit));
        } else {
            tu->array_reader = NULL;
        }
    }
}

void _ogx_update_vertex_array_readers(OgxDrawMode mode)
{
    _ogx_arrays_reset();

    if (glparamstate.current_program) {
        _ogx_shader_update_vertex_array_readers(mode);
    } else {
        _ogx_fp_update_vertex_array_readers(mode);
    }

    glparamstate.dirty.bits.dirty_attributes = 0;
}

OgxDrawMode _ogx_draw_mode(GLenum mode)
{
    OgxDrawMode dm = { 0xff, false };

    if (glparamstate.polygon_mode != GL_FILL) {
        if (glparamstate.polygon_mode == GL_POINT) {
            dm.mode = GX_POINTS;
        } else { // GL LINE
            dm.mode = GX_LINESTRIP;
            dm.loop = true;
        }
        return dm;
    }

    switch (mode) {
    case GL_POINTS:
        dm.mode = GX_POINTS;
        break;
    case GL_LINE_LOOP:
        dm.loop = true;
        // fall through
    case GL_LINE_STRIP:
        dm.mode = GX_LINESTRIP;
        break;
    case GL_LINES:
        dm.mode = GX_LINES;
        break;
    case GL_TRIANGLE_STRIP:
    case GL_QUAD_STRIP:
        dm.mode = GX_TRIANGLESTRIP;
        break;
    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
        dm.mode = GX_TRIANGLEFAN;
        break;
    case GL_TRIANGLES:
        dm.mode = GX_TRIANGLES;
        break;
    case GL_QUADS:
        dm.mode = GX_QUADS;
        break;
    }
    return dm;
}

static void setup_fog()
{
    u8 mode, proj_type;
    GXColor color;
    float start, end, near, far;

    /* GX_SetFog() works differently from OpenGL:
     * 1. It requires the caller to pass the near and far coordinates
     * 2. It applies the "start" and "end" parameters to all curve types
     *    (OpenGL only uses them for linear fogging)
     * 3. It does not support the "density" parameter
     */

    if (glparamstate.fog.enabled) {
        get_projection_info(glparamstate.projection_matrix, &proj_type, &near, &far);

        color = gxcol_new_fv(glparamstate.fog.color);
        switch (glparamstate.fog.mode) {
        case GL_EXP: mode = GX_FOG_EXP; break;
        case GL_EXP2: mode = GX_FOG_EXP2; break;
        case GL_LINEAR: mode = GX_FOG_LIN; break;
        }
        if (proj_type == GX_ORTHOGRAPHIC)
            mode += (GX_FOG_ORTHO_LIN - GX_FOG_PERSP_LIN);

        if (glparamstate.fog.mode == GL_LINEAR) {
            start = glparamstate.fog.start;
            end = glparamstate.fog.end;
        } else {
            /* Tricky part: GX spreads the exponent function so that it affects
             * the range from "start" to "end" (though it's unclear how it
             * does, since the 0 value is never actually reached), whereas
             * openGL expects it to affect the whole world, but with a "speed"
             * dictated by the "density" parameter.
             * So, we emulate the density by playing with the "end" parameter.
             * The factors used in the computations of "end" below have been
             * found empirically, comparing the result with a desktop OpenGL
             * implementation.
             */
            start = near;
            if (glparamstate.fog.density <= 0.0) {
                end = far;
            } else if (glparamstate.fog.mode == GL_EXP2) {
                end = 2.0f / glparamstate.fog.density;
            } else { /* GL_EXP */
                end = 5.0f / glparamstate.fog.density;
            }
        }
    } else {
        start = end = near = far = 0.0f;
        mode = GX_FOG_NONE;
    }
    GX_SetFog(mode, start, end, near, far, color);
}

static bool setup_common_stages()
{
    if (glparamstate.stencil.enabled) {
        bool should_draw = _ogx_stencil_setup_tev();
        if (!should_draw) return false;
    }

    if (glparamstate.clip_plane_mask != 0) {
        _ogx_clip_setup_tev();
    }

    /* Stages and texture coordinate slots must be enabled sequentially, so we
     * know that the number of used resources is given by
     * OgxGpuResources::{tevstage,texcoord}_first. */
    GX_SetNumTevStages(ogx_gpu_resources->tevstage_first);
    GX_SetNumTexGens(ogx_gpu_resources->texcoord_first);
    return true;
}

bool _ogx_setup_render_stages()
{
    if (!glparamstate.dirty.bits.dirty_tev) return true;

    u8 raster_output, raster_reg_index;
    if (glparamstate.texture_enabled) {
        raster_reg_index = ogx_gpu_resources->tevreg_first++;
        raster_output = GX_TEVREG0 + raster_reg_index;
    } else {
        raster_output = GX_TEVPREV;
    }

    if (glparamstate.lighting.enabled) {
        LightMasks light_mask = prepare_lighting();

        GXColor color_black = { 0, 0, 0, 255 };
        GXColor color_gamb = gxcol_new_fv(glparamstate.lighting.globalambient);

        ogx_gpu_resources->tevstage_first += 2;
        GX_SetNumChans(2);

        unsigned char vert_color_src = GX_SRC_VTX;
        if (!glparamstate.cs.color_enabled || !glparamstate.lighting.color_material_enabled) {
            vert_color_src = GX_SRC_REG;
            GXColor acol, dcol, scol;
            bool ambient_set = false, diffuse_set = false, specular_set = false;

            if (glparamstate.lighting.color_material_enabled) {
                GXColor ccol = gxcol_new_fv(glparamstate.imm_mode.current_color);

                if (glparamstate.lighting.color_material_mode == GL_AMBIENT ||
                    glparamstate.lighting.color_material_mode == GL_AMBIENT_AND_DIFFUSE) {
                    acol = ccol;
                    ambient_set = true;
                }

                if (glparamstate.lighting.color_material_mode == GL_DIFFUSE ||
                    glparamstate.lighting.color_material_mode == GL_AMBIENT_AND_DIFFUSE) {
                    dcol = ccol;
                    diffuse_set = true;
                }

                if (glparamstate.lighting.color_material_mode == GL_SPECULAR) {
                    scol = ccol;
                    specular_set = true;
                }
            }
            if (!ambient_set) {
                acol = gxcol_new_fv(glparamstate.lighting.matambient);
            }
            if (!diffuse_set) {
                dcol = gxcol_new_fv(glparamstate.lighting.matdiffuse);
            }
            if (!specular_set) {
                scol = gxcol_new_fv(glparamstate.lighting.matspecular);
            }

            /* We would like to find a way to put matspecular into
             * GX_SetChanMatColor(GX_COLOR0A0), since that's the color that GX
             * combines with the specular light. But we also need this register
             * for the ambient color, which is arguably more important, so we
             * give it higher priority. */
            if (light_mask.ambient_mask) {
                GX_SetChanMatColor(GX_COLOR0A0, acol);
            } else {
                GX_SetChanMatColor(GX_COLOR0A0, scol);
            }
            GX_SetChanMatColor(GX_COLOR1A1, dcol);
        }

        GXColor ecol;
        if (glparamstate.lighting.color_material_enabled &&
            glparamstate.lighting.color_material_mode == GL_EMISSION) {
            ecol = gxcol_new_fv(glparamstate.imm_mode.current_color);
        } else {
            ecol = gxcol_new_fv(glparamstate.lighting.matemission);
        };

        // Color0 channel: Multiplies the light raster result with the vertex color. Ambient is set to register (which is global ambient)
        GX_SetChanCtrl(GX_COLOR0A0, GX_TRUE, GX_SRC_REG, vert_color_src,
                       light_mask.ambient_mask | light_mask.specular_mask , GX_DF_NONE, GX_AF_SPEC);
        GX_SetChanAmbColor(GX_COLOR0A0, color_gamb);

        // Color1 channel: Multiplies the light raster result with the vertex color. Ambient is set to register (which is black)
        GX_SetChanCtrl(GX_COLOR1A1, GX_TRUE, GX_SRC_REG, vert_color_src, light_mask.diffuse_mask, GX_DF_CLAMP, GX_AF_SPOT);
        GX_SetChanAmbColor(GX_COLOR1A1, color_black);

        // STAGE 0: ambient*vert_color -> cprev
        // In data: d: Raster Color, a: emission color
        u8 emission_reg = ogx_gpu_resources->tevreg_first++;
        GX_SetTevColor(GX_TEVREG0 + emission_reg, ecol);
        /* Multiply by two because there are alpha registers in between */
        GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_C0 + emission_reg * 2,
                         GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
        GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
        // Operation: Pass d
        GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        // Select COLOR0A0 for the rasterizer, disable all textures
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_DISABLE, GX_COLOR0A0);

        // STAGE 1: diffuse*vert_color + cprev -> cprev
        // In data: d: Raster Color a: CPREV
        GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_CPREV, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
        GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_RASA, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
        // Operation: Sum a + d
        GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, raster_output);
        GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, raster_output);
        // Select COLOR1A1 for the rasterizer, disable all textures
        GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORDNULL, GX_TEXMAP_DISABLE, GX_COLOR1A1);

        if (glparamstate.texture_enabled) {
            // Do not select any raster color channel
            _ogx_setup_texture_stages(raster_reg_index, GX_COLORNULL);
        }
    } else {
        // Unlit scene
        // TEV STAGE 0: Modulate the vertex color with the texture 0. Outputs to GX_TEVPREV
        /* Optimization: If color_enabled is false (constant vertex color) use
         * the material color register instead of emitting a color for each
         * vertex */
        uint8_t material_source;
        if (glparamstate.cs.color_enabled) {
            material_source = GX_SRC_VTX;
        } else {
            // Load the constant color (current GL color)
            GXColor ccol = gxcol_new_fv(glparamstate.imm_mode.current_color);
            GX_SetChanMatColor(GX_COLOR0A0, ccol);
            material_source = GX_SRC_REG;
        }

        GX_SetNumChans(1);

        // Disable lighting and output vertex color to the rasterized color
        GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, material_source, 0, 0, 0);

        if (glparamstate.texture_enabled) {
            // Select COLOR0A0 for the rasterizer, Texture 0 for texture rasterizer and TEXCOORD0 slot for tex coordinates
            _ogx_setup_texture_stages(raster_reg_index, GX_COLOR0A0);
        } else {
            // Use one stage only
            ogx_gpu_resources->tevstage_first += 1;
            // In data: d: Raster Color
            GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
            GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
            // Operation: Pass the color
            GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
            GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
            // Select COLOR0A0 for the rasterizer, Texture 0 for texture rasterizer and TEXCOORD0 slot for tex coordinates
            GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_DISABLE, GX_COLOR0A0);
        }
    }

    bool should_draw = setup_common_stages();
    glparamstate.dirty.bits.dirty_tev = false;
    return should_draw;
}

void _ogx_update_matrices_fixed_pipeline()
{
    update_modelview_matrix();
    update_projection_matrix();
    update_normal_matrix();
}

void _ogx_apply_state()
{
    // Set up the OGL state to GX state
    if (glparamstate.dirty.bits.dirty_z)
        GX_SetZMode(glparamstate.ztest, glparamstate.zfunc, glparamstate.zwrite & glparamstate.ztest);

    if (glparamstate.dirty.bits.dirty_color_update) {
        GX_SetColorUpdate(glparamstate.color_update ? GX_TRUE : GX_FALSE);
    }

    if (glparamstate.dirty.bits.dirty_blend) {
        if (glparamstate.blendenabled)
            GX_SetBlendMode(GX_BM_BLEND, glparamstate.srcblend, glparamstate.dstblend, GX_LO_CLEAR);
        else
            GX_SetBlendMode(GX_BM_NONE, glparamstate.srcblend, glparamstate.dstblend, GX_LO_CLEAR);
    }

    if (glparamstate.dirty.bits.dirty_alphatest ||
        glparamstate.dirty.bits.dirty_tev) {
        u8 params[4] = { GX_ALWAYS, 0, GX_ALWAYS, 0 };
        int comparisons = 0;
        if (glparamstate.alphatest_enabled) {
            params[0] = glparamstate.alpha_func;
            params[1] = glparamstate.alpha_ref;
            comparisons++;
        }
        if (glparamstate.stencil.enabled || glparamstate.clip_plane_mask) {
            params[comparisons * 2] = GX_GREATER;
            /* The reference value is initialized to 0, which is the value we
             * want */
            comparisons++;
        }
        GX_SetZCompLoc(comparisons > 0 ? GX_DISABLE : GX_ENABLE);
        GX_SetAlphaCompare(params[0], params[1], GX_AOP_AND, params[2], params[3]);
    }

    if (glparamstate.dirty.bits.dirty_cull) {
        setup_cull_mode();
    }

    /* Though glFog*() functions can be replaced by shaders, the OpenGL spec
     * allows them to be used even when shaders are active. */
    if (glparamstate.dirty.bits.dirty_fog) {
        setup_fog();
        glparamstate.dirty.bits.dirty_fog = 0;
    }

    if (glparamstate.dirty.bits.dirty_scissor) {
        update_scissor();
    }

    if (glparamstate.dirty.bits.dirty_viewport) {
        update_viewport();
    }

    /* Reset the updated bits to 0. We don't unconditionally reset everything
     * to 0 because some states might still be dirty: for example, the stencil
     * checks alters the texture coordinate generation. */
    glparamstate.dirty.bits.dirty_cull = 0;
    glparamstate.dirty.bits.dirty_alphatest = 0;
    glparamstate.dirty.bits.dirty_blend = 0;
    glparamstate.dirty.bits.dirty_color_update = 0;
    glparamstate.dirty.bits.dirty_z = 0;
}

static void draw_arrays_general(const OgxDrawData *draw_data)
{
    GLsizei count = draw_data->count;
    GLsizei first = draw_data->first;
    // Invalidate vertex data as may have been modified by the user
    GX_InvVtxCache();

    bool loop = draw_data->gxmode.loop;
    GX_Begin(draw_data->gxmode.mode, GX_VTXFMT0, count + loop);
    int i;
    for (i = 0; i < count + loop; i++) {
        int j = i % count + first;
        _ogx_arrays_process_element(j);
    }
    GX_End();
}

static void flat_draw_geometry(void *cb_data)
{
    OgxDrawData *data = cb_data;

    _ogx_arrays_setup_draw(data, OGX_DRAW_FLAG_FLAT);
    /* TODO: we could use C++ templates here too, to build more effective
     * drawing functions that only process the data we need. */
    draw_arrays_general(data);
}

static void draw_elements_general(const OgxDrawData *draw_data)
{
    const GLvoid *indices = draw_data->indices;
    GLsizei count = draw_data->count;

    // Invalidate vertex data as may have been modified by the user
    GX_InvVtxCache();

    if (glparamstate.bound_vbo_element_array) {
        indices = _ogx_vbo_get_data(glparamstate.bound_vbo_element_array,
                                    indices);
    }

    bool loop = draw_data->gxmode.loop;
    GX_Begin(draw_data->gxmode.mode, GX_VTXFMT0, count + loop);
    for (int i = 0; i < count + loop; i++) {
        int index = read_index(indices, draw_data->type, i % count);
        _ogx_arrays_process_element(index);
    }
    GX_End();
}

static void flat_draw_elements(void *cb_data)
{
    OgxDrawData *data = cb_data;

    _ogx_arrays_setup_draw(data, OGX_DRAW_FLAG_FLAT);
    draw_elements_general(data);
}

void glArrayElement(GLint i)
{
    float value[3];

    if (glparamstate.dirty.bits.dirty_attributes) {
        /* The draw mode is not really relevant here, since the actual drawing
         * is performed in glEnd(), at which time we'll take care of handling
         * point sprites (if enabled). */
        OgxDrawMode mode = { GX_TRIANGLES, true };
        _ogx_update_vertex_array_readers(mode);
    }

    OgxArrayReader *reader = _ogx_array_reader_for_attribute(GX_VA_NRM);
    if (reader) {
        _ogx_array_reader_read_norm3f(reader, i, value);
        glNormal3fv(value);
    }

    for (int tex = 0; tex < MAX_TEXTURE_UNITS; tex++) {
        reader = _ogx_array_reader_for_attribute(GX_VA_TEX0 + tex);
        if (reader) {
            _ogx_array_reader_read_tex2f(reader, i, value);
            glMultiTexCoord2fv(GL_TEXTURE0 + tex, value);
        }
    }

    reader = _ogx_array_reader_for_attribute(GX_VA_CLR0);
    if (reader) {
        GXColor color;
        _ogx_array_reader_read_color(reader, i, &color);
        glColor4ub(color.r, color.g, color.b, color.a);
    }

    reader = _ogx_array_reader_for_attribute(GX_VA_POS);
    if (reader) {
        _ogx_array_reader_read_pos3f(reader, i, value);
        glVertex3fv(value);
    }
}

static bool setup_draw(const OgxDrawData *draw_data)
{
    _ogx_efb_set_content_type(OGX_EFB_SCENE);

    if (!glparamstate.current_program) {
        _ogx_arrays_setup_draw(draw_data, OGX_DRAW_FLAG_NONE);

        /* Note that _ogx_setup_render_stages() uses some information from the
         * vertex arrays computed by _ogx_arrays_setup_draw(), so it must be called
         * after it. */
        bool should_draw = _ogx_setup_render_stages();
        if (!should_draw) return false;
    } else {
        _ogx_shader_setup_draw(draw_data);
        if (!setup_common_stages()) return false;
    }
    _ogx_apply_state();
    return true;
}

void _ogx_update_matrices()
{
    if (glparamstate.dirty.bits.dirty_matrices) {
        glparamstate.update_matrices();
        glparamstate.dirty.bits.dirty_matrices = 0;
    }
}

static void draw_done()
{
    _ogx_arrays_draw_done();
    _ogx_shader_draw_done();
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    OgxDrawMode gxmode = _ogx_draw_mode(mode);
    if (gxmode.mode == 0xff)
        return;

    HANDLE_CALL_LIST(DRAW_ARRAYS, mode, first, count);

    if (glparamstate.dirty.bits.dirty_attributes ||
        /* Point sprites need special handling */
        point_sprites_changed(gxmode.mode))
        _ogx_update_vertex_array_readers(gxmode);

    /* If VBOs are in use, make sure their data has been updated */
    ppcsync();

    _ogx_update_matrices();
    OgxDrawData draw_data = { gxmode, count, first, };
    if (glparamstate.stencil.enabled) {
        _ogx_gpu_resources_push();
        _ogx_stencil_draw(flat_draw_geometry, &draw_data);
        _ogx_gpu_resources_pop();
    }

    _ogx_gpu_resources_push();

    bool should_draw = setup_draw(&draw_data);
    if (should_draw) {
        draw_arrays_general(&draw_data);
        glparamstate.draw_count++;
    }
    draw_done();

    _ogx_gpu_resources_pop();
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    OgxDrawMode gxmode = _ogx_draw_mode(mode);
    if (gxmode.mode == 0xff)
        return;

    HANDLE_CALL_LIST(DRAW_ELEMENTS, mode, count, type, indices);

    if (glparamstate.dirty.bits.dirty_attributes ||
        /* Point sprites need special handling */
        point_sprites_changed(gxmode.mode))
        _ogx_update_vertex_array_readers(gxmode);

    /* If VBOs are in use, make sure their data has been updated */
    ppcsync();

    _ogx_update_matrices();
    OgxDrawData draw_data = { gxmode, count, 0, type, indices };
    if (glparamstate.stencil.enabled) {
        _ogx_gpu_resources_push();
        _ogx_stencil_draw(flat_draw_elements, &draw_data);
        _ogx_gpu_resources_pop();
    }

    _ogx_gpu_resources_push();

    bool should_draw = setup_draw(&draw_data);
    if (should_draw) {
        draw_elements_general(&draw_data);
        glparamstate.draw_count++;
    }
    draw_done();

    _ogx_gpu_resources_pop();
}

void glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
               GLdouble near, GLdouble far)
{
    float mt[16];
    f32 tmp;

    tmp = 1.0f / (right - left);
    mt[0] = (2 * near) * tmp;
    mt[4] = 0.0f;
    mt[8] = (right + left) * tmp;
    mt[12] = 0.0f;
    tmp = 1.0f / (top - bottom);
    mt[1] = 0.0f;
    mt[5] = (2 * near) * tmp;
    mt[9] = (top + bottom) * tmp;
    mt[13] = 0.0f;
    tmp = 1.0f / (far - near);
    mt[2] = 0.0f;
    mt[6] = 0.0f;
    mt[10] = -(far + near) * tmp;
    mt[14] = -2.0 * (far * near) * tmp;
    mt[3] = 0.0f;
    mt[7] = 0.0f;
    mt[11] = -1.0f;
    mt[15] = 0.0f;

    glMultMatrixf(mt);
}

void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val)
{
    Mtx44 newmat;
    // Same as GX's guOrtho, but transposed
    float x = (left + right) / (left - right);
    float y = (bottom + top) / (bottom - top);
    float z = (near_val + far_val) / (near_val - far_val);
    newmat[0][0] = 2.0f / (right - left);
    newmat[1][0] = 0.0f;
    newmat[2][0] = 0.0f;
    newmat[3][0] = x;
    newmat[0][1] = 0.0f;
    newmat[1][1] = 2.0f / (top - bottom);
    newmat[2][1] = 0.0f;
    newmat[3][1] = y;
    newmat[0][2] = 0.0f;
    newmat[1][2] = 0.0f;
    newmat[2][2] = 2.0f / (near_val - far_val);
    newmat[3][2] = z;
    newmat[0][3] = 0.0f;
    newmat[1][3] = 0.0f;
    newmat[2][3] = 0.0f;
    newmat[3][3] = 1.0f;

    glMultMatrixf((float *)newmat);
}

// NOT GOING TO IMPLEMENT

void glBlendEquation(GLenum mode) {}
void glShadeModel(GLenum mode) {}  // In theory we don't have GX equivalent?
void glHint(GLenum target, GLenum mode) {}

// TODO STUB IMPLEMENTATION

void glLineStipple(GLint factor, GLushort pattern) {}
void glPolygonStipple(const GLubyte *mask) {}
void glLightModelf(GLenum pname, GLfloat param) {}
void glLightModeli(GLenum pname, GLint param) {}
void glPushAttrib(GLbitfield mask) {}
void glPopAttrib(void) {}
void glPushClientAttrib(GLbitfield mask) {}
void glPopClientAttrib(void) {}

/*
 ****** NOTES ******

 Front face definition is reversed. CCW is front for OpenGL
 while front facing is defined CW in GX.

 This implementation ONLY supports floats for vertexs, texcoords
 and normals. Support for different types is not implemented as
 GX does only support floats. Simple conversion would be needed.

*/
