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

#define GL_GLEXT_PROTOTYPES
#define BUILDING_SHADER_CODE
#include "debug.h"
#include "murmurhash3.h"
#include "shader.h"
#include "state.h"
#include "utils.h"

#include <GL/gl.h>
#include <GL/glext.h>
#include <assert.h>
#include <malloc.h>
#include <stdarg.h>

OgxShaderState _ogx_shader_state;
const static OgxProgramProcessor *s_processor = NULL;

/* Get the shader coming after the given one. If s is NULL, returns the first
 * shader in the program.
 */
static OgxShader *program_get_next_shader(const OgxProgram *p, const OgxShader *s)
{
    if (!s) return p->vertex_shader ? p->vertex_shader : p->fragment_shader;
    if (s == p->vertex_shader) return p-> fragment_shader;
    return NULL;
}

static bool add_attribute_to_list(OgxAttributeVar **list, OgxAttributeVar *var)
{
    OgxAttributeVar **v = list;
    while (*v != NULL) {
        if (strcmp((*v)->name, var->name) == 0) return false;
        v++;
    }
    /* No terminating NULL, as the array was 0-initialized */
    *v = var;
    return true;
}

static bool add_uniform_to_list(OgxUniformVar **list, OgxUniformVar *var)
{
    OgxUniformVar **v = list;
    while (*v != NULL) {
        if (strcmp((*v)->name, var->name) == 0) return false;
        v++;
    }
    /* No terminating NULL, as the array was 0-initialized */
    *v = var;
    return true;
}

static inline OgxVertexAttribState *get_vertex_attrib(int index)
{
    if (index >= MAX_VERTEX_ATTRIBS) {
        set_error(GL_INVALID_VALUE);
        return NULL;
    }

    return &_ogx_shader_state.vertex_attribs[index];
}

static int num_locations_for_type(GLenum type)
{
    switch (type) {
    case GL_FLOAT_MAT2:
    case GL_FLOAT_MAT2x3:
    case GL_FLOAT_MAT2x4:
        return 2;
    case GL_FLOAT_MAT3:
    case GL_FLOAT_MAT3x2:
    case GL_FLOAT_MAT3x4:
        return 3;
    case GL_FLOAT_MAT4:
    case GL_FLOAT_MAT4x2:
    case GL_FLOAT_MAT4x3:
        return 4;
    default:
        return 1;
    }
}

size_t _ogx_size_for_type(GLenum type)
{
    /* We'all always store doubles as floats, so this function handles them
     * together */
    switch (type) {
    case GL_DOUBLE:
    case GL_FLOAT:
        return sizeof(float);
    case GL_DOUBLE_VEC2:
    case GL_FLOAT_VEC2:
        return sizeof(float) * 2;
    case GL_DOUBLE_VEC3:
    case GL_FLOAT_VEC3:
        return sizeof(float) * 3;
    case GL_DOUBLE_VEC4:
    case GL_DOUBLE_MAT2:
    case GL_FLOAT_VEC4:
    case GL_FLOAT_MAT2:
        return sizeof(float) * 4;
    case GL_DOUBLE_MAT2x3:
    case GL_DOUBLE_MAT3x2:
    case GL_FLOAT_MAT2x3:
    case GL_FLOAT_MAT3x2:
        return sizeof(float) * 6;
    case GL_DOUBLE_MAT2x4:
    case GL_DOUBLE_MAT4x2:
    case GL_FLOAT_MAT2x4:
    case GL_FLOAT_MAT4x2:
        return sizeof(float) * 8;
    case GL_DOUBLE_MAT3:
    case GL_FLOAT_MAT3:
        return sizeof(float) * 9;
    case GL_DOUBLE_MAT3x4:
    case GL_DOUBLE_MAT4x3:
    case GL_FLOAT_MAT3x4:
    case GL_FLOAT_MAT4x3:
        return sizeof(float) * 12;
    case GL_DOUBLE_MAT4:
    case GL_FLOAT_MAT4:
        return sizeof(float) * 16;
    case GL_INT:
    case GL_UNSIGNED_INT:
        return sizeof(int);
    case GL_INT_VEC2:
    case GL_UNSIGNED_INT_VEC2:
        return sizeof(int) * 2;
    case GL_INT_VEC3:
    case GL_UNSIGNED_INT_VEC3:
        return sizeof(int) * 3;
    case GL_INT_VEC4:
    case GL_UNSIGNED_INT_VEC4:
        return sizeof(int) * 4;
    case GL_BOOL:
        return sizeof(bool);
    case GL_BOOL_VEC2:
        return sizeof(bool) * 2;
    case GL_BOOL_VEC3:
        return sizeof(bool) * 3;
    case GL_BOOL_VEC4:
        return sizeof(bool) * 4;
    default:
        return 1;
    }
}

static inline size_t padded_4(size_t size) {
    return ((size + 3) / 4) * 4;
}

/* Padded size required to hold a OgxUniformData struct for the given type */
static inline size_t uniform_data_struct_size(GLenum type)
{
    return padded_4(_ogx_size_for_type(type)) + sizeof(void*);
}

static OgxAttributeVar *
get_attr_variable_for_location(OgxProgram *p, OgxAttrLocation location)
{
    if (location >= MAX_VERTEX_ATTRIBS) return NULL;

    int8_t i = p->active_attributes[location];
    return i >= 0 ? p->attributes[i] : NULL;
}

static char get_attribute_bound_location(OgxProgram *p, const char *name)
{
    OgxBoundAttribute *a = p->bound_attributes;
    while (a) {
        if (strcmp(a->name, name) == 0) return a->index;
        a = a->next;
    }
    return MAX_VERTEX_ATTRIBS; /* invalid value */
}

void glAttachShader(GLuint program, GLuint shader)
{
    OgxProgram *p = PROGRAM_FROM_INT(program);
    OgxShader *s = SHADER_FROM_INT(shader);
    if (!s || !p) {
        set_error(GL_INVALID_OPERATION);
        return;
    }

    /* The OpenGL spec allows attaching more than one shader per type, in which
     * case their code gets concatenated. For the time being, we leave it as a
     * TODO. */
    OgxShader **shader_dest = NULL;
    switch (s->type) {
    case GL_FRAGMENT_SHADER:
        shader_dest = &p->fragment_shader; break;
    case GL_VERTEX_SHADER:
        shader_dest = &p->vertex_shader; break;
    default:
        set_error(GL_INVALID_OPERATION);
        return;
    }
    if (*shader_dest) {
        set_error(GL_STACK_OVERFLOW);
        return;
    }

    *shader_dest = s;
    s->attach_count++;
}

void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name)
{
    OgxProgram *p = PROGRAM_FROM_INT(program);
    if (!p) {
        set_error(GL_INVALID_OPERATION);
        return;
    }

    if (index >= MAX_VERTEX_ATTRIBS) {
        set_error(GL_INVALID_VALUE);
        return;
    }

    OgxBoundAttribute *prev = NULL;
    OgxBoundAttribute *binding = p->bound_attributes;
    while (binding) {
        if (strcmp(binding->name, name) == 0) break;
        prev = binding;
        binding = binding->next;
    }

    bool is_new = binding == NULL;
    if (binding == NULL) {
        binding = malloc(sizeof(OgxBoundAttribute) + strlen(name) + 1);
        binding->next = NULL;
        strcpy(binding->name, name);
    }
    binding->index = index;
    if (prev) prev->next = binding;
    else p->bound_attributes = binding;
}

void glCompileShader(GLuint shader)
{
    OgxShader *s = SHADER_FROM_INT(shader);

    if (!s) {
        set_error(GL_INVALID_VALUE);
        return;
    }

    if (s_processor->compile_shader) {
        s->compiled = s_processor->compile_shader(shader);
    } else {
        s->compiled = true;
    }
}

GLuint glCreateProgram(void)
{
    if (!s_processor) return 0;

    OgxProgram *p = calloc(1, sizeof(OgxProgram));
    p->next = _ogx_shader_state.programs;
    _ogx_shader_state.programs = p;
    return PROGRAM_TO_INT(p);
}

GLuint glCreateShader(GLenum type)
{
    if (!s_processor) return 0;

    switch (type) {
    case GL_FRAGMENT_SHADER:
    case GL_VERTEX_SHADER:
        break;
    default:
        set_error(GL_INVALID_ENUM);
        return 0;
    }

    OgxShader *s = calloc(1, sizeof(OgxShader));
    s->next = _ogx_shader_state.shaders;
    _ogx_shader_state.shaders = s;
    s->type = type;
    return SHADER_TO_INT(s);
}

void glDeleteProgram(GLuint program)
{
    OgxProgram *p = PROGRAM_FROM_INT(program);
    if (!p) return;

    if (program == glparamstate.current_program) {
        p->deletion_requested = true;
        return;
    }

    OgxShader *s = NULL;
    while (s = program_get_next_shader(p, s)) {
        glDetachShader(program, SHADER_TO_INT(s));
    }

    if (p->user_data && p->cleanup_user_data_cb) {
        p->cleanup_user_data_cb(p->user_data);
    }
    free(p->attributes);
    free(p);
}

void glDeleteShader(GLuint shader)
{
    OgxShader *s = SHADER_FROM_INT(shader);
    if (s->attach_count > 0) {
        s->deletion_requested = true;
    } else {
        OgxShader **prev = &_ogx_shader_state.shaders;
        while (*prev != s) *prev = (*prev)->next;
        *prev = s->next;
        free(s);
    }
}

void glDetachShader(GLuint program, GLuint shader)
{
    OgxProgram *p = PROGRAM_FROM_INT(program);
    OgxShader *s = SHADER_FROM_INT(shader);

    OgxShader **dest_ptr = NULL;
    if (s == p->vertex_shader) {
        dest_ptr = &p->vertex_shader;
    } else if (s == p->fragment_shader) {
        dest_ptr = &p->fragment_shader;
    }

    if (!dest_ptr) {
        set_error(GL_INVALID_OPERATION);
        return;
    }

    s->attach_count--;
    *dest_ptr = NULL;
    if (s->deletion_requested) {
        glDeleteShader(shader);
    }
}

void glDisableVertexAttribArray(GLuint index)
{
    if (glparamstate.compat_profile && index == 0) {
        glDisableClientState(GL_VERTEX_ARRAY);
        return;
    }

    OgxVertexAttribState *v = get_vertex_attrib(index);
    if (!v) return;
    v->array_enabled = 0;
    glparamstate.dirty.bits.dirty_attributes = 1;
}

void glEnableVertexAttribArray(GLuint index)
{
    if (glparamstate.compat_profile && index == 0) {
        glEnableClientState(GL_VERTEX_ARRAY);
        return;
    }

    OgxVertexAttribState *v = &_ogx_shader_state.vertex_attribs[index];
    if (!v) return;
    v->array_enabled = 1;
    glparamstate.dirty.bits.dirty_attributes = 1;
}

void glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize,
                       GLsizei *length, GLint *size, GLenum *type,
                       GLchar *name)
{
    OgxProgram *p = PROGRAM_FROM_INT(program);
    if (!p) {
        set_error(GL_INVALID_OPERATION);
        return;
    }

    OgxAttributeVar *v = get_attr_variable_for_location(p, index);
    if (!v) {
        set_error(GL_INVALID_VALUE);
        return;
    }

    size_t l = snprintf(name, bufSize, "%s", v->name);
    if (length) *length = l;
    *size = v->size;
    *type = v->type;
}

void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize,
                        GLsizei *length, GLint *size, GLenum *type,
                        GLchar *name)
{
    OgxProgram *p = PROGRAM_FROM_INT(program);
    if (!p) {
        set_error(GL_INVALID_OPERATION);
        return;
    }

    /* We consider all uniforms declared by the shader to be active */
    if (index >= p->uniform_count) {
        set_error(GL_INVALID_VALUE);
        return;
    }

    OgxUniformVar *v = p->uniforms[index];
    size_t l = snprintf(name, bufSize, "%s", v->name);
    if (length) *length = l;
    *size = v->size;
    *type = v->type;
}

void glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei *count, GLuint *shaders)
{
    OgxProgram *p = PROGRAM_FROM_INT(program);
    if (!p) {
        set_error(GL_INVALID_OPERATION);
        return;
    }

    int i = 0;
    OgxShader *s = NULL;
    while ((s = program_get_next_shader(p, s)) && i < maxCount) {
        shaders[i++] = SHADER_TO_INT(s);
    }
    if (count) *count = i;
}

GLint glGetAttribLocation(GLuint program, const GLchar *name)
{
    OgxProgram *p = PROGRAM_FROM_INT(program);
    if (!p) {
        set_error(GL_INVALID_OPERATION);
        return -1;
    }

    OgxShader *s = NULL;
    while (s = program_get_next_shader(p, s)) {
        for (int i = 0; i < s->attribute_count; i++) {
            OgxAttributeVar *v = &s->attributes[i];
            if (strcmp(v->name, name) == 0) return v->location;
        }
    }
    return -1;
}

void glGetProgramiv(GLuint program, GLenum pname, GLint *params)
{
    OgxProgram *p = PROGRAM_FROM_INT(program);
    if (!p) {
        set_error(GL_INVALID_OPERATION);
        return;
    }

    switch (pname) {
    case GL_ACTIVE_ATTRIBUTES:
        {
            int count = 0;
            for (int i = 0; i < MAX_VERTEX_ATTRIBS; i++)
                if (p->active_attributes[i] >= 0) count++;
            *params = count;
        }
        break;
    case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH:
        {
            int max_length = 0;
            for (int i = 0; i < MAX_VERTEX_ATTRIBS; i++) {
                if (p->active_attributes[i] < 0) continue;
                OgxAttributeVar *attr = get_attr_variable_for_location(p, i);
                size_t len = strlen(attr->name);
                if (len > max_length) max_length = len;
            }
            *params = max_length;
        }
        break;
    case GL_ACTIVE_UNIFORMS:
        *params = p->uniform_count;
        break;
    case GL_ACTIVE_UNIFORM_MAX_LENGTH:
        {
            int max_length = 0;
            for (int i = 0; i < p->uniform_count; i++) {
                OgxUniformVar *v = p->uniforms[i];
                size_t len = strlen(v->name);
                if (len > max_length) max_length = len;
            }
            *params = max_length;
        }
        break;
    case GL_ATTACHED_SHADERS:
        {
            int count = 0;
            OgxShader *s = NULL;
            while (s = program_get_next_shader(p, s)) count++;
            *params = count;
        }
        break;
    case GL_DELETE_STATUS:
        *params = p->deletion_requested;
        break;
    case GL_INFO_LOG_LENGTH:
        *params = 0;
        break;
    case GL_LINK_STATUS:
    case GL_VALIDATE_STATUS:
        *params = p->linked_ok;
        break;
    }
}

void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog)
{
    infoLog[0] = '\0';
    if (length) *length = 0;
}

void glGetShaderiv(GLuint shader, GLenum pname, GLint *params)
{
    OgxShader *s = SHADER_FROM_INT(shader);

    if (!s) {
        set_error(GL_INVALID_VALUE);
        return;
    }

    switch (pname) {
    case GL_COMPILE_STATUS:
        *params = s->compiled; break;
    case GL_DELETE_STATUS:
        *params = s->deletion_requested; break;
    case GL_SHADER_SOURCE_LENGTH:
        *params = s->source_length; break;
    case GL_SHADER_TYPE:
        *params = s->type; break;
    }
}

void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog)
{
    infoLog[0] = '\0';
    if (length) *length = 0;
}

void glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *source)
{
    OgxShader *s = SHADER_FROM_INT(shader);
    if (!s) {
        set_error(GL_INVALID_VALUE);
        *length = 0;
        return;
    }

    /* We should return the shader code that was passed to glShaderSource(),
     * but we don't want to waste memory to store it. So, here we return the
     * hash we computed, so that the application developer can use it to tell
     * the shaders apart. */
    *length = snprintf(source, bufSize, "0x%08x", s->source_hash);
}

GLint glGetUniformLocation(GLuint program, const GLchar *name)
{
    OgxProgram *p = PROGRAM_FROM_INT(program);
    if (!p) {
        set_error(GL_INVALID_OPERATION);
        return -1;
    }

    OgxShader *s = NULL;
    while (s = program_get_next_shader(p, s)) {
        for (int i = 0; i < s->uniform_count; i++) {
            OgxUniformVar *v = &s->uniforms[i];
            if (strcmp(v->name, name) == 0) return v->location;
        }
    }
    return -1;
}

void glGetVertexAttribdv(GLuint index, GLenum pname, GLdouble *params)
{
    float p[4];
    int num_params = 1;
    switch (pname) {
    case GL_CURRENT_VERTEX_ATTRIB: num_params = 4; break;
    }
    glGetVertexAttribfv(index, pname, p);
    for (int i = 0; i < num_params; i++) {
        params[i] = p[i];
    }
}

void glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params)
{
    if (index >= MAX_VERTEX_ATTRIBS) {
        set_error(GL_INVALID_VALUE);
        return;
    }

    switch (pname) {
    case GL_CURRENT_VERTEX_ATTRIB:
        floatcpy(params, _ogx_shader_state.vertex_attrib_data[index], 4);
        return;
    }
}

void glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params)
{
    OgxVertexAttribState *v = get_vertex_attrib(index);
    if (!v) return;

    switch (pname) {
    case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
        *params = v->array_enabled;
        return;
    case GL_VERTEX_ATTRIB_ARRAY_SIZE:
        *params = v->array.size;
        return;
    case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
        *params = v->array.stride;
        return;
    case GL_VERTEX_ATTRIB_ARRAY_TYPE:
        *params = v->array.type;
        return;
    case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
        *params = v->array.normalized;
        return;
    }
}

void glGetVertexAttribPointerv(GLuint index, GLenum pname, void **pointer)
{
    OgxVertexAttribState *v = get_vertex_attrib(index);
    if (!v) return;

    switch (pname) {
    case GL_VERTEX_ATTRIB_ARRAY_POINTER:
        *pointer = (void*)v->array.pointer;
    default:
        set_error(GL_INVALID_ENUM);
        return;
    }
}

GLboolean glIsProgram(GLuint program)
{
    OgxProgram *p = _ogx_shader_state.programs;
    while (p) {
        if (p == PROGRAM_FROM_INT(program)) return GL_TRUE;
        p = p->next;
    }
    return GL_FALSE;
}

GLboolean glIsShader(GLuint shader)
{
    OgxShader *s = _ogx_shader_state.shaders;
    while (s) {
        if (s == SHADER_FROM_INT(shader)) return GL_TRUE;
        s = s->next;
    }
    return GL_FALSE;
}

void glLinkProgram(GLuint program)
{
    OgxProgram *p = PROGRAM_FROM_INT(program);
    if (!p) {
        set_error(GL_INVALID_OPERATION);
        return;
    }

    OgxShader *s = NULL;
    int attribute_count = 0, uniform_count = 0;
    while (s = program_get_next_shader(p, s)) {
        attribute_count += s->attribute_count;
        uniform_count += s->uniform_count;
    }
    /* 2 is for the two NULL terminators */
    void **ptr_list = calloc(attribute_count + uniform_count + 2,
                             sizeof(void*));
    OgxAttributeVar **attr_list = (void*)ptr_list;
    OgxUniformVar **uniform_list = (void*)(ptr_list + attribute_count + 1);

    /* Count the number of variables that we need to allocate (excluding
     * duplicates) */
    OgxUniformLocation uniform_location = 0;
    size_t uniform_data_size = 0;
    uint32_t attr_location_slots = 0; /* bitmask of the allocated locations */
    int num_normals = 0, num_colors = 0, num_texcoords = 0;
    memset(p->active_attributes, -1, sizeof(p->active_attributes));
    attribute_count = 0, uniform_count = 0;
    s = NULL;
    while (s = program_get_next_shader(p, s)) {
        for (int i = 0; i < s->uniform_count; i++) {
            OgxUniformVar *v = &s->uniforms[i];
            if (!add_uniform_to_list(uniform_list, v)) continue;
            uniform_count++;
            uniform_data_size += uniform_data_struct_size(v->type) * v->size;
            v->location = uniform_location;
            uniform_location += v->size;
        }

        /* Find and setup all active attributes */
        for (int i = 0; i < s->attribute_count; i++) {
            OgxAttributeVar *v = &s->attributes[i];
            if (!add_attribute_to_list(attr_list, v)) continue;
            attribute_count++;
            OgxAttrLocation bound_location =
                get_attribute_bound_location(p, v->name);
            if (bound_location < MAX_VERTEX_ATTRIBS) {
                /* if glBindAttribLocation was called, assign the attribute to
                 * that location; otherwise, the attribute will be allocated to
                 * the first available location, in a loop below. */
                v->location = bound_location;
                int used_slots = num_locations_for_type(v->type);
                for (int l = 0; l < used_slots; l++)
                    attr_location_slots |= 1 << (bound_location + l);
            } else {
                v->location = MAX_VERTEX_ATTRIBS; /* invalid value */
            }

            /* Count the GX attributes being used (we assume that the client
             * knows the limitations of GX and won't try to send more
             * attributes than what is supported) */
            switch (v->gx_attribute) {
            case GX_VA_NRM: num_normals++; break;
            case GX_VA_CLR0: num_colors++; break;
            case GX_VA_TEX0: num_texcoords++; break;
            }
        }
    }

    p->attribute_count = attribute_count;
    p->uniform_count = uniform_count;
    p->uniform_location_count = uniform_location;

    size_t attribute_list_size = sizeof(void*) * (attribute_count + 1);
    size_t uniform_list_size = sizeof(void*) * (uniform_count + 1);
    size_t uniform_location_offset_list_size =
        sizeof(p->uniform_location_offsets[0]) * p->uniform_location_count;
    size_t total_data_size =
        uniform_data_size +
        attribute_list_size +
        uniform_list_size +
        uniform_location_offset_list_size;
    /* Allocate room for the variables */
    p->uniform_data_base = realloc(p->uniform_data_base, total_data_size);
    p->attributes = (void*)p->uniform_data_base + uniform_data_size;
    p->uniforms = (void*)p->attributes + attribute_list_size;
    p->uniform_location_offsets = (void*)p->uniforms + uniform_list_size;

    /* copy the pointers from the temporary list and free it */
    memcpy(p->attributes, attr_list, attribute_list_size);
    memcpy(p->uniforms, uniform_list, uniform_list_size);
    free(ptr_list);

    /* Clear the uniform data and compute the location offsets */
    memset(p->uniform_data_base, 0, sizeof(uniform_data_size));
    size_t current_offset = 0;
    uniform_location = 0;
    for (int i = 0; i < p->uniform_count; i++) {
        OgxUniformVar *v = p->uniforms[i];
        size_t data_size = uniform_data_struct_size(v->type);
        for (int index = 0; index < v->size; index++) {
            p->uniform_location_offsets[uniform_location] = current_offset;
            OgxUniformData *data = p->uniform_data_base + current_offset;
            data->uniform = v;
            uniform_location++;
            current_offset += data_size;
        }
    }

    /* Go through all the attributes and assign a default location to all those
     * which don't have one. */
    int num_active_attributes = 0;
    int location_iter = 0;
    bool colors_allocated = 0, texcoord_allocated = 0;
    for (int i = 0; i < p->attribute_count; i++) {
        OgxAttributeVar *v = p->attributes[i];
        if (v->location >= MAX_VERTEX_ATTRIBS) {
            while (location_iter < MAX_VERTEX_ATTRIBS &&
                   attr_location_slots & (1 << location_iter)) {
                location_iter++;
            }
            if (location_iter < MAX_VERTEX_ATTRIBS) {
                v->location = location_iter;
                location_iter += num_locations_for_type(v->type);
            }
        }

        /* Also, map the attribute location to its index in the OgxShaderState
         * structure. The total number of the attributes was computed in the
         * loop above, so here we just have to fill the slots. */
        int target = -1;
        switch (v->gx_attribute) {
        case GX_VA_POS: target = 0; break;
        case GX_VA_NRM: target = 1; break;
        case GX_VA_CLR0: target = 1 + num_normals + colors_allocated++; break;
        case GX_VA_TEX0: target = 1 + num_normals + num_colors + texcoord_allocated++; break;
        }
        if (target >= 0) {
            p->active_attributes[v->location] = i;
            p->locations_sorted_gx[target] = v->location;
            num_active_attributes++;
        }
    }
    /* Set to -1 all remaining active attribute slots */
    for (int i = num_active_attributes; i < MAX_VERTEX_ATTRIBS; i++) {
        p->locations_sorted_gx[i] = -1;
    }

    p->linked = true;
    p->linked_ok = true;
    if (s_processor->link_program) {
        GLenum error = s_processor->link_program(program);
        if (error != GL_NO_ERROR) {
            set_error(error);
            p->linked_ok = false;
        }
    }
}

void glShaderSource(GLuint shader, GLsizei count,
                    const GLchar *const*string, const GLint *length)
{
    OgxShader *s = SHADER_FROM_INT(shader);

    if (!s) {
        set_error(GL_INVALID_VALUE);
        return;
    }

    uint32_t hash = 0;
    int total_length;
    for (int i = 0; i < count; i++) {
        int len = length ? length[i] : strlen(string[i]);
        int h;
        MurmurHash3_x86_32(string[i], len, 0, &h);
        total_length += len;
        hash += h;
    }
    s->source_length = total_length;
    s->source_hash = hash;

    if (s_processor->shader_source)
        s_processor->shader_source(shader, count, string, length);
}

void glUseProgram(GLuint program)
{
    if (program == glparamstate.current_program) return;

    /* TODO: applications are free to modify the shaders after this call, and
     * we should make it so that these changes do not affect the rendering,
     * until glLinkProgram() is called again. */
    debug(OGX_LOG_SHADER, "activating program %x", program);
    OgxProgram *old = PROGRAM_FROM_INT(glparamstate.current_program);
    glparamstate.current_program = program;
    glparamstate.dirty.bits.dirty_attributes = 1;

    if (old && old->deletion_requested) {
        glDeleteProgram(PROGRAM_TO_INT(old));
    }
}

void glValidateProgram(GLuint program)
{
}

void glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                           GLboolean normalized, GLsizei stride,
                           const void *pointer)
{
    if (glparamstate.compat_profile && index == 0) {
        glVertexPointer(size, type, stride, pointer);
        return;
    }

    OgxVertexAttribState *v = &_ogx_shader_state.vertex_attribs[index];
    if (!v) return;

    v->array.size = size;
    v->array.type = type;
    v->array.normalized = normalized;
    assert(stride < (1 << (8 * sizeof(v->array.stride))));
    v->array.stride = stride;
    v->array.pointer = pointer;
    glparamstate.dirty.bits.dirty_attributes = 1;
}

void _ogx_shader_initialize()
{
    for (int i = 0; i < MAX_VERTEX_ATTRIBS; i++) {
        /* the attributes should be initialized to [0, 0, 0, 1], but since we
         * know that the data is 0-initialized, we only explicitly initialize
         * the fourth component here. */
        _ogx_shader_state.vertex_attrib_data[i][3] = 1.0f;
    }
}

void ogx_shader_register_program_processor(const OgxProgramProcessor *processor)
{
    s_processor = processor;
}

uint32_t ogx_shader_get_source_hash(GLuint shader)
{
    OgxShader *s = SHADER_FROM_INT(shader);
    return s->source_hash;
}

void ogx_shader_add_uniforms(GLuint shader, int count, ...)
{
    OgxShader *s = SHADER_FROM_INT(shader);
    va_list args;
    va_start(args, count);
    int new_count = s->uniform_count + count;
    s->uniforms = realloc(s->uniforms, sizeof(OgxUniformVar) * new_count);
    OgxUniformVar *dest = s->uniforms + s->uniform_count;

    for (int i = 0; i < count; i++) {
        dest->name = va_arg(args, char *);
        uint32_t type = va_arg(args, uint32_t);
        dest->type = type & 0xffff;
        dest->location = -1;
        dest->size = 1;
        dest++;
    }
    s->uniform_count = new_count;
    va_end(args);
}

void ogx_shader_add_attributes(GLuint shader, int count, ...)
{
    OgxShader *s = SHADER_FROM_INT(shader);
    va_list args;
    va_start(args, count);
    int new_count = s->attribute_count + count;
    s->attributes = realloc(s->attributes, sizeof(OgxAttributeVar) * new_count);
    OgxAttributeVar *dest = s->attributes + s->attribute_count;

    for (int i = 0; i < count; i++) {
        dest->name = va_arg(args, char *);
        uint32_t type = va_arg(args, uint32_t);
        dest->type = type & 0xffff;
        dest->location = MAX_VERTEX_ATTRIBS; /* invalid value */
        dest->size = 1;
        dest->gx_attribute = va_arg(args, int);
        dest++;
    }
    s->attribute_count = new_count;
    va_end(args);
}

void ogx_shader_program_set_user_data(GLuint program, void *data,
                                      OgxCleanupCb cleanup)
{
    OgxProgram *p = PROGRAM_FROM_INT(program);
    p->user_data = data;
    p->cleanup_user_data_cb = cleanup;
}

void ogx_shader_program_set_setup_draw_cb(GLuint program,
                                          OgxSetupDrawCb setup_draw)
{
    OgxProgram *p = PROGRAM_FROM_INT(program);
    p->setup_draw_cb = setup_draw;
}

void *ogx_shader_get_data(GLuint shader)
{
    OgxShader *s = SHADER_FROM_INT(shader);
    return s->user_data;
}

void _ogx_shader_setup_draw(const OgxDrawData *draw_data)
{
    OgxProgram *p = PROGRAM_FROM_INT(glparamstate.current_program);

    if (p->setup_draw_cb) {
        p->setup_draw_cb(PROGRAM_TO_INT(p), draw_data, p->user_data);
    }

    _ogx_arrays_setup_draw(draw_data, OGX_DRAW_FLAG_NONE);
}

void _ogx_shader_update_vertex_array_readers(OgxDrawMode mode)
{
    OgxProgram *p = PROGRAM_FROM_INT(glparamstate.current_program);
    for (int i = 0; i < MAX_VERTEX_ATTRIBS; i++) {
        OgxAttrLocation index = p->locations_sorted_gx[i];
        if (index >= MAX_VERTEX_ATTRIBS) break;

        OgxAttributeVar *v = get_attr_variable_for_location(p, index);
        OgxVertexAttribState *attr = &_ogx_shader_state.vertex_attribs[index];

        if (attr->array_enabled) {
            _ogx_array_add(v->gx_attribute, &attr->array);
        } else {
            /* TODO: add an example to test this */
            int size;
            switch (v->type) {
            case GL_FLOAT: size = 1; break;
            case GL_FLOAT_VEC2: size = 2; break;
            case GL_FLOAT_VEC3: size = 3; break;
            case GL_FLOAT_VEC4: size = 4; break;
            /* TODO: support more types */
            default:
                warning("Unimplemented shader attr type %04x\n", v->type);
                continue;
            }

            _ogx_array_add_constant_fv(v->gx_attribute, size,
                _ogx_shader_state.vertex_attrib_data[index]);
        }
    }
}

#define INCLUDED_FROM_SHADER_C
#include "shader_functions.h"
