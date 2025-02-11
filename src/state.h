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

#ifndef OGX_STATE_H
#define OGX_STATE_H

#include "arrays.h"
#include "types.h"

#include <GL/gl.h>
#include <gccore.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constant definition. Here are the limits of this implementation.
// Can be changed with care.

#define _MAX_GL_TEX    2048 // Maximum number of textures
#define MAX_PROJ_STACK 4   // Proj. matrix stack depth
#define MAX_MODV_STACK 16  // Modelview matrix stack depth
#define MAX_TEXTURE_MAT_STACK 2 // Matrix stack, 2 is the required minimum
#define NUM_VERTS_IM   64  // Maximum number of vertices that can be inside a glBegin/End
#define MAX_LIGHTS     4   // Max num lights
#define MAX_GX_LIGHTS  8
#define MAX_NAME_STACK_DEPTH 256 /* 64 is the minimum required */
/* A TEV stage can process up to 2 clip planes, so we could increase this if
 * needed */
#define MAX_CLIP_PLANES 6
#define MAX_PIXEL_MAP_TABLE 32 /* 32 is the minimum required */
/* GX supports up to 8 texture units (that is, TEV stages with textures), but
 * applications typically use much less. Also, one textured stage is used by
 * opengx when stencil is enabled. */
#define MAX_TEXTURE_UNITS 4
#define MAX_COLOR_ARRAYS 2 /* GX limit */
/* The GX limit is 8, but we can have proxy arrays which generate texture
 * coordinates from GX_VA_POS, GX_VA_NORM, etc.
 * The choise of 10 is arbitrary here, we could set it up to 16
 * (GX_TEVSTAGE15 - GX_TEVSTAGE0). */
#define MAX_TEXCOORD_ARRAYS 10

#define STATE_ARRAY(attribute) \
    (glparamstate.arrays[OGX_ATTR_INDEX_##attribute])
#define STATE_ARRAY_TEX(unit) \
    (glparamstate.arrays[OGX_ATTR_INDEX_TEX0 + unit])

typedef enum {
    OGX_HINT_NONE = 0,
    /* Enables fast (but wrong) GPU-accelerated GL_SPHERE_MAP */
    OGX_HINT_FAST_SPHERE_MAP = 1 << 0,
} OgxHints;

typedef enum {
    OGX_ATTR_INDEX_POS = 0,
    OGX_ATTR_INDEX_NRM,
    OGX_ATTR_INDEX_CLR,
    OGX_ATTR_INDEX_TEX0,
    OGX_ATTR_INDEX_TEX_LAST = OGX_ATTR_INDEX_TEX0 + MAX_TEXTURE_UNITS - 1,
    OGX_ATTR_INDEX_COUNT
} OgxAttrIndex;

typedef struct {
    Pos3f pos;
    Norm3f norm;
    Tex2f tex[MAX_TEXTURE_UNITS];
    GXColor color;
} VertexData;

typedef float ClipPlane[4];

typedef struct gltexture_
{
    GXTexObj texobj;
} gltexture_;

typedef enum {
    OGX_TEXGEN_S = 1 << 0,
    OGX_TEXGEN_T = 1 << 1,
    OGX_TEXGEN_R = 1 << 2,
    OGX_TEXGEN_Q = 1 << 3,
} OgxTexgenMask;

typedef uint8_t OgxPixelMap[MAX_PIXEL_MAP_TABLE];
typedef struct {
    /* 10 is the number of pixel maps defined by OpenGL, from
     * GL_PIXEL_MAP_I_TO_I to GL_PIXEL_MAP_A_TO_A (see the documentation of
     * glPixelMap for an explanation) */
    uint8_t sizes[10];
    OgxPixelMap maps[10];
} OgxPixelMapTables;

typedef struct {
    Mtx matrix[MAX_TEXTURE_MAT_STACK];
    int glcurtex;
    float texture_eye_plane_s[4];
    float texture_eye_plane_t[4];
    float texture_object_plane_s[4];
    float texture_object_plane_t[4];
    OgxArrayReader *array_reader;
    /* There should be 4 of these (for S, T, R, Q) but GX uses a single
     * transformation for all of them */
    uint16_t gen_mode;
    OgxTexgenMask gen_enabled;
    char matrix_index;
    GLenum mode;
    GLenum combine_rgb;
    GLenum source_rgb[3];
    GLenum operand_rgb[3];
    GLenum combine_alpha;
    GLenum source_alpha[3];
    GLenum operand_alpha[3];
    GXColor color; // TODO: still unused
} OgxTextureUnit;

typedef struct glparams_
{
    Mtx modelview_matrix;
    Mtx44 projection_matrix;
    Mtx modelview_stack[MAX_MODV_STACK];
    Mtx44 projection_stack[MAX_PROJ_STACK];
    ClipPlane clip_planes[MAX_CLIP_PLANES];
    float raster_pos[4];
    float pixel_zoom_x;
    float pixel_zoom_y;
    float depth_near;
    float depth_far;
    int cur_modv_mat, cur_proj_mat;

    int viewport[4];
    int scissor[4];

    OgxHints hints;

    unsigned char srcblend, dstblend;
    unsigned char blendenabled;
    unsigned char zwrite, ztest, zfunc;
    unsigned char matrixmode;
    unsigned char frontcw, cullenabled;
    bool color_update;
    bool polygon_offset_fill;
    bool raster_pos_valid;
    bool scissor_enabled;
    unsigned point_sprites_enabled : 1;
    unsigned point_sprites_coord_replace : 1;
    char active_texture;
    uint8_t alpha_func, alpha_ref, alphatest_enabled;
    uint8_t clip_plane_mask;
    GLenum glcullmode;
    GLenum render_mode;
    GLenum active_buffer; /* no separate buffers for reading and writing */
    GLenum polygon_mode;
    int draw_count;
    GXColor clear_color;
    GXColor accum_clear_color;
    float clearz;
    float polygon_offset_factor;
    float polygon_offset_units;
    float transfer_depth_scale;
    float transfer_depth_bias;
    int16_t transfer_index_shift;
    int16_t transfer_index_offset;

    OgxTextureUnit texture_unit[MAX_TEXTURE_UNITS];

    OgxPixelMapTables *pixel_maps; /* Only allocated if glPixelMap is called */

    GLuint *name_stack;
    GLuint *select_buffer;
    uint16_t name_stack_depth;
    uint16_t select_buffer_size;
    int16_t select_buffer_offset; /* negative if overflow occurred */
    uint16_t hit_count;

    void *index_array;
    OgxVertexAttribArray arrays[OGX_ATTR_INDEX_COUNT];
    union client_state
    {
        struct {
            unsigned vertex_enabled : 1;
            unsigned normal_enabled : 1;
            unsigned index_enabled : 1;
            unsigned color_enabled : 1;
            unsigned texcoord_enabled : MAX_TEXTURE_UNITS;
            char active_texture;
        };
        uint32_t as_int;
    } cs;

    unsigned texture_enabled : MAX_TEXTURE_UNITS;
    unsigned pack_swap_bytes: 1;
    unsigned pack_lsb_first: 1;
    unsigned unpack_swap_bytes: 1;
    unsigned unpack_lsb_first: 1;
    uint8_t pack_skip_pixels;
    uint8_t pack_skip_rows;
    uint8_t pack_skip_images;
    uint8_t pack_alignment;
    uint8_t unpack_skip_pixels;
    uint8_t unpack_skip_rows;
    uint8_t unpack_skip_images;
    uint8_t unpack_alignment;
    uint16_t pack_row_length;
    uint16_t pack_image_height;
    uint16_t unpack_row_length;
    uint16_t unpack_image_height;

    VboType bound_vbo_array;
    VboType bound_vbo_element_array;

    struct imm_mode
    {
        float current_color[4];
        Tex2f current_texcoord[MAX_TEXTURE_UNITS];
        Norm3f current_normal;
        int current_numverts;
        int current_vertices_size;
        VertexData *current_vertices;
        GLenum prim_type;
        unsigned in_gl_begin : 1;
        unsigned has_color : 1;
        unsigned has_normal : 1;
        unsigned has_texcoord : MAX_TEXTURE_UNITS;
    } imm_mode;

    union dirty_union
    {
        struct dirty_struct
        {
            unsigned dirty_alphatest : 1;
            unsigned dirty_blend : 1;
            unsigned dirty_z : 1;
            unsigned dirty_clearz : 1;
            unsigned dirty_color_update : 1;
            unsigned dirty_matrices : 1;
            unsigned dirty_tev : 1;
            unsigned dirty_cull : 1;
            unsigned dirty_fog : 1;
            unsigned dirty_scissor : 1;
            unsigned dirty_attributes : 1;
        } bits;
        unsigned int all;
    } dirty;

    struct _lighting
    {
        struct alight
        {
            float position[4];
            float direction[3];
            float spot_direction[3];
            float ambient_color[4];
            float diffuse_color[4];
            float specular_color[4];
            float atten[3];
            float spot_cutoff;
            int spot_exponent;
            char enabled;
            int8_t gx_ambient;
            int8_t gx_diffuse;
            int8_t gx_specular;
        } lights[MAX_LIGHTS];
        GXLightObj lightobj[MAX_LIGHTS * 2];
        float globalambient[4];
        float matambient[4];
        float matdiffuse[4];
        float matemission[4];
        float matspecular[4];
        float matshininess;
        char enabled;

        char color_material_enabled;
        uint16_t color_material_mode;

        GXColor cached_ambient;
    } lighting;

    struct _fog
    {
        u8 enabled;
        uint16_t mode;
        float color[4];
        float density;
        float start;
        float end;
    } fog;

    struct _stencil {
        bool enabled;
        uint8_t func;
        uint8_t ref;
        uint8_t mask;
        uint8_t wmask;
        uint8_t clear;
        uint16_t op_fail;
        uint16_t op_zfail;
        uint16_t op_zpass;
    } stencil;

    gltexture_ textures[_MAX_GL_TEX];

    struct CurrentCallList
    {
        int16_t index; /* -1 if not currently inside a glNewList */
        char must_execute;
        /* > 0 if we are executing a glCallList while compiling a display list.
         * This is needed so that we don't write the executed list's command
         * into the list being built. */
        uint8_t execution_depth;
    } current_call_list;

    GLuint current_program;

    bool compat_profile;
    GLenum error;
} glparams_;

extern glparams_ _ogx_state;

/* To avoid renaming all the variables */
#define glparamstate _ogx_state
#define texture_list _ogx_state.textures

void _ogx_apply_state(void);

#ifdef __cplusplus
} // extern C
#endif

#endif /* OGX_STATE_H */
