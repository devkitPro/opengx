/*****************************************************************************
Copyright (c) 2011  David Guillen Fandos (david@davidgf.net)
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

#include "debug.h"

#include <stdlib.h>
#include <string.h>

OgxLogMask _ogx_log_mask = 0;

static const struct {
    const char *feature;
    OgxLogMask mask;
} s_feature_masks[] = {
    { "warning", OGX_LOG_WARNING },
    { "call-lists", OGX_LOG_CALL_LISTS },
    { "clipping", OGX_LOG_CLIPPING },
    { "lighting", OGX_LOG_LIGHTING },
    { "texture", OGX_LOG_TEXTURE },
    { "stencil", OGX_LOG_STENCIL },
    { "shader", OGX_LOG_SHADER },
    { NULL, 0 },
};

void _ogx_log_init()
{
    const char *log_env = getenv("OPENGX_DEBUG");
    if (log_env) {
        for (int i = 0; s_feature_masks[i].feature != NULL; i++) {
            if (strstr(log_env, s_feature_masks[i].feature) != NULL) {
                _ogx_log_mask |= s_feature_masks[i].mask;
            }
        }
        if (strcmp(log_env, "all") == 0) {
            _ogx_log_mask = 0xffffffff;
        }
    }
}
