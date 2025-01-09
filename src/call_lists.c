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

#include "call_lists.h"
#include "debug.h"
#include "efb.h"
#include "gpu_resources.h"
#include "opengx.h"
#include "stencil.h"
#include "utils.h"

#include <GL/gl.h>
#include <assert.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdlib.h>

#define MAX_COMMANDS_PER_BUFFER 4
#define MAX_CALL_LISTS 1536
/* The glut teapot can take more than 300KB, for reference */
#define MAX_GXLIST_SIZE (1024 * 1024)
#define CALL_LIST_START_ID 1

typedef struct
{
    CommandType type;
    union {
        GLuint gllist; // glCallList

        GLenum cap; // glEnable, glDisable

        GLenum mode; // glBegin, glFrontFace

        struct LightParams {
            uint16_t light;
            uint16_t pname;
            GLfloat params[4];
        } light;

        struct MaterialParams {
            uint16_t face;
            uint16_t pname;
            GLfloat params[4];
        } material;

        struct {
            GLenum sfactor;
            GLenum dfactor;
        } blend_func;

        struct BoundTexture {
            GLenum target;
            GLuint texture;
        } bound_texture;

        struct TexEnv {
            GLenum target;
            GLenum pname;
            GLint param;
        } tex_env;

        struct {
            GLfloat x;
            GLfloat y;
            GLfloat z;
        } xyz;

        struct {
            GLfloat angle;
            GLfloat x;
            GLfloat y;
            GLfloat z;
        } rotate;

        struct DrawGeometry {
            GLenum mode;
            uint16_t count;
            union client_state cs;
            u32 list_size;
            void *gxlist;
        } draw_geometry;

        float color[4];

        float normal[3];

        float matrix[16];
    } c;
} Command;

typedef struct CommandBuffer
{
    Command commands[MAX_COMMANDS_PER_BUFFER];
    struct CommandBuffer *next;
} CommandBuffer;

typedef struct
{
    CommandBuffer *head;
} CallList;

static CallList call_lists[MAX_CALL_LISTS];
static GXColor s_current_color;
static float s_current_normal[3];
static bool s_last_draw_used_indexed_data = false;
static uint16_t s_last_draw_sync_token = 0;
static union client_state s_last_client_state;
static bool s_last_client_state_is_valid = false;

#define BUFFER_IS_VALID(buffer) (((uint32_t)buffer) > 1)
#define LIST_IS_USED(index) BUFFER_IS_VALID(call_lists[index].head)
#define LIST_IS_RESERVED_OR_USED(index) (call_lists[index].head != NULL)
#define LIST_RESERVE(index) call_lists[index].head = (void*)1
#define LIST_UNRESERVE(index) call_lists[index].head = NULL

static inline int last_command(CommandBuffer **buffer)
{
    CommandBuffer *next;

    while (BUFFER_IS_VALID(*buffer)) {
        next = (*buffer)->next;
        if (!next) {
            for (int i = 0; i < MAX_COMMANDS_PER_BUFFER; i++) {
                Command *curr = &(*buffer)->commands[i];
                if (curr->type == COMMAND_NONE) {
                    return i - 1;
                }
            }
            return MAX_COMMANDS_PER_BUFFER - 1;
        }

        *buffer = next;
    }

    return -1;
}

static Command *new_command(CommandBuffer **head)
{
    int last_index = -1;
    CommandBuffer *last_buffer = NULL;

    if (BUFFER_IS_VALID(*head)) {
        last_buffer = *head;
        last_index = last_command(&last_buffer);
    }

    if (last_index < 0 || last_index == MAX_COMMANDS_PER_BUFFER - 1) {
        CommandBuffer *new_buffer = malloc(sizeof(CommandBuffer));
        if (!new_buffer) {
            warning("Failed to allocate memory for call-list buffer (%d)", errno);
            return NULL;
        }

        for (int i = 0; i < MAX_COMMANDS_PER_BUFFER; i++) {
            new_buffer->commands[i].type = COMMAND_NONE;
        }
        new_buffer->next = NULL;

        if (last_buffer) {
            last_buffer->next = new_buffer;
        } else {
            *head = new_buffer;
        }
        return &new_buffer->commands[0];
    } else {
        return &last_buffer->commands[last_index + 1];
    }
}

static void setup_draw_geometry(struct DrawGeometry *dg,
                                bool uses_indexed_data)
{
    u8 vtxindex = GX_VTXFMT0;
    GXColor current_color;

    if (uses_indexed_data && s_last_draw_used_indexed_data) {
        bool data_changed = false;
        /* If the indexed data has changed, we need to wait until the previous
         * list has completed its execution, because changing the data under
         * its feet will cause rendering issues. */
        if (!dg->cs.color_enabled) {
            current_color = gxcol_new_fv(glparamstate.imm_mode.current_color);
            if (!gxcol_equal(current_color, s_current_color)) data_changed = true;
        }

        if (!dg->cs.normal_enabled) {
            if (memcmp(s_current_normal, glparamstate.imm_mode.current_normal,
                       sizeof(s_current_normal)) != 0)
                data_changed = true;
        }

        if (data_changed) {
            // Wait
            while (GX_GetDrawSync() < s_last_draw_sync_token);
        }
    }

    OgxDrawMode gxmode = _ogx_draw_mode(dg->mode);
    OgxDrawData draw_data = {
        gxmode,
        dg->count,
        /* The remaining fields are not used when drawing through lists */
    };
    _ogx_arrays_setup_draw(&draw_data,
                           dg->cs.normal_enabled,
                           dg->cs.color_enabled ? 2 : 0,
                           dg->cs.texcoord_enabled);
    if (!dg->cs.normal_enabled) {
        GX_SetVtxDesc(GX_VA_NRM, GX_INDEX8);
        GX_SetArray(GX_VA_NRM, s_current_normal, 12);
        floatcpy(s_current_normal, glparamstate.imm_mode.current_normal, 3);
        /* Not needed on Dolphin, but it is on a Wii */
        DCStoreRange(s_current_normal, 12);
    }
    if (!dg->cs.color_enabled) {
        GX_SetVtxDesc(GX_VA_CLR0, GX_INDEX8);
        GX_SetVtxDesc(GX_VA_CLR1, GX_INDEX8);
        s_current_color = current_color;
        GX_SetArray(GX_VA_CLR0, &s_current_color, 4);
        GX_SetArray(GX_VA_CLR1, &s_current_color, 4);
        DCStoreRange(&s_current_color, 4);
    }

    /* It makes no sense to use a fixed texture coordinates for all vertices,
     * so we won't add them unless they are enabled. */

    GX_InvVtxCache();
}

static void execute_draw_geometry_list(struct DrawGeometry *dg)
{
    bool uses_indexed_data = !dg->cs.normal_enabled || !dg->cs.color_enabled;
    if (!s_last_client_state_is_valid ||
        s_last_client_state.as_int != dg->cs.as_int) {
        setup_draw_geometry(dg, uses_indexed_data);
        s_last_client_state = dg->cs;
        s_last_client_state_is_valid = true;
    }

    GX_CallDispList(dg->gxlist, dg->list_size);

    if (uses_indexed_data) {
        s_last_draw_sync_token = send_draw_sync_token();
        s_last_draw_used_indexed_data = true;
    } else {
        s_last_draw_used_indexed_data = false;
    }
}

static void flat_draw_geometry(void *cb_data)
{
    struct DrawGeometry *dg = cb_data;
    execute_draw_geometry_list(dg);
}

static void run_draw_geometry(struct DrawGeometry *dg)
{
    union client_state cs;

    /* Update the drawing mode on the list. This required peeping into
     * GX_Begin() code. */
    OgxDrawMode gxmode = _ogx_draw_mode(dg->mode);
    u8 *fifo_ptr = dg->gxlist;
    u8 mode_opcode = gxmode.mode | (GX_VTXFMT0 & 0x7);
    if (*fifo_ptr != mode_opcode) {
        /* Before altering the list, we need to make sure that it's not in use
         * by the GP.
         * TODO: find a better criterium, to minimize waits */
        GX_DrawDone();
        *fifo_ptr = mode_opcode;
        DCStoreRange(fifo_ptr, 32); // min size is 32
    }

    _ogx_efb_set_content_type(OGX_EFB_SCENE);

    _ogx_gpu_resources_push();
    cs = glparamstate.cs;
    glparamstate.cs = dg->cs;
    _ogx_apply_state();
    _ogx_setup_render_stages();
    glparamstate.cs = cs;

    execute_draw_geometry_list(dg);
    _ogx_gpu_resources_pop();

    glparamstate.draw_count++;

    if (glparamstate.stencil.enabled) {
        s_last_client_state_is_valid = false;
        _ogx_gpu_resources_push();
        _ogx_stencil_draw(flat_draw_geometry, dg);
        _ogx_gpu_resources_pop();
        s_last_client_state_is_valid = false;
    }
}

static void run_command(Command *cmd)
{
    switch (cmd->type) {
    case COMMAND_DRAW_ARRAYS:
        run_draw_geometry(&cmd->c.draw_geometry);
        break;
    case COMMAND_DRAW_ELEMENTS:
        run_draw_geometry(&cmd->c.draw_geometry);
        break;
    case COMMAND_CALL_LIST:
        glCallList(cmd->c.gllist);
        break;
    case COMMAND_ENABLE:
        glEnable(cmd->c.cap);
        break;
    case COMMAND_DISABLE:
        glDisable(cmd->c.cap);
        break;
    case COMMAND_LIGHT:
        glLightfv(cmd->c.light.light, cmd->c.light.pname, cmd->c.light.params);
        break;
    case COMMAND_MATERIAL:
        glMaterialfv(cmd->c.material.face, cmd->c.material.pname,
                     cmd->c.material.params);
        break;
    case COMMAND_BLEND_FUNC:
        glBlendFunc(cmd->c.blend_func.sfactor, cmd->c.blend_func.dfactor);
        break;
    case COMMAND_BIND_TEXTURE:
        glBindTexture(cmd->c.bound_texture.target,
                      cmd->c.bound_texture.texture);
        break;
    case COMMAND_TEX_ENV:
        glTexEnvi(cmd->c.tex_env.target,
                  cmd->c.tex_env.pname,
                  cmd->c.tex_env.param);
        break;
    case COMMAND_LOAD_IDENTITY:
        glLoadIdentity();
        break;
    case COMMAND_PUSH_MATRIX:
        glPushMatrix();
        break;
    case COMMAND_POP_MATRIX:
        glPopMatrix();
        break;
    case COMMAND_MULT_MATRIX:
        glMultMatrixf(cmd->c.matrix);
        break;
    case COMMAND_TRANSLATE:
        glTranslatef(cmd->c.xyz.x, cmd->c.xyz.y, cmd->c.xyz.z);
        break;
    case COMMAND_ROTATE:
        glRotatef(cmd->c.rotate.angle,
                  cmd->c.rotate.x, cmd->c.rotate.y, cmd->c.rotate.z);
        break;
    case COMMAND_SCALE:
        glScalef(cmd->c.xyz.x, cmd->c.xyz.y, cmd->c.xyz.z);
        break;
    case COMMAND_FRONT_FACE:
        glFrontFace(cmd->c.mode);
        break;
    case COMMAND_COLOR:
        glColor4fv(cmd->c.color);
        break;
    case COMMAND_NORMAL:
        glNormal3fv(cmd->c.normal);
        break;
    }
}

typedef int (*IndexCallback)(int i, void *index_data);

static int draw_array_index_cb(int i, void *index_data)
{
    int first = *(int*)index_data;
    return i + first;
}

typedef struct {
    GLenum type;
    const GLvoid *indices;
} DrawElementsIndexData;

static int draw_elements_index_cb(int i, void *index_data)
{
    const DrawElementsIndexData *id = (DrawElementsIndexData*)index_data;
    return read_index(id->indices, id->type, i);
}

static void queue_draw_geometry(struct DrawGeometry *dg,
                                GLenum mode, GLsizei count,
                                IndexCallback index_cb,
                                void *index_data)
{
    /* When executing a display list containing glDrawElements() or
     * glDrawArrays() all the attributes that were not enabled at the time of
     * the list creation should be taken from the current active attribute
     * (color, normals and texture coordinates). Since we are not be able to
     * modify a GX list to add more attributes, we'll add them now as indexed
     * attributes: this will allow us to set the value of the indexed attribute
     * at the time when the list is executed. */
    dg->mode = mode;
    dg->gxlist = memalign(32, MAX_GXLIST_SIZE);
    DCInvalidateRange(dg->gxlist, MAX_GXLIST_SIZE);
    dg->cs = glparamstate.cs;
    OgxDrawMode gxmode = _ogx_draw_mode(mode);
    dg->count = count + gxmode.loop;

    if (glparamstate.dirty.bits.dirty_attributes)
        _ogx_update_vertex_array_readers();

    if (dg->cs.color_enabled) {
        _ogx_array_reader_enable_dup_color(&glparamstate.color_reader, true);
    }

    GX_BeginDispList(dg->gxlist, MAX_GXLIST_SIZE);

    /* Note that the drawing mode set here will be overwritten when executing the list */

    GX_Begin(gxmode.mode, GX_VTXFMT0, dg->count);
    for (int i = 0; i < dg->count; i++) {
        int index = index_cb(i % count, index_data);
        float value[4];
        _ogx_array_reader_process_element(&glparamstate.vertex_reader, index);

        if (dg->cs.normal_enabled) {
            _ogx_array_reader_process_element(&glparamstate.normal_reader, index);
        } else {
            GX_Normal1x8(0);
        }

        if (dg->cs.color_enabled) {
            _ogx_array_reader_process_element(&glparamstate.color_reader, index);
        } else {
            GX_Color1x8(0); // CLR0
            GX_Color1x8(0); // CLR1
        }

        for (int tex = 0; tex < MAX_TEXTURE_UNITS; tex++) {
            if (dg->cs.texcoord_enabled & (1 << tex)) {
                _ogx_array_reader_process_element(
                    &glparamstate.texcoord_reader[tex], index);
            }
        }
    }
    GX_End();

    u32 size = GX_EndDispList();
    fprintf(stderr, "Created draw list %u\n", size);
    /* Free any excess memory */
    void *new_ptr = realloc(dg->gxlist, size);
    assert(new_ptr == dg->gxlist);
    dg->list_size = size;
}

static void queue_draw_arrays(struct DrawGeometry *dg,
                              GLenum mode, GLint first, GLsizei count)
{
    queue_draw_geometry(dg, mode, count,
                        draw_array_index_cb, &first);
}

static void queue_draw_elements(struct DrawGeometry *dg,
                                GLenum mode, GLsizei count, GLenum type,
                                const GLvoid *indices)
{
    DrawElementsIndexData id = { type, indices };
    queue_draw_geometry(dg, mode, count,
                        draw_elements_index_cb, &id);
}

static void destroy_buffer(CommandBuffer *buffer)
{
    if (buffer->next) {
        destroy_buffer(buffer->next);
    }

    for (int i =0; i < MAX_COMMANDS_PER_BUFFER; i++) {
        Command *command = &buffer->commands[i];
        if (command->type == COMMAND_NONE) break;

        /* Free the memory for those commands who allocated it */
        if (command->type == COMMAND_DRAW_ELEMENTS ||
            command->type == COMMAND_DRAW_ARRAYS) {
            free(command->c.draw_geometry.gxlist);
        }
    }
    free(buffer);
}

static void destroy_list(int index)
{
    CallList *list = &call_lists[index];
    if (!LIST_IS_RESERVED_OR_USED(index)) return;

    if (BUFFER_IS_VALID(list->head)) {
        destroy_buffer(list->head);
    }
    list->head = NULL;
}

/* This function returns true if the caller's code needs to be executed now,
 * false if it can immediately return with no further action.
 *
 * For operations that we store in GX lists we do return true, because we want
 * the caller to perform the operation as usual (the only difference will be
 * that GX will not really execute it, but store it in the GX display list).
 */
bool _ogx_call_list_append(CommandType op, ...)
{
    CallList *list = &call_lists[glparamstate.current_call_list.index];
    CommandBuffer *buffer = list->head;
    Command *command;
    va_list ap;
    int count;

    debug(OGX_LOG_CALL_LISTS, "Adding command %d to list %d",
          op, glparamstate.current_call_list.index);

    command = new_command(&list->head);
    command->type = op;
    va_start(ap, op);
    switch (op) {
    case COMMAND_CALL_LIST:
        command->c.gllist = va_arg(ap, GLuint);
        break;
    case COMMAND_DRAW_ARRAYS:
        {
            GLenum mode = va_arg(ap, GLenum);
            GLint first = va_arg(ap, GLint);
            GLsizei count = va_arg(ap, GLsizei);
            queue_draw_arrays(&command->c.draw_geometry,
                              mode, first, count);
        }
        break;
    case COMMAND_DRAW_ELEMENTS:
        {
            GLenum mode = va_arg(ap, GLenum);
            GLsizei count = va_arg(ap, GLsizei);
            GLenum type = va_arg(ap, GLenum);
            const GLvoid *indices = va_arg(ap, GLvoid *);
            queue_draw_elements(&command->c.draw_geometry,
                                mode, count, type, indices);
        }
        break;
    case COMMAND_ENABLE:
    case COMMAND_DISABLE:
        command->c.cap = va_arg(ap, GLenum);
        break;
    case COMMAND_LIGHT:
        command->c.light.light = va_arg(ap, GLenum);
        command->c.light.pname = va_arg(ap, GLenum);
        switch (command->c.light.pname) {
        case GL_CONSTANT_ATTENUATION:
        case GL_LINEAR_ATTENUATION:
        case GL_QUADRATIC_ATTENUATION:
        case GL_SPOT_CUTOFF:
        case GL_SPOT_EXPONENT:
            count = 1;
            break;
        case GL_SPOT_DIRECTION:
            count = 3;
            break;
        case GL_POSITION:
        case GL_DIFFUSE:
        case GL_AMBIENT:
        case GL_SPECULAR:
            count = 4;
            break;
        }
        floatcpy(command->c.light.params, va_arg(ap, GLfloat *), count);
        break;
    case COMMAND_MATERIAL:
        command->c.material.face = va_arg(ap, GLenum);
        command->c.material.pname = va_arg(ap, GLenum);
        switch (command->c.material.pname) {
        case GL_SHININESS:
            count = 1;
            break;
        default:
            count = 4;
            break;
        }
        floatcpy(command->c.material.params, va_arg(ap, GLfloat *), count);
        break;
    case COMMAND_BLEND_FUNC:
        command->c.blend_func.sfactor = va_arg(ap, GLenum);
        command->c.blend_func.dfactor = va_arg(ap, GLenum);
        break;
    case COMMAND_BIND_TEXTURE:
        command->c.bound_texture.target = va_arg(ap, GLenum);
        command->c.bound_texture.texture = va_arg(ap, GLuint);
        break;
    case COMMAND_TEX_ENV:
        command->c.tex_env.target = va_arg(ap, GLenum);
        command->c.tex_env.pname = va_arg(ap, GLenum);
        command->c.tex_env.param = va_arg(ap, GLint);
        break;
    case COMMAND_MULT_MATRIX:
        floatcpy(command->c.matrix, va_arg(ap, GLfloat *), 16);
        break;
    case COMMAND_TRANSLATE:
    case COMMAND_SCALE:
        command->c.xyz.x = va_arg(ap, double);
        command->c.xyz.y = va_arg(ap, double);
        command->c.xyz.z = va_arg(ap, double);
        break;
    case COMMAND_ROTATE:
        command->c.rotate.angle = va_arg(ap, double);
        command->c.rotate.x = va_arg(ap, double);
        command->c.rotate.y = va_arg(ap, double);
        command->c.rotate.z = va_arg(ap, double);
        break;
    case COMMAND_FRONT_FACE:
        command->c.mode = va_arg(ap, GLenum);
        break;
    case COMMAND_COLOR:
        floatcpy(command->c.color, va_arg(ap, GLfloat *), 4);
        break;
    case COMMAND_NORMAL:
        floatcpy(command->c.normal, va_arg(ap, GLfloat *), 3);
        break;
    }
    va_end(ap);
    return glparamstate.current_call_list.must_execute;
}

GLboolean glIsList(GLuint list)
{
    if (list < CALL_LIST_START_ID) {
        return GL_FALSE;
    }

    list -= CALL_LIST_START_ID;
    if (list >= MAX_CALL_LISTS) {
        return GL_FALSE;
    }

    return LIST_IS_USED(list);
}

void glDeleteLists(GLuint list, GLsizei range)
{
    if (glparamstate.current_call_list.index != -1) {
        set_error(GL_INVALID_OPERATION);
        return;
    }

    if (range < 0) {
        set_error(GL_INVALID_VALUE);
        return;
    }

    for (int i = 0; i < range; i++) {
        int index = list - CALL_LIST_START_ID;
        if (index < 0 || index >= MAX_CALL_LISTS) {
            /* Note that OpenGL does not specify an error in this case */
            break;
        }

        destroy_list(index);
    }
}

GLuint glGenLists(GLsizei range)
{
    int remaining = range;

    for (int i = 0; i < MAX_CALL_LISTS && remaining > 0; i++) {
        if (!LIST_IS_RESERVED_OR_USED(i)) {
            remaining--;
            if (remaining == 0) {
                /* We found a contiguous range available. Reserve them*/
                int first = i - range + 1;
                for (int j = first; j < first + range; j++)
                    LIST_RESERVE(j);
                return first + CALL_LIST_START_ID;
            }
        } else {
            remaining = range;
        }
    }

    if (remaining > 0) {
        warning("Could not allocate %d display lists", remaining);
        set_error(GL_OUT_OF_MEMORY);
    }

    return 0;
}

void glNewList(GLuint list, GLenum mode)
{
    if (list < CALL_LIST_START_ID) {
        set_error(GL_INVALID_VALUE);
        return;
    }

    list -= CALL_LIST_START_ID;
    if (list >= MAX_CALL_LISTS) {
        set_error(GL_INVALID_VALUE);
        return;
    }

    if (glparamstate.current_call_list.index != -1) {
        set_error(GL_INVALID_OPERATION);
        return;
    }

    glparamstate.current_call_list.index = list;
    glparamstate.current_call_list.must_execute = (mode == GL_COMPILE_AND_EXECUTE);
    glparamstate.current_call_list.execution_depth = 0;
    if (LIST_IS_USED(list)) {
        destroy_list(list);
    }
    LIST_RESERVE(list);
}

void glEndList(void)
{
    if (glparamstate.current_call_list.index < 0) {
        set_error(GL_INVALID_OPERATION);
        return;
    }

    GLuint list = glparamstate.current_call_list.index + CALL_LIST_START_ID;
    glparamstate.current_call_list.index = -1;
    glparamstate.current_call_list.execution_depth = 0;
}

void glCallList(GLuint id)
{
    if (id < CALL_LIST_START_ID ||
        id - CALL_LIST_START_ID >= MAX_CALL_LISTS) {
        set_error(GL_INVALID_OPERATION);
        return;
    }

    HANDLE_CALL_LIST(CALL_LIST, id);

    debug(OGX_LOG_CALL_LISTS, "Calling list %d", id - CALL_LIST_START_ID);

    bool must_decrement = false;
    if (glparamstate.current_call_list.index >= 0) {
        /* We don't want to expand the call list and put its command inside the
         * list currently building */
        glparamstate.current_call_list.execution_depth++;
        must_decrement = true;
    }

    CallList *list = &call_lists[id - CALL_LIST_START_ID];
    for (CommandBuffer *buffer = list->head;
         BUFFER_IS_VALID(buffer);
         buffer = buffer->next) {
        for (int i =0; i < MAX_COMMANDS_PER_BUFFER; i++) {
            Command *command = &buffer->commands[i];
            if (command->type == COMMAND_NONE) goto done;

            run_command(command);
        }
    }

done:
    /* Until we find a reliable mechanism to ensure that the client state has
     * been preserved, avoid reusing it across different lists. */
    s_last_client_state_is_valid = false;

    if (must_decrement) {
        glparamstate.current_call_list.execution_depth--;
    }
}

void glCallLists(GLsizei n, GLenum type, const GLvoid *lists)
{
    foreach(n, type, lists, glCallList);
}
