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

#include "stencil.h"

#include "debug.h"
#include "efb.h"
#include "state.h"
#include "utils.h"

#include <GL/gl.h>
#include <malloc.h>

static bool s_wants_stencil = false;
static bool s_stencil_texture_needs_update = false;
static uint8_t s_stencil_format = GX_CTF_R4;
OgxStencilFlags _ogx_stencil_flags = OGX_STENCIL_NONE;
/* This is the authoritative stencil buffer contents: note that the order of
 * the pixel data follows the GX texture scrambling logic. */
static OgxEfbBuffer *s_stencil_buffer = NULL;
/* This is a simplified version of the stencil buffer only used for drawing:
 * its pixels are set to 0 for blocked areas and > 0 for paintable areas. */
static GXTexObj s_stencil_texture;
static struct _dirty_area {
    uint16_t top;
    uint16_t bottom;
    uint16_t left;
    uint16_t right;
} s_dirty_area = { 0, 0, 0, 0 };
static int s_stencil_count_updated = 0;

static inline bool stencil_8bit()
{
    return _ogx_stencil_flags & OGX_STENCIL_8BIT;
}

static void check_bounding_box()
{
    /* Read the new bounding box and update the old one, if needed */
    struct _dirty_area a;
    GX_ReadBoundingBox(&a.top, &a.bottom, &a.left, &a.right);
    if (a.bottom <= a.top || a.right <= a.left) return;

    if (s_dirty_area.top >= s_dirty_area.bottom) {
        /* previous area is invalid, use the new one */
        s_dirty_area = a;
    } else {
        /* enlarge the area */
        if (a.top < s_dirty_area.top) s_dirty_area.top = a.top;
        if (a.left < s_dirty_area.left) s_dirty_area.left = a.left;
        if (a.bottom > s_dirty_area.bottom) s_dirty_area.bottom = a.bottom;
        if (a.right > s_dirty_area.right) s_dirty_area.right = a.right;
    }
    debug(OGX_LOG_STENCIL, "Bounding box (%d,%d) - (%d,%d)",
          s_dirty_area.left, s_dirty_area.top, s_dirty_area.right, s_dirty_area.bottom);
}

static inline u8 invert_comp(u8 comp)
{
    switch (comp) {
    case GX_NEVER: return GX_ALWAYS;
    case GX_LESS: return GX_GEQUAL;
    case GX_EQUAL: return GX_NEQUAL;
    case GX_LEQUAL: return GX_GREATER;
    case GX_GREATER: return GX_LEQUAL;
    case GX_GEQUAL: return GX_LESS;
    case GX_ALWAYS: return GX_NEVER;
    default: return 0xff;
    }
}

/* The type of comparison we'll setup in the TEV, and, accordingly, the stencil
 * texture texel to prepare.
 */
typedef enum {
    TEV_COMP_ALWAYS,        /* stencil TEV stage not needed, always drawing */
    TEV_COMP_NEVER,         /* stencil TEV stage not needed, not drawing */
    TEV_COMP_DIRECT,        /* TEV stage will use a HW comparison, the stencil
                               texture can be prepared with the masked stencil
                               buffer data */
    TEV_COMP_IND_NEQUAL,    /* the TEV does not support the GL_NOTEQUAL
                               comparison, so we'll need to build a stencil
                               texture having 1 where the stencil test passes,
                               and 0 otherwise */
} TevComparisonType;

static inline TevComparisonType comparison_type(uint8_t comparison, uint8_t masked_ref)
{
    switch (comparison) {
    case GX_ALWAYS:
        return TEV_COMP_ALWAYS;
    case GX_NEVER:
        return TEV_COMP_NEVER;
    case GX_NEQUAL:
        return TEV_COMP_IND_NEQUAL;
    default: /* All other types */
        /* Handle a few special cases that lead to optimizations */
        if (comparison == GX_GEQUAL && masked_ref == 0)
            return TEV_COMP_ALWAYS;
        if (comparison == GX_LEQUAL) {
            uint8_t max_value = stencil_8bit() ? 0xff : 0xf;
            if (masked_ref == max_value)
                return TEV_COMP_ALWAYS;
        }
        return TEV_COMP_DIRECT;
    }
}

static inline bool tev_stage_needed(TevComparisonType comp_type)
{
    return comp_type != TEV_COMP_ALWAYS && comp_type != TEV_COMP_NEVER;
}

/* Prepare the texture used for stencil test: we cannot use the stencil buffer
 * directly, because the TEV does lack a function for bitwise AND of the pixels
 * (which is needed to implement the OpenGL stencil "mask" operation) and does
 * not support the full set of pixel comparisons used in OpenGL (it supports
 * only EQ (=) and GT (>) comparisons).
 * For this reason, we prepare a texture whose pixel data is a transformed
 * version of the stencil buffer which can be used with the TEV operations.
 * Depending on the value of the OpenGL stencil comparison function, we might
 * need to rebuild this texture differently.
 */
static void update_stencil_texture()
{
    if (!s_stencil_texture_needs_update ||
        glparamstate.draw_count == s_stencil_count_updated) {
        return;
    }
    s_stencil_texture_needs_update = false;
    s_stencil_count_updated = glparamstate.draw_count;

    u16 top, bottom, left, right;
    top = s_dirty_area.top;
    bottom = s_dirty_area.bottom;
    left = s_dirty_area.left;
    right = s_dirty_area.right;
    if (bottom <= top || right <= left) return;

    u16 width = GX_GetTexObjWidth(&s_stencil_texture);
    u16 height = GX_GetTexObjHeight(&s_stencil_texture);
    u32 size = GX_GetTexBufferSize(width, height, s_stencil_format, 0, GX_FALSE);

    /* The bounding box can have a 1 pixel error (returning a slightly bigger
     * area) and, in addition to that, we round up to the texture blocks, to
     * simplify the loops */
    int block_width = 8;
    int block_height = stencil_8bit() ? 4 : 8;
    int block_pitch = width / block_width;

    int block_start_y = top / block_height;
    int block_end_y = (bottom + block_height - 1) / block_height;
    int block_start_x = left / block_width;
    int block_end_x = (right + block_width - 1) / block_width;
    int width_blocks = block_end_x - block_start_x;

    void *stencil_data = _ogx_efb_buffer_get_texels(s_stencil_buffer);
    void *stencil_texels =
        MEM_PHYSICAL_TO_K0(GX_GetTexObjData(&s_stencil_texture));
    uint8_t masked_ref = glparamstate.stencil.ref & glparamstate.stencil.mask;
    TevComparisonType comp_type = comparison_type(glparamstate.stencil.func,
                                                  masked_ref);
    if (comp_type == TEV_COMP_DIRECT) {
        debug(OGX_LOG_STENCIL,
              "Updating stencil texture for direct comparison");
        /* Fast conversion: we build a texture whose pixels are the stencil
         * buffer values ANDed with the stencil mask. Such a texture can be
         * used with most comparison functions. */
        uint32_t mask = glparamstate.stencil.mask;
        if (!stencil_8bit()) {
            mask |= mask << 4; /* replicate the nibble it to fill a byte */
        }
        /* replicate the byte to fill a 32-bit word */
        mask |= mask << 8;
        mask |= mask << 16;
        for (int y = block_start_y; y < block_end_y; y++) {
            int offset = (y * block_pitch + block_start_x) * 32;
            uint32_t *src = stencil_data + offset;
            uint32_t *dst = stencil_texels + offset;
            /* A block is 32 bytes, which we fill with 32-bit integers */
            for (int i = 0; i < width_blocks * 32 / sizeof(uint32_t); i++) {
                *dst++ = *src++ & mask;
            }
            //DCStoreRange(stencil_texels + offset, width_blocks * 32);
        }
    } else if (comp_type == TEV_COMP_IND_NEQUAL) {
        debug(OGX_LOG_STENCIL,
              "Updating stencil texture for NEQUAL comparison");
        /* These's just no way to implement the GL_NOTEQUAL comparison on the
         * TEV, so we prepare a stencil texture that already contains the
         * result of the compasiron. */
        uint8_t mask = glparamstate.stencil.mask;
        for (int y = block_start_y; y < block_end_y; y++) {
            int offset = (y * block_pitch + block_start_x) * 32;
            uint8_t *src = stencil_data + offset;
            uint8_t *dst = stencil_texels + offset;
            /* A block is 32 bytes */
            for (int i = 0; i < width_blocks * 32; i++) {
                if (stencil_8bit()) {
                    uint8_t gequal_than_ref = (*src++ & mask) != masked_ref;
                    *dst++ = gequal_than_ref;
                } else {
                    /* Two pixels per byte */
                    uint8_t gequal_than_ref0 = (*src++ & mask) != masked_ref;
                    uint8_t gequal_than_ref1 = ((*src++ >> 4) & mask) != masked_ref;
                    *dst++ = gequal_than_ref0 | (gequal_than_ref1 << 4);
                }
            }
        }
    }
    DCStoreRange(stencil_texels, size); // FIXME
    GX_InvalidateTexAll();

    /* The area is not dirty anymore */
    memset(&s_dirty_area, 0, sizeof(s_dirty_area));
}

void _ogx_stencil_load_into_efb()
{

    GX_InvalidateTexAll();
    _ogx_efb_restore_texobj(&s_stencil_buffer->texobj);

    /* We clear the bounding box because at the end of the drawing
     * operations on the stencil buffer we will need to update the stencil
     * texture which we use for the actual drawing, and the bounding box
     * allows us to do it more efficiently. */
    GX_DrawDone();
    GX_ClearBoundingBox();
    _ogx_setup_3D_projection();

    /* When restoring the EFB we alter the cull mode, Z mode, alpha
     * compare and more. All these settings need to be restored. */
    _ogx_apply_state();
}

void _ogx_stencil_save_to_efb()
{
    GX_DrawDone();
    check_bounding_box();
    debug(OGX_LOG_STENCIL, "Saving EFB to stencil buffer, restoring color");
    _ogx_efb_buffer_save(s_stencil_buffer, OGX_EFB_COLOR);
}

static bool setup_tev_full(int *stages, int *tex_coords,
                           int *tex_maps, int *tex_mtxs,
                           bool invert_logic)
{
    u8 stage = GX_TEVSTAGE0 + *stages;
    u8 tex_coord = GX_TEXCOORD0 + *tex_coords;
    u8 tex_map = GX_TEXMAP0 + *tex_maps;
    u8 tex_mtx = GX_TEXMTX0 + *tex_mtxs * 3;

    /* TODO: keep track of the potential values of the stencil buffer, and
     * avoid drawing if we are sure that a match cannot happen. */
    uint8_t masked_ref = glparamstate.stencil.ref & glparamstate.stencil.mask;
    uint8_t comp_func = invert_logic ?
        invert_comp(glparamstate.stencil.func) : glparamstate.stencil.func;
    TevComparisonType comp_type = comparison_type(comp_func, masked_ref);
    if (!tev_stage_needed(comp_type)) {
        warning("TEV stage not needed");
        return comp_type == TEV_COMP_ALWAYS;
    }

    debug(OGX_LOG_STENCIL, "%d TEV stages, %d tex_coords, %d tex_maps",
          *stages, *tex_coords, *tex_maps);
    u8 logical_op;
    u8 ref_value = GX_CA_KONST;
    bool invert_operands = false;
    switch (comp_func) {
    case GX_EQUAL:
        logical_op = GX_TEV_COMP_A8_EQ;
        break;
    case GX_GREATER:
        invert_operands = true;
        // fallthrough
    case GX_LESS:
        logical_op = GX_TEV_COMP_A8_GT;
        break;
    case GX_LEQUAL:
        logical_op = GX_TEV_COMP_A8_GT;
        masked_ref += 1; /* because a<=b <=> b+1>a, when a and b are ints */
        break;
    case GX_GEQUAL:
        logical_op = GX_TEV_COMP_A8_GT;
        invert_operands = true;
        masked_ref -= 1; /* because a>=b <=> a>b-1, when a and b are ints */
        break;
    case GX_NEQUAL:
        ref_value = GX_CA_ZERO;
        logical_op = GX_TEV_COMP_A8_GT;
        break;
    default:
        warning(" ########## Unhandled stencil comparison: %d\n", glparamstate.stencil.func);
    }

    debug(OGX_LOG_STENCIL, "masked ref = %d, logical op = %d, invert = %d",
          masked_ref, logical_op, invert_operands);
    if (ref_value == GX_CA_KONST) {
        GX_SetTevKColorSel(stage, GX_TEV_KCSEL_K0);
        GX_SetTevKAlphaSel(stage, GX_TEV_KASEL_K0_A);
        if (!stencil_8bit()) {
            /* Replicate the value in the upper 4 bits */
            masked_ref |= masked_ref << 4;
        }
        GXColor ref_color = { 0, 0, 0, masked_ref};
        GX_SetTevKColor(GX_KCOLOR0, ref_color);
    }

    /* Set a TEV stage that draws only where the stencil texture is > 0 */
    GX_SetTevColorIn(stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
    GX_SetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    /* Set a logical operation: output = d + ((a OP b) ? c:0) */
    if (!invert_operands) {
        GX_SetTevAlphaIn(stage, GX_CA_TEXA, ref_value, GX_CA_APREV, GX_CA_ZERO);
    } else {
        GX_SetTevAlphaIn(stage, ref_value, GX_CA_TEXA, GX_CA_APREV, GX_CA_ZERO);
    }
    GX_SetTevAlphaOp(stage, logical_op, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevOrder(stage, tex_coord, tex_map, GX_COLORNULL);

    update_stencil_texture();

    /* Add a set of texture coordinates that exactly match the viewport
     * coordinates of each vertex. This is done by multiplying the vertex
     * positions by the model view matrix and by the projection matrix.
     * Note that since the texture coordinate generator only works with
     * matrices up to 3x4, it's unable to multiply the full 4x4 matrix needed
     * for the projection, so we have to resort to some hacks (described
     * below).
     * Here we just take the movel view matrix and apply the scale on the X and
     * Y coordinates from the projection matrix. */
    Mtx m;
    guMtxScaleApply(glparamstate.modelview_matrix, m,
                    glparamstate.projection_matrix[0][0],
                    glparamstate.projection_matrix[1][1],
                    1.0);
    u8 matrix_type;
    if (glparamstate.projection_matrix[3][3] != 0) {
        /* Othographic projection: this can be handled by a 2x4 matrix. We
         * apply a scale & translation matrix to transform the [-1,1]x[-1,1]
         * clip space coordinates to the [0,1]x[0,1] range which we need for
         * the s and t texture coordinates. This is can be done by scaling by
         * 0.5 and translating along the positive X and Y axes by 0.5. */
        matrix_type = GX_TG_MTX2x4;
        const static Mtx trans = {
            {0.5,    0, 0, 0.5},
            {0,   -0.5, 0, 0.5},
            {0,      0,  0, 1}, /* this row is ignored */
        };
        guMtxConcat(trans, m, m);
        GX_LoadTexMtxImm(m, tex_mtx, GX_MTX2x4);
    } else {
        /* Perspective projection: this is conceptually harder, because the
         * operation that we need to perform (and that we do usually perform
         * when drawing to the screen) involves dividing the x, y, z
         * coordinates by the w coordinate, whose value in turn depends on the
         * z coordinate. See the PDF document for an explanation of why this
         * works. */
        matrix_type = GX_TG_MTX3x4;
        const static Mtx trans = {
            {-0.5,   0, 0.5, 0},
            {0,    0.5, 0.5, 0},
            {0,      0,   1, 0},
        };
        guMtxConcat(trans, m, m);
        GX_LoadTexMtxImm(m, tex_mtx, GX_MTX3x4);
    }

    GX_SetTexCoordGen(tex_coord, matrix_type, GX_TG_POS, tex_mtx);

    GX_LoadTexObj(&s_stencil_texture, tex_map);
    ++(*stages);
    ++(*tex_coords);
    ++(*tex_maps);
    ++(*tex_mtxs);
    return true;
}

static bool draw_op(uint16_t op,
                    bool check_stencil, bool invert_stencil,
                    bool check_z, bool invert_z,
                    OgxStencilDrawCallback callback, void *cb_data)
{
    if (op == GL_KEEP) {
        /* nothing to do */
        return false;
    }

    int num_stages = 1;
    int num_tex_coords = 0;
    int num_tex_maps = 0;
    int num_tex_mtxs = 0;

    uint8_t masked_ref = glparamstate.stencil.ref & glparamstate.stencil.wmask;
    if (!stencil_8bit()) {
        /* Replicate the nibble to fill the whole byte */
        masked_ref |= masked_ref << 4;
    }
    GXColor refColor = {
        masked_ref,
        masked_ref,
        masked_ref,
        255
    };

    GXColor drawColor;
    if (op == GL_REPLACE) {
        drawColor = refColor;
    } else if (op == GL_ZERO) {
        drawColor.r = drawColor.g = drawColor.b = 0;
        drawColor.a = 255;
    } else {
        /* TODO: either find a blend mode to implement the desired effect
         * (this is probably possible for the GL_INCR and GL_DECR
         * operations, but note that then we'd need to move the stencil check
         * to a TEV stage in order to make the blending operation available for
         * them), or render to an intermediate buffer and then manually update
         * the stencil buffer pixel by pixel (use a bounding box to reduce the
         * area). */
        warning("Stencil operation %04x not implemented");
        drawColor = refColor;
    }

    _ogx_efb_set_content_type(OGX_EFB_STENCIL);

    /* Unconditionally enable color updates when drawing on the stencil buffer.
     */
    GX_SetColorUpdate(GX_TRUE);
    glparamstate.dirty.bits.dirty_color_update = 1;

    u8 stage = GX_TEVSTAGE0;
    GX_SetTevColor(GX_TEVREG0, drawColor);
    GX_SetTevOrder(stage, GX_TEXCOORDNULL, GX_TEXMAP_DISABLE, GX_COLOR0A0);
    /* Pass the constant color */
    GX_SetTevColorIn(stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
    GX_SetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
    GX_SetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                     GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                     GX_TRUE, GX_TEVPREV);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);


    if (check_stencil) {
        bool must_draw =
            setup_tev_full(&num_stages, &num_tex_coords,
                           &num_tex_maps, &num_tex_mtxs,
                           invert_stencil);
        if (!must_draw) return false;
    }

    s_stencil_texture_needs_update = true;

    GX_SetNumTexGens(num_tex_coords);
    GX_SetNumTevStages(num_stages);

    if (check_z) {
        /* Use the Z-buffer, but don't modify it! */
        u8 comp = invert_z ?
            invert_comp(glparamstate.zfunc) : glparamstate.zfunc;
        GX_SetZMode(GX_TRUE, comp, GX_FALSE);
    } else {
        GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    }
    glparamstate.dirty.bits.dirty_z = 1;

    GX_SetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_COPY);
    glparamstate.dirty.bits.dirty_blend = 1;

    /* Draw */
    callback(cb_data);
    return true;
}

bool _ogx_stencil_setup_tev(int *stages, int *tex_coords,
                            int *tex_maps, int *tex_mtxs)
{
    return setup_tev_full(stages, tex_coords, tex_maps, tex_mtxs, false);
}

void _ogx_stencil_draw(OgxStencilDrawCallback callback, void *cb_data)
{
    /* If all of op_fail, op_zpass and op_zfail are the same, we can use a
     * single draw operation to update the stencil buffer, since it would
     * happen unconditionally */
    bool single_op =
        (glparamstate.stencil.op_fail == glparamstate.stencil.op_zpass &&
         glparamstate.stencil.op_zpass == glparamstate.stencil.op_zfail &&
         glparamstate.stencil.op_zfail == glparamstate.stencil.op_fail);
    bool check_stencil, invert_stencil, check_z, invert_z;
    if (single_op) {
        check_stencil = false;
        invert_stencil = false;
        check_z = false;
        invert_z = false;
        draw_op(glparamstate.stencil.op_fail,
                check_stencil, invert_stencil, check_z, invert_z,
                callback, cb_data);
    } else {
        /* Perform the three operations separately */
        check_stencil = true;
        invert_stencil = true;
        check_z = false;
        invert_z = false;
        draw_op(glparamstate.stencil.op_fail,
                check_stencil, invert_stencil, check_z, invert_z,
                callback, cb_data);

        invert_stencil = false;
        check_z = true;
        draw_op(glparamstate.stencil.op_zpass,
                check_stencil, invert_stencil, check_z, invert_z,
                callback, cb_data);

        invert_z = true;
        draw_op(glparamstate.stencil.op_zfail,
                check_stencil, invert_stencil, check_z, invert_z,
                callback, cb_data);
    }
}

void _ogx_stencil_enabled()
{
    glparamstate.stencil.enabled = 1;
    glparamstate.dirty.bits.dirty_stencil = 1;
}

void _ogx_stencil_disabled()
{
    glparamstate.stencil.enabled = 0;
    glparamstate.dirty.bits.dirty_stencil = 1;
}

void _ogx_stencil_update()
{
    u16 width = glparamstate.viewport[2];
    u16 height = glparamstate.viewport[3];
    if (width == 0 || height == 0) return;

    u16 old_width = GX_GetTexObjWidth(&s_stencil_texture);
    u16 old_height = GX_GetTexObjHeight(&s_stencil_texture);
    if (width == old_width && height == old_height) return;

    /* This method also gets called when the viewport size changes. Get rid of
     * any existing stencil buffer. */
    if (s_stencil_buffer) {
        free(MEM_PHYSICAL_TO_K0(GX_GetTexObjData(&s_stencil_texture)));
        free(s_stencil_buffer);
        s_stencil_buffer = NULL;
    }

    if (!s_wants_stencil) return;

    u8 format = s_stencil_format;
    _ogx_efb_buffer_prepare(&s_stencil_buffer, format);
    u32 size = GX_GetTexBufferSize(width, height, format, 0, GX_FALSE);
    memset(_ogx_efb_buffer_get_texels(s_stencil_buffer), 0, size);
    void *stencil_texels = memalign(32, size);
    memset(stencil_texels, 0, size);
    DCStoreRange(stencil_texels, size);

    GX_InitTexObj(&s_stencil_texture, stencil_texels, width, height,
                  format, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&s_stencil_texture, GX_NEAR, GX_NEAR,
                     0.0f, 0.0f, 0.0f, 0, 0, GX_ANISO_1);
    GX_InvalidateTexAll();
}

void _ogx_stencil_clear()
{
    if (!s_wants_stencil) return;

    u16 width = GX_GetTexObjWidth(&s_stencil_texture);
    u16 height = GX_GetTexObjHeight(&s_stencil_texture);
    u32 size = GX_GetTexBufferSize(width, height, s_stencil_format,
                                   0, GX_FALSE);
    int value = glparamstate.stencil.clear;
    if (!stencil_8bit()) {
        value |= value << 4;
    }
    if (s_stencil_buffer) {
        void *texels = _ogx_efb_buffer_get_texels(s_stencil_buffer);
        memset(texels, value, size);
        DCStoreRangeNoSync(texels, size);
    }
    uint8_t *texels = GX_GetTexObjData(&s_stencil_texture);
    if (texels) {
        texels = MEM_PHYSICAL_TO_K0(texels);
        /* TODO: do this only for direct comparisons, otherwise set
         * s_stencil_texture_needs_update to true */
        memset(texels, value, size);
        DCStoreRange(texels, size);
        GX_InvalidateTexAll();
    }

    s_stencil_texture_needs_update = false;
}

OgxEfbBuffer *_ogx_stencil_get_buffer()
{
    return s_stencil_buffer;
}

void ogx_stencil_create(OgxStencilFlags flags)
{
    s_wants_stencil = true;
    _ogx_stencil_flags = flags;
    if (flags & OGX_STENCIL_8BIT) {
        s_stencil_format = GX_CTF_R8;
    } else {
        /* reduce the masks to 4 bits */
        glparamstate.stencil.mask &= 0xf;
        glparamstate.stencil.wmask &= 0xf;
    }
    _ogx_stencil_update();
}

void glStencilFunc(GLenum func, GLint ref, GLuint mask)
{
    uint8_t new_func = gx_compare_from_gl(func);
    /* No sense in storing more than the lower 8 bits */
    uint8_t new_ref = (uint8_t)ref;
    uint8_t new_mask = (uint8_t)mask;
    if (!stencil_8bit()) {
        new_mask &= 0xf;
        new_ref &= 0xf;
    }
    if (new_func != glparamstate.stencil.func) {
        uint8_t old_masked_ref =
            glparamstate.stencil.ref & glparamstate.stencil.mask;
        TevComparisonType old_type =
            comparison_type(glparamstate.stencil.func, old_masked_ref);
        TevComparisonType new_type =
            comparison_type(new_func, new_ref & new_mask);

        glparamstate.stencil.func = new_func;
        if (tev_stage_needed(new_type) && new_type != old_type)
            s_stencil_texture_needs_update = true;
    }
    if (new_ref != glparamstate.stencil.ref) {
        glparamstate.stencil.ref = new_ref;
        s_stencil_texture_needs_update = true;
    }
    if (new_mask != glparamstate.stencil.mask) {
        glparamstate.stencil.mask = new_mask;
        s_stencil_texture_needs_update = true;
    }
}

void glStencilMask(GLuint mask)
{
    glparamstate.stencil.wmask = (uint8_t)mask;
    if (!stencil_8bit()) {
        glparamstate.stencil.wmask &= 0xf;
    }
}

void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
    glparamstate.stencil.op_fail = fail;
    glparamstate.stencil.op_zfail = zfail;
    glparamstate.stencil.op_zpass = zpass;
}

void glClearStencil(GLint s)
{
    glparamstate.stencil.clear = (uint8_t)s;
}
