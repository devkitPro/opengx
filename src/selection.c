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

#include "selection.h"

#include "debug.h"
#include "state.h"
#include "utils.h"

#include <malloc.h>

static uint8_t *s_zbuffer_backup = NULL;

static void enter_selection_mode()
{
    if (glparamstate.select_buffer == NULL) {
        set_error(GL_INVALID_OPERATION);
        return;
    }

    /* Save the current Z-buffer contents */
    u16 width = glparamstate.viewport[2];
    u16 height = glparamstate.viewport[3];
    u32 size = GX_GetTexBufferSize(width, height, GX_TF_Z24X8, 0, GX_FALSE);
    s_zbuffer_backup = memalign(32, size);
    DCInvalidateRange(s_zbuffer_backup, size);

    /* Disable color and alpha updates (TODO: we could also simplify the
     * rendering by disabling texturing and lighting) */
    GX_SetColorUpdate(GX_DISABLE);
    GX_SetAlphaUpdate(GX_DISABLE);

    GX_SetTexCopySrc(glparamstate.viewport[0],
                     glparamstate.viewport[1],
                     width,
                     height);
    GX_SetTexCopyDst(width, height, GX_TF_Z24X8, GX_FALSE);
    GX_CopyTex(s_zbuffer_backup, GX_TRUE);

    /* Disable Z-buffer comparisons, but keep writes enabled, since we will
     * read the Z-buffer when a hit is recorded. */
    GX_SetZMode(GX_ENABLE, GX_ALWAYS, GX_ENABLE);

    /* Clear the bounding box in order to understand if something has been
     * drawn. */
    GX_DrawDone();
    GX_ClearBoundingBox();
}

static void restore_z_buffer()
{
    _ogx_setup_2D_projection();

    GX_SetZTexture(GX_ZT_REPLACE, GX_TF_Z24X8, 0);
    GX_SetZCompLoc(GX_DISABLE);

    u16 width = glparamstate.viewport[2];
    u16 height = glparamstate.viewport[3];
    GXTexObj texobj;
    GX_InitTexObj(&texobj, s_zbuffer_backup, width, height,
                  GX_TF_Z24X8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&texobj, GX_NEAR, GX_NEAR,
                     0.0f, 0.0f, 0.0f, 0, 0, GX_ANISO_1);
    GX_LoadTexObj(&texobj, GX_TEXMAP0);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_U16, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_U8, 0);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetNumTexGens(1);
    GX_SetNumTevStages(1);
    GX_SetNumChans(0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
    glparamstate.dirty.bits.dirty_tev = 1;

    GX_SetCullMode(GX_CULL_NONE);
    glparamstate.dirty.bits.dirty_cull = 1;

    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2u16(0, 0);
    GX_TexCoord2u8(0, 0);
    GX_Position2u16(0, height);
    GX_TexCoord2u8(0, 1);
    GX_Position2u16(width, height);
    GX_TexCoord2u8(1, 1);
    GX_Position2u16(width, 0);
    GX_TexCoord2u8(1, 0);
    GX_End();

    GX_SetZTexture(GX_ZT_DISABLE, GX_TF_Z24X8, 0);
    GX_SetZCompLoc(GX_ENABLE);
}

static void leave_selection_mode()
{
    glparamstate.name_stack_depth = 0;
    glparamstate.select_buffer_offset = 0;
    glparamstate.hit_count = 0;
    glparamstate.dirty.bits.dirty_z = 1;

    if (s_zbuffer_backup) {
        restore_z_buffer();
        free(s_zbuffer_backup);
        s_zbuffer_backup = NULL;
    }

    GX_SetColorUpdate(GX_ENABLE);
    GX_SetAlphaUpdate(GX_ENABLE);
}

static void check_for_hits()
{
    u16 top, bottom, left, right;
    GX_DrawDone();
    /* We know that the bounding box is imprecise (it operates on 2x2 pixel
     * squares), but what matters for us is just whether something has been
     * draw at all */
    GX_ReadBoundingBox(&top, &bottom, &left, &right);
    GX_ClearBoundingBox();
    if (bottom <= top || right <= left) {
        /* No drawing occurred */
        return;
    }
    if (glparamstate.select_buffer_offset < 0) {
        /* We already had an overflow */
        return;
    }

    glparamstate.hit_count++;
    GLuint record[3];
    record[0] = glparamstate.name_stack_depth;
    /* The 2nd and 3rd elements of the hit record are the min and max Z of the
     * affected area. We just set them to 0 because computing them is expensive
     * (we would have to check all values in the framebuffer, since the
     * applications typically use gluPickMatrix() and set up a transformation
     * that zooms on a small area around the mouse cursor and causes the whole
     * viewport to be updated); on the other hand, the values appear to be 0
     * also on my desktop PC, both using the AMD and the Mesa software
     * renderer. */
    record[1] = 0;
    record[2] = 0;
    for (int i = 0;
         glparamstate.select_buffer_size > glparamstate.select_buffer_offset &&
         i < 3; i++) {
        glparamstate.select_buffer[glparamstate.select_buffer_offset] = record[i];
        glparamstate.select_buffer_offset++;
    }
    for (int i = 0;
         glparamstate.select_buffer_size > glparamstate.select_buffer_offset &&
         i < glparamstate.name_stack_depth; i++) {
        glparamstate.select_buffer[glparamstate.select_buffer_offset] =
            glparamstate.name_stack[i];
        glparamstate.select_buffer_offset++;
    }
    if (glparamstate.select_buffer_offset >= glparamstate.select_buffer_size) {
        /* An overflow occurred, we mark it like this: */
        glparamstate.select_buffer_offset = -1;
    }
}

int _ogx_selection_mode_changing(GLenum new_mode)
{
    int hit_count = 0;

    if (new_mode == GL_RENDER && glparamstate.render_mode == GL_SELECT) {
        if (glparamstate.select_buffer == NULL) {
            set_error(GL_INVALID_OPERATION);
            return 0;
        }

        /* We are leaving selection mode, we need to update the client's select
         * buffer */
        check_for_hits();

        hit_count = glparamstate.select_buffer_offset >= 0 ?
            glparamstate.hit_count : -glparamstate.hit_count;

        leave_selection_mode();
    } else if (new_mode == GL_SELECT && glparamstate.render_mode == GL_RENDER) {
        enter_selection_mode();
    }

    return hit_count;
}

void glSelectBuffer(GLsizei size, GLuint *buffer)
{
    if (glparamstate.render_mode == GL_SELECT) {
        set_error(GL_INVALID_OPERATION);
        return;
    }

    if (size < 0) {
        set_error(GL_INVALID_VALUE);
        return;
    }

    glparamstate.select_buffer_size = size;
    glparamstate.select_buffer = buffer;
}

void glInitNames()
{
    if (!glparamstate.name_stack) {
        glparamstate.name_stack = malloc(sizeof(GLuint) * MAX_NAME_STACK_DEPTH);
    }
    glparamstate.name_stack_depth = 0;
}

void glLoadName(GLuint name)
{
    if (glparamstate.render_mode != GL_SELECT) return;
    if (glparamstate.name_stack_depth == 0) {
        set_error(GL_INVALID_OPERATION);
        return;
    }
    check_for_hits();
    glparamstate.name_stack[glparamstate.name_stack_depth - 1] = name;
}

void glPushName(GLuint name)
{
    if (glparamstate.render_mode != GL_SELECT) return;
    if (glparamstate.name_stack_depth == MAX_NAME_STACK_DEPTH) {
        set_error(GL_STACK_OVERFLOW);
        return;
    }
    check_for_hits();
    glparamstate.name_stack[glparamstate.name_stack_depth++] = name;
}

void glPopName()
{
    if (glparamstate.render_mode != GL_SELECT) return;
    if (glparamstate.name_stack_depth == 0) {
        set_error(GL_STACK_UNDERFLOW);
        return;
    }
    check_for_hits();
    glparamstate.name_stack_depth--;
}
