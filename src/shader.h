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

#ifndef OPENGX_SHADER_H
#define OPENGX_SHADER_H

#include "debug.h"
#include "opengx.h"
#include "types.h"

#ifdef BUILDING_SHADER_CODE

typedef struct _OgxBoundAttribute OgxBoundAttribute;
/* This must be large enough to hold up to MAX_VERTEX_ATTRIBS + 1 values */
typedef uint8_t OgxAttrLocation;
typedef int16_t OgxUniformLocation;

typedef struct {
    const char *name;
    GLenum type;
    uint8_t size; /* TODO: currently hardcoded to 1 */
    OgxAttrLocation location;
    uint8_t gx_attribute;
} OgxAttributeVar;

typedef struct {
    const char *name;
    GLenum type;
    uint8_t size; /* TODO: currently hardcoded to 1 */
    OgxUniformLocation location;
} OgxUniformVar;

typedef union {
    float vec4f[4];
    float mat4[16];
    int vec4i[4];
    bool vec4b[4];
} OgxVariableData;

typedef struct {
    OgxUniformVar *uniform;
    OgxVariableData data;
} OgxUniformData;

struct _OgxBoundAttribute {
    OgxBoundAttribute *next;
    OgxAttrLocation index;
    char name[0];
};

struct _OgxShader {
    OgxShader *next;
    GLenum type;
    char attach_count;
    unsigned deletion_requested : 1;
    unsigned compiled : 1;
    uint8_t attribute_count;
    uint16_t uniform_count;
    uint16_t source_length;
    uint32_t source_hash;
    void *user_data;
    OgxAttributeVar *attributes;
    OgxUniformVar *uniforms;
    void (*setup_draw)(GLuint shader, const OgxDrawData *draw_data,
                       void *user_data);
};

struct _OgxProgram {
    OgxProgram *next;
    OgxShader *vertex_shader;
    OgxShader *fragment_shader;
    unsigned deletion_requested : 1;
    unsigned linked : 1;
    unsigned linked_ok : 1;
    uint8_t attribute_count;
    uint16_t uniform_count;
    uint16_t uniform_location_count;

    /* List of the attributes bound by glBindAttribLocation(). */
    OgxBoundAttribute *bound_attributes;

    /* NULL-terminated arrays pointing to all attributes and uniforms (without
     * repetitions).
     * To reduce memory fragmentation, these are placed on the same memory
     * allocation. */
    OgxAttributeVar **attributes;
    OgxUniformVar **uniforms;

    /* maps attribute locations indexes to the index of attribute in the
     * `attributes` array (-1 means inactive) */
    int8_t active_attributes[MAX_VERTEX_ATTRIBS];
    /* Active attribute location indexes, sorted by the order in which they
     * have to be sent to GX (first postion, then normal, colors, texture
     * coordinates). */
    OgxAttrLocation locations_sorted_gx[MAX_VERTEX_ATTRIBS];

    /* Block of memory where uniform data is stored. The
     * uniform_location_offsets array tells the offset at which the uniform
     * data (in the form of a OgxUniformData struct) is located for the given
     * uniform location. */
    void *uniform_data_base;
    uint16_t *uniform_location_offsets;

    void *user_data;
    OgxSetupDrawCb setup_draw_cb;
    OgxCleanupCb cleanup_user_data_cb;
};

typedef struct {
    /* Use these to navigate shaders and programs as a list */
    OgxShader *shaders;
    OgxProgram *programs;

    struct _OgxVertexAttribState {
        unsigned array_enabled : 1;
        /* Used when array_enabled is true: */
        OgxVertexAttribArray array;
    } vertex_attribs[MAX_VERTEX_ATTRIBS];

    /* Data fields (used when array_enabled is false). We keep these in a
     * separate array so that we can use consecutive elements as matrix
     * columns. */
    Vec4f vertex_attrib_data[MAX_VERTEX_ATTRIBS];
} OgxShaderState;

typedef struct _OgxVertexAttribState OgxVertexAttribState;

#define PROGRAM_TO_INT(p) ((GLuint)p)
#define PROGRAM_FROM_INT(p) ((OgxProgram *)p)
#define SHADER_TO_INT(s) ((GLuint)s)
#define SHADER_FROM_INT(s) ((OgxShader *)s)

extern OgxFunctions _ogx_shader_functions;
extern OgxShaderState _ogx_shader_state;

void _ogx_shader_initialize();
void _ogx_shader_setup_draw(const OgxDrawData *draw_data);
void _ogx_shader_update_vertex_array_readers(OgxDrawMode mode);

size_t _ogx_size_for_type(GLenum type);

#else /* BUILDING_SHADER_CODE not defined */

/* Define all needed symbols as weak */

void __attribute__((weak)) _ogx_shader_initialize() {}

/* This weak symbol should never be reached: the strong symbol is defined in
 * shader.c, along with the other shader support functions. That code is also
 * responsible of setting glparamstate.current_program to a nonzero value, and
 * since this condition is the trigger to call _ogx_shader_setup_draw(), when
 * the condition is satisfied we should also have the strong definition of this
 * symbol.
 */
void __attribute__((weak)) _ogx_shader_setup_draw(const OgxDrawData *)
{
    warning("Rendering via shaders is not enabled");
}

void __attribute__((weak)) _ogx_shader_update_vertex_array_readers(OgxDrawMode)
{
}

OgxFunctions _ogx_shader_functions __attribute__((weak)) = { 0, NULL };

#endif /* BUILDING_SHADER_CODE */

static inline bool _ogx_has_shaders()
{
    return _ogx_shader_functions.num_functions > 0;
}

#endif /* OPENGX_SHADER_H */
