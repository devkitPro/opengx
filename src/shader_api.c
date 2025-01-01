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

#include "debug.h"
#include "opengx.h"
#include "shader.h"
#include "state.h"
#include "utils.h"

#include <GL/gl.h>
#include <GL/glext.h>

static void scale_matrix(const GLfloat *matrix, float divisor, float *out)
{
    for (int i = 0; i < 16; i++)
        out[i] = matrix[i] / divisor;
}

void ogx_set_mvp_matrix(const GLfloat *matrix)
{
    Mtx44 proj;
    Mtx mv;
    float scaled[16];
    u8 type;

    /* Decomposes the MVP matrix into MV and P matrices. Note that this is not
     * a canonical (meaning) decomposition like the one implemented in
     * https://github.com/ChromiumWebApps/chromium/blob/master/ui/gfx/transform_util.cc
     *
     * Here we only care about producing a P matrix that can be fed to GX
     * (which expects only a few matrix elements to be set) and that the
     * equation MVP = MV * P holds. */
    if (matrix[11] != 0) {
        /* it's a perspective matrix */
        type = GX_PERSPECTIVE;
        memset(proj, 0, sizeof(proj));
        float p22 = -matrix[10] / matrix[11];
        proj[2][2] = p22;
        proj[0][0] = proj[1][1] = 1.0f;
        proj[2][3] = p22 * matrix[15] + matrix[14];
        proj[3][2] = -1.0f;

        mv[0][0] = matrix[0];
        mv[1][0] = matrix[1];
        mv[2][0] = -matrix[3];
        mv[0][1] = matrix[4];
        mv[1][1] = matrix[5];
        mv[2][1] = -matrix[7];
        mv[0][2] = matrix[8];
        mv[1][2] = matrix[9];
        mv[2][2] = -matrix[11];
        mv[0][3] = matrix[12];
        mv[1][3] = matrix[13];
        mv[2][3] = -matrix[15];
    } else {
        /* it's an orthographic matrix */
        type = GX_ORTHOGRAPHIC;
        if (matrix[15] != 1.0) {
            scale_matrix(matrix, matrix[15], scaled);
            matrix = scaled;
        }

        guMtx44Identity(proj);
        for (int i = 0; i < 12; i++) {
            int row = i / 4;
            int col = i % 4;
            mv[row][col] = matrix[col * 4 + row];
        }
    }
    ogx_set_projection_gx(proj);
    GX_LoadPosMtxImm(mv, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);

    /* In the unlikely case that some fixed-pipeline drawing happens, we mark
     * the matrices as dirty so that they'd be reconfigured when needed. */
    glparamstate.dirty.bits.dirty_matrices = 1;
}
