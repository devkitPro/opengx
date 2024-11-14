/*****************************************************************************
Copyright (c) 2024  Alberto Mardegan (mardy@users.sourceforge.net)
All rights reserved.

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

#ifndef OPENGX_GPU_RESOURCES_H
#define OPENGX_GPU_RESOURCES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* *_first: number of the first available resource
     * *_end: number of the first *not* available resource
     *
     * The number of available resources is X_end - X_first. Each member
     * specifies the number starting from zero, so that in order to get the ID
     * of the desired resource, you need to add the base ID of the resource:
     * for example, to get the actual stage number, you'd have to do
     *
     *     stage = number + GX_TEVSTAGE0
     *
     * and, for matrix types,
     *
     *     texmtx = number * 3 + GX_TEXMTX0
     *
     * Fields are named according to libogc's constants, to minimize confusion.
     */
    uint8_t tevstage_first;
    uint8_t tevstage_end;
    uint8_t kcolor_first;
    uint8_t kcolor_end;
    uint8_t tevreg_first;
    uint8_t tevreg_end;
    uint8_t texcoord_first;
    uint8_t texcoord_end;
    uint8_t pnmtx_first;
    uint8_t pnmtx_end;
    uint8_t dttmtx_first;
    uint8_t dttmtx_end;
    uint8_t texmtx_first;
    uint8_t texmtx_end;
    uint8_t texmap_first;
    uint8_t texmap_end;
    /* We could add the VTXFMT here too, if we decided to reserve them for
     * specific goals; for the time being, we only use GX_VTXFMT0 and set it up
     * from scratch every time. */
} OgxGpuResources;

extern OgxGpuResources *_ogx_gpu_resources;

void _ogx_gpu_resources_init();
void _ogx_gpu_resources_push();
void _ogx_gpu_resources_pop();

/* TODO: provide an API for the integration library, so that it can book some
 * resources for itself -- or, in alternative, document which resources it can
 * use outside of a frame drawing phase. */

#ifdef __cplusplus
} // extern C
#endif

#endif /* OPENGX_GPU_RESOURCES_H */
