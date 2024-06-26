
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

#include "call_lists.h"
#include "debug.h"
#include "opengx.h"
#include "selection.h"
#include "state.h"
#include "utils.h"

#include <GL/gl.h>
#include <gctypes.h>
#include <malloc.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

glparams_ _ogx_state;

typedef struct
{
    uint8_t ambient_mask;
    uint8_t diffuse_mask;
    uint8_t specular_mask;
} LightMasks;

static const GLubyte gl_null_string[1] = { 0 };
char _ogx_log_level = 0;
static GXTexObj s_zbuffer_texture;
static uint8_t s_zbuffer_texels[2 * 32] ATTRIBUTE_ALIGN(32);

static void draw_arrays_general(int first, int count, int ne,
                                int color_provide, int texen, bool loop);

#define MODELVIEW_UPDATE                                           \
    {                                                              \
        Mtx trans;                                                 \
        model_view_matrix_to_gx(trans);                            \
        GX_LoadPosMtxImm(trans, GX_PNMTX3);                        \
        GX_SetCurrentMtx(GX_PNMTX3);                               \
    }

/* OpenGL's projection matrix transform the scene into a clip space where all
 * the coordinates lie in the range [-1, 1]. Nintendo's GX, however, for the z
 * coordinates expects a range of [-1, 0], so the projection matrix needs to be
 * adjusted. We do that by extracting the near and far planes from the GL
 * projection matrix and by recomputing the related two matrix entries
 * according to the formulas used by guFrustum() and guOrtho(). */
#define PROJECTION_UPDATE                                           \
    {                                                               \
        Mtx44 proj;                                                 \
        u8 type;                                                    \
        float near, far;                                            \
        get_projection_info(&type, &near, &far);                    \
        for (int i = 0; i < 4; i++)                                 \
            for (int j = 0; j < 4; j++)                             \
                proj[i][j] = glparamstate.projection_matrix[j][i];  \
        float tmp = 1.0f / (far - near);                            \
        if (glparamstate.projection_matrix[3][3] != 0) {            \
            proj[2][2] = -tmp;                                      \
            proj[2][3] = -far * tmp;                                \
            GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);            \
        } else {                                                    \
            proj[2][2] = -near * tmp;                               \
            proj[2][3] = -near * far * tmp;                         \
            GX_LoadProjectionMtx(proj, GX_PERSPECTIVE);             \
        }                                                           \
    }

#define NORMAL_UPDATE                                                  \
    {                                                                  \
        int i, j;                                                      \
        Mtx mvinverse, normalm, modelview;                             \
        for (i = 0; i < 3; i++)                                        \
            for (j = 0; j < 4; j++)                                    \
                modelview[i][j] = glparamstate.modelview_matrix[j][i]; \
                                                                       \
        guMtxInverse(modelview, mvinverse);                            \
        guMtxTranspose(mvinverse, normalm);                            \
        GX_LoadNrmMtxImm(normalm, GX_PNMTX3);                          \
    }

static inline void model_view_matrix_to_gx(Mtx mv)
{
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++)
            mv[i][j] = glparamstate.modelview_matrix[j][i];
}

/* Deduce the projection type (perspective vs orthogonal) and the values of the
 * near and far clipping plane from the projection matrix. */
static void get_projection_info(u8 *type, float *near, float *far)
{
    float A, B;

    A = glparamstate.projection_matrix[2][2];
    /* Note that the matrix is transposed: this is row 2, column 3 */
    B = glparamstate.projection_matrix[3][2];

    if (glparamstate.projection_matrix[3][3] == 0) {
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

int ogx_prepare_swap_buffers()
{
    return glparamstate.render_mode == GL_RENDER ? 0 : -1;
}

void ogx_initialize()
{
    _ogx_log_init();

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
    glparamstate.clearz = 1.0f;

    glparamstate.ztest = GX_FALSE; // Depth test disabled but z write enabled
    glparamstate.zfunc = GX_LESS;  // Although write is efectively disabled
    glparamstate.zwrite = GX_TRUE; // unless test is enabled

    glparamstate.matrixmode = 1; // Modelview default mode
    glparamstate.glcurtex = 0;   // Default texture is 0 (nonstardard)
    GX_SetNumChans(1);           // One modulation color (as glColor)
    glDisable(GL_TEXTURE_2D);

    glparamstate.glcullmode = GL_BACK;
    glparamstate.render_mode = GL_RENDER;
    glparamstate.cullenabled = 0;
    glparamstate.alpha_func = GX_ALWAYS;
    glparamstate.alpha_ref = 0;
    glparamstate.alphatest_enabled = 0;
    glparamstate.frontcw = 0; // By default front is CCW
    glparamstate.texture_env_mode = GL_MODULATE;
    glparamstate.texture_gen_mode = GL_EYE_LINEAR;
    glparamstate.texture_gen_enabled = 0;
    /* All the other plane elements should be set to 0.0f */
    glparamstate.texture_eye_plane_s[0] = 1.0f;
    glparamstate.texture_eye_plane_t[1] = 1.0f;
    glparamstate.texture_object_plane_s[0] = 1.0f;
    glparamstate.texture_object_plane_t[1] = 1.0f;

    glparamstate.cur_proj_mat = -1;
    glparamstate.cur_modv_mat = -1;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    /* Load the identity matrix into GX_PNMTX0 */
    Mtx mv;
    guMtxIdentity(mv);
    GX_LoadPosMtxImm(mv, GX_PNMTX0);

    glparamstate.imm_mode.current_color[0] = 1.0f; // Default imm data, could be wrong
    glparamstate.imm_mode.current_color[1] = 1.0f;
    glparamstate.imm_mode.current_color[2] = 1.0f;
    glparamstate.imm_mode.current_color[3] = 1.0f;
    glparamstate.imm_mode.current_texcoord[0] = 0;
    glparamstate.imm_mode.current_texcoord[1] = 0;
    glparamstate.imm_mode.current_normal[0] = 0;
    glparamstate.imm_mode.current_normal[1] = 0;
    glparamstate.imm_mode.current_normal[2] = 1.0f;
    glparamstate.imm_mode.current_numverts = 0;
    glparamstate.imm_mode.in_gl_begin = 0;

    glparamstate.cs.vertex_enabled = 0; // DisableClientState on everything
    glparamstate.cs.normal_enabled = 0;
    glparamstate.cs.texcoord_enabled = 0;
    glparamstate.cs.index_enabled = 0;
    glparamstate.cs.color_enabled = 0;

    glparamstate.texture_enabled = 0;
    glparamstate.pack_alignment = 4;
    glparamstate.unpack_alignment = 4;

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

    glparamstate.error = GL_NO_ERROR;

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
}

void _ogx_setup_2D_projection()
{
    /* GX_PNMTX0 is fixed to be the identity matrix */
    GX_SetCurrentMtx(GX_PNMTX0);

    Mtx44 proj;
    /* The 0.5f is to center the drawing into the pixels. */
    float left = glparamstate.viewport[0] + 0.5f;
    float top = glparamstate.viewport[1] + 0.5f;
    guOrtho(proj,
            top, top + (glparamstate.viewport[3] - 1),
            left, left + (glparamstate.viewport[2] - 1),
            0, 1);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);

    glparamstate.dirty.bits.dirty_matrices = 1;
}

void glEnable(GLenum cap)
{ // TODO
    HANDLE_CALL_LIST(ENABLE, cap);

    switch (cap) {
    case GL_TEXTURE_2D:
        glparamstate.texture_enabled = 1;
        break;
    case GL_TEXTURE_GEN_S:
        glparamstate.texture_gen_enabled |= OGX_TEXGEN_S;
        glparamstate.dirty.bits.dirty_texture_gen = 1;
        break;
    case GL_TEXTURE_GEN_T:
        glparamstate.texture_gen_enabled |= OGX_TEXGEN_T;
        glparamstate.dirty.bits.dirty_texture_gen = 1;
        break;
    case GL_TEXTURE_GEN_R:
        glparamstate.texture_gen_enabled |= OGX_TEXGEN_R;
        glparamstate.dirty.bits.dirty_texture_gen = 1;
        break;
    case GL_TEXTURE_GEN_Q:
        glparamstate.texture_gen_enabled |= OGX_TEXGEN_Q;
        glparamstate.dirty.bits.dirty_texture_gen = 1;
        break;
    case GL_COLOR_MATERIAL:
        glparamstate.lighting.color_material_enabled = 1;
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
    case GL_DEPTH_TEST:
        glparamstate.ztest = GX_TRUE;
        glparamstate.dirty.bits.dirty_z = 1;
        break;
    case GL_FOG:
        glparamstate.fog.enabled = 1;
        break;
    case GL_LIGHTING:
        glparamstate.lighting.enabled = 1;
        glparamstate.dirty.bits.dirty_lighting = 1;
        break;
    case GL_LIGHT0:
    case GL_LIGHT1:
    case GL_LIGHT2:
    case GL_LIGHT3:
        glparamstate.lighting.lights[cap - GL_LIGHT0].enabled = 1;
        glparamstate.dirty.bits.dirty_lighting = 1;
        break;
    default:
        break;
    }
}

void glDisable(GLenum cap)
{ // TODO
    HANDLE_CALL_LIST(DISABLE, cap);

    switch (cap) {
    case GL_TEXTURE_2D:
        glparamstate.texture_enabled = 0;
        break;
    case GL_TEXTURE_GEN_S:
        glparamstate.texture_gen_enabled &= ~OGX_TEXGEN_S;
        glparamstate.dirty.bits.dirty_texture_gen = 1;
        break;
    case GL_TEXTURE_GEN_T:
        glparamstate.texture_gen_enabled &= ~OGX_TEXGEN_T;
        glparamstate.dirty.bits.dirty_texture_gen = 1;
        break;
    case GL_TEXTURE_GEN_R:
        glparamstate.texture_gen_enabled &= ~OGX_TEXGEN_R;
        glparamstate.dirty.bits.dirty_texture_gen = 1;
        break;
    case GL_TEXTURE_GEN_Q:
        glparamstate.texture_gen_enabled &= ~OGX_TEXGEN_Q;
        glparamstate.dirty.bits.dirty_texture_gen = 1;
        break;
    case GL_COLOR_MATERIAL:
        glparamstate.lighting.color_material_enabled = 0;
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
    case GL_DEPTH_TEST:
        glparamstate.ztest = GX_FALSE;
        glparamstate.dirty.bits.dirty_z = 1;
        break;
    case GL_LIGHTING:
        glparamstate.lighting.enabled = 0;
        glparamstate.dirty.bits.dirty_lighting = 1;
        break;
    case GL_LIGHT0:
    case GL_LIGHT1:
    case GL_LIGHT2:
    case GL_LIGHT3:
        glparamstate.lighting.lights[cap - GL_LIGHT0].enabled = 0;
        glparamstate.dirty.bits.dirty_lighting = 1;
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
        break;
    }
}

void glFogfv(GLenum pname, const GLfloat *params)
{
    switch (pname) {
    case GL_FOG_MODE:
    case GL_FOG_DENSITY:
    case GL_FOG_START:
    case GL_FOG_END:
        glFogf(pname, params[0]);
        break;
    case GL_FOG_COLOR:
        floatcpy(glparamstate.fog.color, params, 4);
        break;
    }
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
    glparamstate.dirty.bits.dirty_lighting = 1;
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
        {
            float modv[3][4];
            int i;
            int j;
            for (i = 0; i < 3; i++)
                for (j = 0; j < 4; j++)
                    modv[i][j] = glparamstate.modelview_matrix[j][i];
            guVecMultiply(modv, (guVector *)glparamstate.lighting.lights[lnum].position, (guVector *)glparamstate.lighting.lights[lnum].position);
        }
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
    glparamstate.dirty.bits.dirty_lighting = 1;
}

void glLightModelfv(GLenum pname, const GLfloat *params)
{
    switch (pname) {
    case GL_LIGHT_MODEL_AMBIENT:
        floatcpy(glparamstate.lighting.globalambient, params, 4);
        break;
    }
    glparamstate.dirty.bits.dirty_material = 1;
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
    glparamstate.dirty.bits.dirty_material = 1;
};

void glColorMaterial(GLenum face, GLenum mode)
{
    /* TODO: support the face parameter */
    glparamstate.lighting.color_material_mode = mode;
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

void glBegin(GLenum mode)
{
    // Just discard all the data!
    glparamstate.imm_mode.current_numverts = 0;
    glparamstate.imm_mode.prim_type = mode;
    glparamstate.imm_mode.in_gl_begin = 1;
    glparamstate.imm_mode.has_color = 0;
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
    struct client_state cs_backup = glparamstate.cs;
    float *base = &glparamstate.imm_mode.current_vertices[0][0];
    int stride = 12 * sizeof(float);
    _ogx_array_reader_init(&glparamstate.texcoord_array, base, GL_FLOAT, stride);
    _ogx_array_reader_set_num_elements(&glparamstate.texcoord_array, 2);
    base += 2;
    _ogx_array_reader_init(&glparamstate.color_array, base, GL_FLOAT, stride);
    _ogx_array_reader_set_num_elements(&glparamstate.color_array, 4);
    base += 4;
    _ogx_array_reader_init(&glparamstate.normal_array, base, GL_FLOAT, stride);
    _ogx_array_reader_set_num_elements(&glparamstate.normal_array, 3);
    base += 3;
    _ogx_array_reader_init(&glparamstate.vertex_array, base, GL_FLOAT, stride);
    _ogx_array_reader_set_num_elements(&glparamstate.vertex_array, 3);
    glparamstate.cs.texcoord_enabled = 1;
    glparamstate.cs.color_enabled = glparamstate.imm_mode.has_color;
    glparamstate.cs.normal_enabled = 1;
    glparamstate.cs.vertex_enabled = 1;
    glDrawArrays(glparamstate.imm_mode.prim_type, 0, glparamstate.imm_mode.current_numverts);
    glparamstate.cs = cs_backup;
    glparamstate.imm_mode.in_gl_begin = 0;
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    glparamstate.viewport[0] = x;
    glparamstate.viewport[1] = y;
    glparamstate.viewport[2] = width;
    glparamstate.viewport[3] = height;
    GX_SetViewport(x, y, width, height, 0.0f, 1.0f);
    GX_SetScissor(x, y, width, height);
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    GX_SetScissor(x, y, width, height);
}

void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a)
{
    if (glparamstate.imm_mode.in_gl_begin)
        glparamstate.imm_mode.has_color = 1;
    glparamstate.imm_mode.current_color[0] = r / 255.0f;
    glparamstate.imm_mode.current_color[1] = g / 255.0f;
    glparamstate.imm_mode.current_color[2] = b / 255.0f;
    glparamstate.imm_mode.current_color[3] = a / 255.0f;
}
void glColor4ubv(const GLubyte *color)
{
    if (glparamstate.imm_mode.in_gl_begin)
        glparamstate.imm_mode.has_color = 1;
    glparamstate.imm_mode.current_color[0] = color[0] / 255.0f;
    glparamstate.imm_mode.current_color[1] = color[1] / 255.0f;
    glparamstate.imm_mode.current_color[2] = color[2] / 255.0f;
    glparamstate.imm_mode.current_color[3] = color[3] / 255.0f;
}
void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    if (glparamstate.imm_mode.in_gl_begin)
        glparamstate.imm_mode.has_color = 1;
    glparamstate.imm_mode.current_color[0] = clampf_01(red);
    glparamstate.imm_mode.current_color[1] = clampf_01(green);
    glparamstate.imm_mode.current_color[2] = clampf_01(blue);
    glparamstate.imm_mode.current_color[3] = clampf_01(alpha);
}

void glColor3f(GLfloat red, GLfloat green, GLfloat blue)
{
    if (glparamstate.imm_mode.in_gl_begin)
        glparamstate.imm_mode.has_color = 1;
    glparamstate.imm_mode.current_color[0] = clampf_01(red);
    glparamstate.imm_mode.current_color[1] = clampf_01(green);
    glparamstate.imm_mode.current_color[2] = clampf_01(blue);
    glparamstate.imm_mode.current_color[3] = 1.0f;
}

void glColor4fv(const GLfloat *v)
{
    if (glparamstate.imm_mode.in_gl_begin)
        glparamstate.imm_mode.has_color = 1;
    glparamstate.imm_mode.current_color[0] = clampf_01(v[0]);
    glparamstate.imm_mode.current_color[1] = clampf_01(v[1]);
    glparamstate.imm_mode.current_color[2] = clampf_01(v[2]);
    glparamstate.imm_mode.current_color[3] = clampf_01(v[3]);
}

void glColor3ub(GLubyte red, GLubyte green, GLubyte blue)
{
    glColor3f(red / 255.0f, green / 255.0f, blue / 255.0f);
}

void glColor3fv(const GLfloat *v)
{
    glColor3f(v[0], v[1], v[2]);
}

void glTexCoord2d(GLdouble u, GLdouble v)
{
    glTexCoord2f(u, v);
}

void glTexCoord2f(GLfloat u, GLfloat v)
{
    glparamstate.imm_mode.current_texcoord[0] = u;
    glparamstate.imm_mode.current_texcoord[1] = v;
}

void glTexCoord2i(GLint s, GLint t)
{
    glTexCoord2f(s, t);
}

void glTexCoord3f(GLfloat s, GLfloat t, GLfloat r)
{
    glTexCoord2f(s, t);
    if (r != 0.0) {
        warning("glTexCoord3f not supported");
    }
}

void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz)
{
    glparamstate.imm_mode.current_normal[0] = nx;
    glparamstate.imm_mode.current_normal[1] = ny;
    glparamstate.imm_mode.current_normal[2] = nz;
}

void glNormal3fv(const GLfloat *v)
{
    glparamstate.imm_mode.current_normal[0] = v[0];
    glparamstate.imm_mode.current_normal[1] = v[1];
    glparamstate.imm_mode.current_normal[2] = v[2];
}

void glVertex2d(GLdouble x, GLdouble y)
{
    glVertex3f(x, y, 0.0f);
}

void glVertex2i(GLint x, GLint y)
{
    glVertex3f(x, y, 0.0f);
}

void glVertex2f(GLfloat x, GLfloat y)
{
    glVertex3f(x, y, 0.0f);
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
        glparamstate.imm_mode.current_vertices = new_buffer;
    }

    // GL_T2F_C4F_N3F_V3F
    float *vert = glparamstate.imm_mode.current_vertices[glparamstate.imm_mode.current_numverts++];
    vert[0] = glparamstate.imm_mode.current_texcoord[0];
    vert[1] = glparamstate.imm_mode.current_texcoord[1];

    floatcpy(vert + 2, glparamstate.imm_mode.current_color, 4);

    floatcpy(vert + 6, glparamstate.imm_mode.current_normal, 3);

    vert[9] = x;
    vert[10] = y;
    vert[11] = z;
}

void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    glVertex3f(x / w, y / w, z / w);
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
    case 1:
        if (glparamstate.cur_modv_mat < 0) {
            set_error(GL_STACK_UNDERFLOW);
            return;
        }
        memcpy(glparamstate.modelview_matrix, glparamstate.modelview_stack[glparamstate.cur_modv_mat], sizeof(Mtx44));
        glparamstate.cur_modv_mat--;
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
        memcpy(glparamstate.modelview_stack[glparamstate.cur_modv_mat], glparamstate.modelview_matrix, sizeof(Mtx44));
        break;
    default:
        break;
    }
}
void glLoadMatrixf(const GLfloat *m)
{
    switch (glparamstate.matrixmode) {
    case 0:
        memcpy(glparamstate.projection_matrix, m, sizeof(Mtx44));
        break;
    case 1:
        memcpy(glparamstate.modelview_matrix, m, sizeof(Mtx44));
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
    Mtx44 curr;

    HANDLE_CALL_LIST(MULT_MATRIX, m);

    switch (glparamstate.matrixmode) {
    case 0:
        memcpy((float *)curr, &glparamstate.projection_matrix[0][0], sizeof(Mtx44));
        gl_matrix_multiply(&glparamstate.projection_matrix[0][0], (float *)curr, (float *)m);
        break;
    case 1:
        memcpy((float *)curr, &glparamstate.modelview_matrix[0][0], sizeof(Mtx44));
        gl_matrix_multiply(&glparamstate.modelview_matrix[0][0], (float *)curr, (float *)m);
        break;
    default:
        break;
    }
    glparamstate.dirty.bits.dirty_matrices = 1;
}
void glLoadIdentity()
{
    float *mtrx;

    HANDLE_CALL_LIST(LOAD_IDENTITY);

    switch (glparamstate.matrixmode) {
    case 0:
        mtrx = &glparamstate.projection_matrix[0][0];
        break;
    case 1:
        mtrx = &glparamstate.modelview_matrix[0][0];
        break;
    default:
        return;
    }

    mtrx[0] = 1.0f;
    mtrx[1] = 0.0f;
    mtrx[2] = 0.0f;
    mtrx[3] = 0.0f;
    mtrx[4] = 0.0f;
    mtrx[5] = 1.0f;
    mtrx[6] = 0.0f;
    mtrx[7] = 0.0f;
    mtrx[8] = 0.0f;
    mtrx[9] = 0.0f;
    mtrx[10] = 1.0f;
    mtrx[11] = 0.0f;
    mtrx[12] = 0.0f;
    mtrx[13] = 0.0f;
    mtrx[14] = 0.0f;
    mtrx[15] = 1.0f;

    glparamstate.dirty.bits.dirty_matrices = 1;
}
void glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    HANDLE_CALL_LIST(SCALE, x, y, z);

    Mtx44 newmat;
    Mtx44 curr;
    newmat[0][0] = x;
    newmat[0][1] = 0.0f;
    newmat[0][2] = 0.0f;
    newmat[0][3] = 0.0f;
    newmat[1][0] = 0.0f;
    newmat[1][1] = y;
    newmat[1][2] = 0.0f;
    newmat[1][3] = 0.0f;
    newmat[2][0] = 0.0f;
    newmat[2][1] = 0.0f;
    newmat[2][2] = z;
    newmat[2][3] = 0.0f;
    newmat[3][0] = 0.0f;
    newmat[3][1] = 0.0f;
    newmat[3][2] = 0.0f;
    newmat[3][3] = 1.0f;

    switch (glparamstate.matrixmode) {
    case 0:
        memcpy((float *)curr, &glparamstate.projection_matrix[0][0], sizeof(Mtx44));
        gl_matrix_multiply(&glparamstate.projection_matrix[0][0], (float *)curr, (float *)newmat);
        break;
    case 1:
        memcpy((float *)curr, &glparamstate.modelview_matrix[0][0], sizeof(Mtx44));
        gl_matrix_multiply(&glparamstate.modelview_matrix[0][0], (float *)curr, (float *)newmat);
        break;
    default:
        break;
    }

    glparamstate.dirty.bits.dirty_matrices = 1;
}

void glTranslated(GLdouble x, GLdouble y, GLdouble z)
{
    glTranslatef(x, y, z);
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    HANDLE_CALL_LIST(TRANSLATE, x, y, z);

    Mtx44 newmat;
    Mtx44 curr;
    newmat[0][0] = 1.0f;
    newmat[0][1] = 0.0f;
    newmat[0][2] = 0.0f;
    newmat[0][3] = 0.0f;
    newmat[1][0] = 0.0f;
    newmat[1][1] = 1.0f;
    newmat[1][2] = 0.0f;
    newmat[1][3] = 0.0f;
    newmat[2][0] = 0.0f;
    newmat[2][1] = 0.0f;
    newmat[2][2] = 1.0f;
    newmat[2][3] = 0.0f;
    newmat[3][0] = x;
    newmat[3][1] = y;
    newmat[3][2] = z;
    newmat[3][3] = 1.0f;

    switch (glparamstate.matrixmode) {
    case 0:
        memcpy((float *)curr, &glparamstate.projection_matrix[0][0], sizeof(Mtx44));
        gl_matrix_multiply(&glparamstate.projection_matrix[0][0], (float *)curr, (float *)newmat);
        break;
    case 1:
        memcpy((float *)curr, &glparamstate.modelview_matrix[0][0], sizeof(Mtx44));
        gl_matrix_multiply(&glparamstate.modelview_matrix[0][0], (float *)curr, (float *)newmat);
        break;
    default:
        break;
    }

    glparamstate.dirty.bits.dirty_matrices = 1;
}
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    HANDLE_CALL_LIST(ROTATE, angle, x, y, z);

    angle *= (M_PI / 180.0f);
    float c = cosf(angle);
    float s = sinf(angle);
    float t = 1.0f - c;
    Mtx44 newmat;
    Mtx44 curr;

    float imod = 1.0f / sqrtf(x * x + y * y + z * z);
    x *= imod;
    y *= imod;
    z *= imod;

    newmat[0][0] = t * x * x + c;
    newmat[0][1] = t * x * y + s * z;
    newmat[0][2] = t * x * z - s * y;
    newmat[0][3] = 0;
    newmat[1][0] = t * x * y - s * z;
    newmat[1][1] = t * y * y + c;
    newmat[1][2] = t * y * z + s * x;
    newmat[1][3] = 0;
    newmat[2][0] = t * x * z + s * y;
    newmat[2][1] = t * y * z - s * x;
    newmat[2][2] = t * z * z + c;
    newmat[2][3] = 0;
    newmat[3][0] = 0;
    newmat[3][1] = 0;
    newmat[3][2] = 0;
    newmat[3][3] = 1;

    switch (glparamstate.matrixmode) {
    case 0:
        memcpy((float *)curr, &glparamstate.projection_matrix[0][0], sizeof(Mtx44));
        gl_matrix_multiply(&glparamstate.projection_matrix[0][0], (float *)curr, (float *)newmat);
        break;
    case 1:
        memcpy((float *)curr, &glparamstate.modelview_matrix[0][0], sizeof(Mtx44));
        gl_matrix_multiply(&glparamstate.modelview_matrix[0][0], (float *)curr, (float *)newmat);
        break;
    default:
        break;
    }

    glparamstate.dirty.bits.dirty_matrices = 1;
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
    glparamstate.clearz = clampf_01(depth);
}

// Clearing is simulated by rendering a big square with the depth value
// and the desired color
void glClear(GLbitfield mask)
{
    if (glparamstate.render_mode == GL_SELECT) {
        return;
    }

    if (mask & GL_DEPTH_BUFFER_BIT) {
        GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
        GX_SetZCompLoc(GX_DISABLE);
        GX_SetZTexture(GX_ZT_REPLACE, GX_TF_Z24X8, 0);
        GX_SetNumTexGens(1);

        /* Create a 1x1 Z-texture to set the desired depth */
        /* Our z-buffer depth is 24 bits */
        uint32_t depth = glparamstate.clearz * ((1 << 24) - 1);
        s_zbuffer_texels[0] = 0xff; // ignored
        s_zbuffer_texels[1] = (depth >> 16) & 0xff;
        s_zbuffer_texels[32] = (depth >> 8) & 0xff;
        s_zbuffer_texels[33] = depth & 0xff;
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
    glparamstate.dirty.all = ~0;
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

GLenum glGetError(void)
{
    GLenum error = glparamstate.error;
    glparamstate.error = GL_NO_ERROR;
    return error;
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
    GX_SetPointSize((unsigned int)(size * 16), GX_TO_ZERO);
}

void glLineWidth(GLfloat width)
{
    GX_SetLineWidth((unsigned int)(width * 16), GX_TO_ZERO);
}

void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    if ((red | green | blue | alpha) != 0)
        GX_SetColorUpdate(GX_TRUE);
    else
        GX_SetColorUpdate(GX_FALSE);
}

/*

  Render setup code.

*/

void glDisableClientState(GLenum cap)
{
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
        glparamstate.cs.texcoord_enabled = 0;
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
        glparamstate.cs.texcoord_enabled = 1;
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
    _ogx_array_reader_init(&glparamstate.vertex_array,
                           pointer, type, stride);
    _ogx_array_reader_set_num_elements(&glparamstate.vertex_array, size);
}

void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
    _ogx_array_reader_init(&glparamstate.normal_array,
                           pointer, type, stride);
    _ogx_array_reader_set_num_elements(&glparamstate.normal_array, 3);
}

void glColorPointer(GLint size, GLenum type,
                    GLsizei stride, const GLvoid *pointer)
{
    _ogx_array_reader_init(&glparamstate.color_array,
                           pointer, type, stride);
    _ogx_array_reader_set_num_elements(&glparamstate.color_array, size);
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    _ogx_array_reader_init(&glparamstate.texcoord_array,
                           pointer, type, stride);
    _ogx_array_reader_set_num_elements(&glparamstate.texcoord_array, size);
}

void glInterleavedArrays(GLenum format, GLsizei stride, const GLvoid *pointer)
{
    const float *vertex_array = pointer;
    const float *normal_array = pointer;
    const float *texcoord_array = pointer;
    const float *color_array = pointer;

    glparamstate.cs.index_enabled = 0;
    glparamstate.cs.normal_enabled = 0;
    glparamstate.cs.texcoord_enabled = 0;
    glparamstate.cs.vertex_enabled = 0;
    glparamstate.cs.color_enabled = 0;

    int cstride = 0;
    switch (format) {
    case GL_V2F:
        glparamstate.cs.vertex_enabled = 1;
        cstride = 2;
        break;
    case GL_V3F:
        glparamstate.cs.vertex_enabled = 1;
        cstride = 3;
        break;
    case GL_N3F_V3F:
        glparamstate.cs.vertex_enabled = 1;
        glparamstate.cs.normal_enabled = 1;
        cstride = 6;
        vertex_array += 3;
        break;
    case GL_T2F_V3F:
        glparamstate.cs.vertex_enabled = 1;
        glparamstate.cs.texcoord_enabled = 1;
        cstride = 5;
        vertex_array += 2;
        break;
    case GL_T2F_N3F_V3F:
        glparamstate.cs.vertex_enabled = 1;
        glparamstate.cs.normal_enabled = 1;
        glparamstate.cs.texcoord_enabled = 1;
        cstride = 8;

        vertex_array += 5;
        normal_array += 2;
        break;

    case GL_C4F_N3F_V3F:
        glparamstate.cs.vertex_enabled = 1;
        glparamstate.cs.normal_enabled = 1;
        glparamstate.cs.color_enabled = 1;
        cstride = 10;

        vertex_array += 7;
        normal_array += 4;
        break;
    case GL_C3F_V3F:
        glparamstate.cs.vertex_enabled = 1;
        glparamstate.cs.color_enabled = 1;
        cstride = 6;

        vertex_array += 3;
        break;
    case GL_T2F_C3F_V3F:
        glparamstate.cs.vertex_enabled = 1;
        glparamstate.cs.color_enabled = 1;
        glparamstate.cs.texcoord_enabled = 1;
        cstride = 8;

        vertex_array += 5;
        color_array += 2;
        break;
    case GL_T2F_C4F_N3F_V3F: // Complete type
        glparamstate.cs.vertex_enabled = 1;
        glparamstate.cs.normal_enabled = 1;
        glparamstate.cs.color_enabled = 1;
        glparamstate.cs.texcoord_enabled = 1;
        cstride = 12;

        vertex_array += 9;
        normal_array += 6;
        color_array += 2;
        break;

    case GL_C4UB_V2F:
    case GL_C4UB_V3F:
    case GL_T2F_C4UB_V3F:
    case GL_T4F_C4F_N3F_V4F:
    case GL_T4F_V4F:
        // TODO: Implement T4F! And UB color!
        return;
    }

    if (stride == 0) stride = cstride * sizeof(float);
    _ogx_array_reader_init(&glparamstate.vertex_array,
                           vertex_array, GL_FLOAT, stride);
    _ogx_array_reader_init(&glparamstate.normal_array,
                           normal_array, GL_FLOAT, stride);
    _ogx_array_reader_init(&glparamstate.texcoord_array,
                           texcoord_array, GL_FLOAT, stride);
    _ogx_array_reader_init(&glparamstate.color_array,
                           color_array, GL_FLOAT, stride);
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

static unsigned char draw_mode(GLenum mode)
{
    unsigned char gxmode;
    switch (mode) {
    case GL_POINTS:
        gxmode = GX_POINTS;
        break;
    case GL_LINE_LOOP:
    case GL_LINE_STRIP:
        gxmode = GX_LINESTRIP;
        break;
    case GL_LINES:
        gxmode = GX_LINES;
        break;
    case GL_TRIANGLE_STRIP:
    case GL_QUAD_STRIP:
        gxmode = GX_TRIANGLESTRIP;
        break;
    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
        gxmode = GX_TRIANGLEFAN;
        break;
    case GL_TRIANGLES:
        gxmode = GX_TRIANGLES;
        break;
    case GL_QUADS:
        gxmode = GX_QUADS;
        break;
    default:
        return 0xff; // FIXME: Emulate these modes
    }
    return gxmode;
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
        get_projection_info(&proj_type, &near, &far);

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

static void setup_texture_gen()
{
    Mtx m;

    if (!glparamstate.texture_gen_enabled) {
        GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
        return;
    }

    /* The GX API does not allow setting different inputs and generation modes
     * for the S and T coordinates; so, if one of them is enabled, we assume
     * that both share the same generation mode. */
    u32 input_type = GX_TG_TEX0;
    u32 matrix_src = GX_IDENTITY;
    switch (glparamstate.texture_gen_mode) {
    case GL_OBJECT_LINEAR:
        input_type = GX_TG_POS;
        matrix_src = GX_TEXMTX0;
        set_gx_mtx_rowv(0, m, glparamstate.texture_object_plane_s);
        set_gx_mtx_rowv(1, m, glparamstate.texture_object_plane_t);
        set_gx_mtx_row(2, m, 0.0f, 0.0f, 1.0f, 0.0f);
        GX_LoadTexMtxImm(m, GX_TEXMTX0, GX_MTX2x4);
        break;
    case GL_EYE_LINEAR:
        input_type = GX_TG_POS;
        matrix_src = GX_TEXMTX0;
        model_view_matrix_to_gx(m);
        Mtx eye_plane;
        set_gx_mtx_rowv(0, eye_plane, glparamstate.texture_eye_plane_s);
        set_gx_mtx_rowv(1, eye_plane, glparamstate.texture_eye_plane_t);
        set_gx_mtx_row(2, eye_plane, 0.0f, 0.0f, 1.0f, 0.0f);
        guMtxConcat(eye_plane, m, m);
        GX_LoadTexMtxImm(m, GX_TEXMTX0, GX_MTX2x4);
        break;
    default:
        warning("Unsupported texture coordinate generation mode %x",
                glparamstate.texture_gen_mode);
    }

    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, input_type, matrix_src);
}

static void setup_texture_stage(u8 stage, u8 raster_color, u8 raster_alpha,
                                u8 channel)
{
    switch (glparamstate.texture_env_mode) {
    case GL_REPLACE:
        // In data: a: Texture Color
        GX_SetTevColorIn(stage, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
        GX_SetTevAlphaIn(stage, GX_CA_TEXA, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
        break;
    case GL_ADD:
        // In data: d: Texture Color a: raster value, Operation: a+d
        GX_SetTevColorIn(stage, raster_color, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
        GX_SetTevAlphaIn(stage, raster_alpha, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
        break;
    case GL_BLEND:
        /* In data: c: Texture Color, a: raster value, b: tex env
         * Operation: a(1-c)+b*c
         * Until we implement GL_TEXTURE_ENV_COLOR, use white (GX_CC_ONE) for
         * the tex env color. */
        GX_SetTevColorIn(stage, raster_color, GX_CC_ONE, GX_CC_TEXC, GX_CC_ZERO);
        GX_SetTevAlphaIn(stage, GX_CA_ZERO, raster_alpha, GX_CA_TEXA, GX_CA_ZERO);
        break;
    case GL_MODULATE:
    default:
        // In data: c: Texture Color b: raster value, Operation: b*c
        GX_SetTevColorIn(stage, GX_CC_ZERO, raster_color, GX_CC_TEXC, GX_CC_ZERO);
        GX_SetTevAlphaIn(stage, GX_CA_ZERO, raster_alpha, GX_CA_TEXA, GX_CA_ZERO);
        break;
    }
    GX_SetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevOrder(stage, GX_TEXCOORD0, GX_TEXMAP0, channel);
    GX_SetNumTexGens(1);
    if (glparamstate.dirty.bits.dirty_texture_gen) {
        setup_texture_gen();
    }
}

static void setup_render_stages(int texen)
{
    if (glparamstate.lighting.enabled) {
        LightMasks light_mask = prepare_lighting();

        GXColor color_zero = { 0, 0, 0, 0 };
        GXColor color_gamb = gxcol_new_fv(glparamstate.lighting.globalambient);

        GX_SetNumChans(2);
        GX_SetNumTevStages(2);
        GX_SetNumTexGens(0);

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

        // Color1 channel: Multiplies the light raster result with the vertex color. Ambient is set to register (which is zero)
        GX_SetChanCtrl(GX_COLOR1A1, GX_TRUE, GX_SRC_REG, vert_color_src, light_mask.diffuse_mask, GX_DF_CLAMP, GX_AF_SPOT);
        GX_SetChanAmbColor(GX_COLOR1A1, color_zero);

        // STAGE 0: ambient*vert_color -> cprev
        // In data: d: Raster Color, a: emission color
        GX_SetTevColor(GX_TEVREG0, ecol);
        GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_C0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
        GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
        // Operation: Pass d
        GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        // Select COLOR0A0 for the rasterizer, disable all textures
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_DISABLE, GX_COLOR0A0);

        // STAGE 1: diffuse*vert_color + cprev -> cprev
        // In data: d: Raster Color a: CPREV
        GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_CPREV, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
        GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_APREV, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
        // Operation: Sum a + d
        GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        // Select COLOR1A1 for the rasterizer, disable all textures
        GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORDNULL, GX_TEXMAP_DISABLE, GX_COLOR1A1);

        if (texen) {
            // Do not select any raster value, Texture 0 for texture rasterizer and TEXCOORD0 slot for tex coordinates
            setup_texture_stage(GX_TEVSTAGE2, GX_CC_CPREV, GX_CA_APREV, GX_COLORNULL);
            GX_SetNumTevStages(3);
        }
    } else {
        // Unlit scene
        // TEV STAGE 0: Modulate the vertex color with the texture 0. Outputs to GX_TEVPREV
        // Optimization: If color_enabled is false (constant vertex color) use the constant color register
        // instead of using the rasterizer and emitting a color for each vertex

        // By default use rasterized data and put it a COLOR0A0
        unsigned char vertex_color_register = GX_CC_RASC;
        unsigned char vertex_alpha_register = GX_CA_RASA;
        unsigned char rasterized_color = GX_COLOR0A0;
        if (!glparamstate.cs.color_enabled) { // No need for vertex color raster, it's constant
            // Use constant color
            vertex_color_register = GX_CC_KONST;
            vertex_alpha_register = GX_CA_KONST;
            // Select register 0 for color/alpha
            GX_SetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K0);
            GX_SetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_K0_A);
            // Load the color (current GL color)
            GXColor ccol = gxcol_new_fv(glparamstate.imm_mode.current_color);
            GX_SetTevKColor(GX_KCOLOR0, ccol);

            rasterized_color = GX_COLORNULL; // Disable vertex color rasterizer
        }

        GX_SetNumChans(1);
        GX_SetNumTevStages(1);

        // Disable lighting and output vertex color to the rasterized color
        GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, 0, 0, 0);
        GX_SetChanCtrl(GX_COLOR1A1, GX_DISABLE, GX_SRC_REG, GX_SRC_REG, 0, 0, 0);

        if (texen) {
            // Select COLOR0A0 for the rasterizer, Texture 0 for texture rasterizer and TEXCOORD0 slot for tex coordinates
            setup_texture_stage(GX_TEVSTAGE0,
                                vertex_color_register, vertex_alpha_register,
                                rasterized_color);
        } else {
            // In data: d: Raster Color
            GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, vertex_color_register);
            GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, vertex_alpha_register);
            // Operation: Pass the color
            GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
            GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
            // Select COLOR0A0 for the rasterizer, Texture 0 for texture rasterizer and TEXCOORD0 slot for tex coordinates
            GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_DISABLE, rasterized_color);
            GX_SetNumTexGens(0);
        }
    }

    setup_fog();
}

void _ogx_apply_state()
{
    setup_render_stages(glparamstate.texture_enabled);

    // Set up the OGL state to GX state
    if (glparamstate.dirty.bits.dirty_z)
        GX_SetZMode(glparamstate.ztest, glparamstate.zfunc, glparamstate.zwrite & glparamstate.ztest);

    if (glparamstate.dirty.bits.dirty_blend) {
        if (glparamstate.blendenabled)
            GX_SetBlendMode(GX_BM_BLEND, glparamstate.srcblend, glparamstate.dstblend, GX_LO_CLEAR);
        else
            GX_SetBlendMode(GX_BM_NONE, glparamstate.srcblend, glparamstate.dstblend, GX_LO_CLEAR);
    }

    if (glparamstate.dirty.bits.dirty_alphatest) {
        if (glparamstate.alphatest_enabled) {
            GX_SetZCompLoc(GX_DISABLE);
            GX_SetAlphaCompare(glparamstate.alpha_func, glparamstate.alpha_ref,
                               GX_AOP_AND, GX_ALWAYS, 0);
        } else {
            GX_SetZCompLoc(GX_ENABLE);
            GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
        }
    }

    if (glparamstate.dirty.bits.dirty_cull) {
        setup_cull_mode();
    }

    // Matrix stuff
    if (glparamstate.dirty.bits.dirty_matrices) {
        MODELVIEW_UPDATE
        PROJECTION_UPDATE
    }
    if (glparamstate.dirty.bits.dirty_matrices | glparamstate.dirty.bits.dirty_lighting) {
        NORMAL_UPDATE
    }

    // All the state has been transferred, no need to update it again next time
    glparamstate.dirty.all = 0;
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    unsigned char gxmode = draw_mode(mode);
    if (gxmode == 0xff)
        return;

    int texen = glparamstate.cs.texcoord_enabled;
    if (glparamstate.current_call_list.index >= 0 &&
        glparamstate.current_call_list.execution_depth == 0) {
        _ogx_call_list_append(COMMAND_GXLIST);
    } else {
        _ogx_apply_state();
        /* When not building a display list, we can optimize the drawing by
         * avoiding passing texture coordinates if texturing is not enabled.
         */
        texen = texen && glparamstate.texture_enabled;
    }

    int color_provide = 0;
    if (glparamstate.cs.color_enabled &&
        (!glparamstate.lighting.enabled || glparamstate.lighting.color_material_enabled)) { // Vertex colouring
        if (glparamstate.lighting.enabled)
            color_provide = 2; // Lighting requires two color channels
        else
            color_provide = 1;
    }

    // Not using indices
    GX_ClearVtxDesc();
    if (glparamstate.cs.vertex_enabled)
        GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    if (glparamstate.cs.normal_enabled)
        GX_SetVtxDesc(GX_VA_NRM, GX_DIRECT);
    if (color_provide)
        GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    if (color_provide == 2)
        GX_SetVtxDesc(GX_VA_CLR1, GX_DIRECT);
    if (texen)
        GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);

    // Using floats
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR1, GX_CLR_RGBA, GX_RGBA8, 0);

    // Invalidate vertex data as may have been modified by the user
    GX_InvVtxCache();

    bool loop = (mode == GL_LINE_LOOP);
    GX_Begin(gxmode, GX_VTXFMT0, count + loop);
    draw_arrays_general(first, count, glparamstate.cs.normal_enabled,
                        color_provide, texen, loop);
    GX_End();
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{

    unsigned char gxmode = draw_mode(mode);
    if (gxmode == 0xff)
        return;

    int texen = glparamstate.cs.texcoord_enabled;
    if (glparamstate.current_call_list.index >= 0 &&
        glparamstate.current_call_list.execution_depth == 0) {
        _ogx_call_list_append(COMMAND_GXLIST);
    } else {
        _ogx_apply_state();
        /* When not building a display list, we can optimize the drawing by
         * avoiding passing texture coordinates if texturing is not enabled.
         */
        texen = texen && glparamstate.texture_enabled;
    }

    int color_provide = 0;
    if (glparamstate.cs.color_enabled &&
        (!glparamstate.lighting.enabled || glparamstate.lighting.color_material_enabled)) { // Vertex colouring
        if (glparamstate.lighting.enabled)
            color_provide = 2; // Lighting requires two color channels
        else
            color_provide = 1;
    }

    // Not using indices
    GX_ClearVtxDesc();
    if (glparamstate.cs.vertex_enabled)
        GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    if (glparamstate.cs.normal_enabled)
        GX_SetVtxDesc(GX_VA_NRM, GX_DIRECT);
    if (color_provide)
        GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    if (color_provide == 2)
        GX_SetVtxDesc(GX_VA_CLR1, GX_DIRECT);
    if (texen)
        GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);

    // Using floats
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR1, GX_CLR_RGBA, GX_RGBA8, 0);

    // Invalidate vertex data as may have been modified by the user
    GX_InvVtxCache();

    bool loop = (mode == GL_LINE_LOOP);
    GX_Begin(gxmode, GX_VTXFMT0, count + loop);
    int i;
    for (i = 0; i < count + loop; i++) {
        int index = read_index(indices, type, i % count);
        float value[4];
        _ogx_array_reader_read_float(&glparamstate.vertex_array, index, value);

        GX_Position3f32(value[0], value[1], value[2]);

        if (glparamstate.cs.normal_enabled) {
            _ogx_array_reader_read_float(&glparamstate.normal_array, index, value);
            GX_Normal3f32(value[0], value[1], value[2]);
        }

        if (color_provide) {
            _ogx_array_reader_read_float(&glparamstate.color_array, index, value);
            unsigned char arr[4] = { value[0] * 255.0f, value[1] * 255.0f, value[2] * 255.0f, value[3] * 255.0f };
            GX_Color4u8(arr[0], arr[1], arr[2], arr[3]);
            if (color_provide == 2)
                GX_Color4u8(arr[0], arr[1], arr[2], arr[3]);
        }

        if (texen) {
            _ogx_array_reader_read_float(&glparamstate.texcoord_array, index, value);
            GX_TexCoord2f32(value[0], value[1]);
        }
    }
    GX_End();
}

static void draw_arrays_general(int first, int count, int ne,
                                int color_provide, int texen, bool loop)
{

    int i;
    for (i = 0; i < count + loop; i++) {
        int j = i % count + first;
        float value[4];
        _ogx_array_reader_read_float(&glparamstate.vertex_array, j, value);
        GX_Position3f32(value[0], value[1], value[2]);

        if (ne) {
            _ogx_array_reader_read_float(&glparamstate.normal_array, j, value);
            GX_Normal3f32(value[0], value[1], value[2]);
        }

        // If the data stream doesn't contain any color data just
        // send the current color (the last glColor* call)
        if (color_provide) {
            _ogx_array_reader_read_float(&glparamstate.color_array, j, value);
            unsigned char arr[4] = { value[0] * 255.0f, value[1] * 255.0f, value[2] * 255.0f, value[3] * 255.0f };
            GX_Color4u8(arr[0], arr[1], arr[2], arr[3]);
            if (color_provide == 2)
                GX_Color4u8(arr[0], arr[1], arr[2], arr[3]);
        }

        if (texen) {
            _ogx_array_reader_read_float(&glparamstate.texcoord_array, j, value);
            GX_TexCoord2f32(value[0], value[1]);
        }
    }
}

void glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
               GLdouble near, GLdouble far)
{
    Mtx44 mt;
    f32 tmp;

    tmp = 1.0f / (right - left);
    mt[0][0] = (2 * near) * tmp;
    mt[0][1] = 0.0f;
    mt[0][2] = (right + left) * tmp;
    mt[0][3] = 0.0f;
    tmp = 1.0f / (top - bottom);
    mt[1][0] = 0.0f;
    mt[1][1] = (2 * near) * tmp;
    mt[1][2] = (top + bottom) * tmp;
    mt[1][3] = 0.0f;
    tmp = 1.0f / (far - near);
    mt[2][0] = 0.0f;
    mt[2][1] = 0.0f;
    mt[2][2] = -(far + near) * tmp;
    mt[2][3] = -2.0 * (far * near) * tmp;
    mt[3][0] = 0.0f;
    mt[3][1] = 0.0f;
    mt[3][2] = -1.0f;
    mt[3][3] = 0.0f;

    glMultMatrixf((float *)mt);
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
void glClearStencil(GLint s) {}
void glStencilMask(GLuint mask) {} // Should use Alpha testing to achieve similar results
void glShadeModel(GLenum mode) {}  // In theory we don't have GX equivalent?
void glHint(GLenum target, GLenum mode) {}

// XXX: Need to finish glGets, important!!!
void glGetIntegerv(GLenum pname, GLint *params)
{
    switch (pname) {
    case GL_MAX_TEXTURE_SIZE:
        *params = 1024;
        return;
    case GL_MODELVIEW_STACK_DEPTH:
        *params = MAX_MODV_STACK;
        return;
    case GL_PROJECTION_STACK_DEPTH:
        *params = MAX_PROJ_STACK;
        return;
    case GL_MAX_NAME_STACK_DEPTH:
        *params = MAX_NAME_STACK_DEPTH;
        return;
    case GL_NAME_STACK_DEPTH:
        *params = glparamstate.name_stack_depth;
        return;
    case GL_PACK_SWAP_BYTES:
        *params = glparamstate.pack_swap_bytes;
        break;
    case GL_PACK_LSB_FIRST:
        *params = glparamstate.pack_lsb_first;
        break;
    case GL_PACK_ROW_LENGTH:
        *params = glparamstate.pack_row_length;
        break;
    case GL_PACK_IMAGE_HEIGHT:
        *params = glparamstate.pack_image_height;
        break;
    case GL_PACK_SKIP_ROWS:
        *params = glparamstate.pack_skip_rows;
        break;
    case GL_PACK_SKIP_PIXELS:
        *params = glparamstate.pack_skip_pixels;
        break;
    case GL_PACK_SKIP_IMAGES:
        *params = glparamstate.pack_skip_images;
        break;
    case GL_PACK_ALIGNMENT:
        *params = glparamstate.pack_alignment;
        break;
    case GL_UNPACK_SWAP_BYTES:
        *params = glparamstate.unpack_swap_bytes;
        break;
    case GL_UNPACK_LSB_FIRST:
        *params = glparamstate.unpack_lsb_first;
        break;
    case GL_UNPACK_ROW_LENGTH:
        *params = glparamstate.unpack_row_length;
        break;
    case GL_UNPACK_IMAGE_HEIGHT:
        *params = glparamstate.unpack_image_height;
        break;
    case GL_UNPACK_SKIP_ROWS:
        *params = glparamstate.unpack_skip_rows;
        break;
    case GL_UNPACK_SKIP_PIXELS:
        *params = glparamstate.unpack_skip_pixels;
        break;
    case GL_UNPACK_SKIP_IMAGES:
        *params = glparamstate.unpack_skip_images;
        break;
    case GL_UNPACK_ALIGNMENT:
        *params = glparamstate.unpack_alignment;
        break;
    case GL_VIEWPORT:
        memcpy(params, glparamstate.viewport, 4 * sizeof(int));
        return;
    case GL_RENDER_MODE:
        *params = glparamstate.render_mode;
        return;
    default:
        return;
    };
}

void glGetDoublev(GLenum pname, GLdouble *params)
{
    float paramsf[16];
    int n = 1;

    glGetFloatv(pname, paramsf);
    switch (pname) {
    case GL_MODELVIEW_MATRIX:
    case GL_PROJECTION_MATRIX:
        n = 16; break;
    };
    for (int i = 0; i < n; i++) {
        params[i] = paramsf[i];
    }
}

void glGetFloatv(GLenum pname, GLfloat *params)
{
    switch (pname) {
    case GL_MODELVIEW_MATRIX:
        memcpy(params, glparamstate.modelview_matrix, sizeof(float) * 16);
        return;
    case GL_PROJECTION_MATRIX:
        memcpy(params, glparamstate.projection_matrix, sizeof(float) * 16);
        return;
    default:
        return;
    };
}

// TODO STUB IMPLEMENTATION

void glClipPlane(GLenum plane, const GLdouble *equation) {}
const GLubyte *glGetString(GLenum name) { return gl_null_string; }
void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params) {}
void glLightModelf(GLenum pname, GLfloat param) {}
void glLightModeli(GLenum pname, GLint param) {}
void glPushAttrib(GLbitfield mask) {}
void glPopAttrib(void) {}
void glPushClientAttrib(GLbitfield mask) {}
void glPopClientAttrib(void) {}
void glPolygonMode(GLenum face, GLenum mode) {}
void glReadBuffer(GLenum mode) {}
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *data) {}

/*
 ****** NOTES ******

 Front face definition is reversed. CCW is front for OpenGL
 while front facing is defined CW in GX.

 This implementation ONLY supports floats for vertexs, texcoords
 and normals. Support for different types is not implemented as
 GX does only support floats. Simple conversion would be needed.

*/
