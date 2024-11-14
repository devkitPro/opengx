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

#include "gpu_resources.h"

#include <assert.h>
#include <ogc/gx.h>
#include <stdlib.h>
#include <string.h>

/* We won't check the bounds of this stack. We'll just try to be careful not to
 * overrun it (the struct itself is not that big, so we can increase this
 * value, if needed). */
#define GPU_RESOURCES_STACK_SIZE 3

OgxGpuResources *_ogx_gpu_resources = NULL;
OgxGpuResources *s_gpu_resources = NULL;

static void resources_init(OgxGpuResources *resources)
{
    /* Here we can book (steal) some resources that we want to reserve for
     * OpenGX or for the integration library. */
    resources->tevstage_first = 0;
    resources->tevstage_end = GX_MAX_TEVSTAGE;

    resources->kcolor_first = 0;
    resources->kcolor_end = GX_KCOLOR_MAX;

    resources->tevreg_first = 0;
    resources->tevreg_end = GX_MAX_TEVREG - 1; /* we exclude GX_TEVPREV */

    resources->texcoord_first = 0;
    resources->texcoord_end = GX_MAXCOORD;

    /* GX_PNMTX0 is reserved for the modelview matrix */
    resources->pnmtx_first = 1;
    resources->pnmtx_end = 10;

    resources->dttmtx_first = 0;
    resources->dttmtx_end = 20;

    resources->texmtx_first = 0;
    resources->texmtx_end = 10;

    resources->texmap_first = 0;
    resources->texmap_end = 8;
}

void _ogx_gpu_resources_init()
{
    if (s_gpu_resources) return;

    s_gpu_resources =
        malloc(sizeof(OgxGpuResources) * GPU_RESOURCES_STACK_SIZE);
    resources_init(s_gpu_resources);
    _ogx_gpu_resources = s_gpu_resources;
}

void _ogx_gpu_resources_push()
{
    OgxGpuResources *old = _ogx_gpu_resources;
    memcpy(++_ogx_gpu_resources, old, sizeof(OgxGpuResources));
}

void _ogx_gpu_resources_pop()
{
    assert(_ogx_gpu_resources != s_gpu_resources);
    _ogx_gpu_resources--;
}
