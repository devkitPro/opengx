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

#ifndef OPENGX_ARRAYS_H
#define OPENGX_ARRAYS_H

#include "opengx.h"
#include "types.h"

#include <GL/gl.h>
#include <gccore.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Opaque struct */
    uint32_t reader[6];
} OgxArrayReader;

typedef enum {
    OGX_DRAW_FLAG_NONE = 0,
    OGX_DRAW_FLAG_FLAT = 1 << 0,
} OgxDrawFlags;

void _ogx_arrays_setup_draw(const OgxDrawData *draw_data, OgxDrawFlags flags);

void _ogx_arrays_process_element(int index);
/* Any memory allocated by the OgxArrayReader objects can be released. */
void _ogx_arrays_draw_done();
void _ogx_array_reader_process_element(OgxArrayReader *reader, int index);
uint8_t _ogx_array_reader_get_tex_coord_source(OgxArrayReader *reader);

void _ogx_array_reader_read_pos3f(OgxArrayReader *reader,
                                  int index, float *pos);
void _ogx_array_reader_read_norm3f(OgxArrayReader *reader,
                                   int index, float *norm);
void _ogx_array_reader_read_tex2f(OgxArrayReader *reader,
                                  int index, float *tex);
void _ogx_array_reader_read_color(OgxArrayReader *reader,
                                  int index, GXColor *color);

void _ogx_arrays_reset(void);
OgxArrayReader *_ogx_array_add_constant_fv(uint8_t attribute, int size,
                                           const float *values);
OgxArrayReader *_ogx_array_add(uint8_t attribute,
                               const OgxVertexAttribArray *array);

typedef void (*OgxGenerator_fv)(int index, float *values_out);
OgxArrayReader *_ogx_array_add_generator_fv(uint8_t attribute, int size,
                                            OgxGenerator_fv generator);

/* Enumerate active array readers. Start by passing NULL, then pass the reader
 * obtained from the previous call, until this returns NULL.
 */
OgxArrayReader *_ogx_array_reader_next(OgxArrayReader *reader);

/* Get the n-th array for the given attribute. NULL is returned if no array was
 * added for the given attribute. */
OgxArrayReader *_ogx_array_reader_for_attribute(uint8_t attribute);

/* This returns the libogc constants describing the vertex format:
 * - attribute: libogc's vtxattr
 * - inputmode: libogc's vtxattrin
 * - type: libogc's comptype
 * - size: libogc's comptype
 */
void _ogx_array_reader_get_format(OgxArrayReader *reader,
                                  uint8_t *attribute, uint8_t *inputmode,
                                  uint8_t *type, uint8_t *size);

#ifdef __cplusplus
} // extern C
#endif

#endif /* OPENGX_ARRAYS_H */
