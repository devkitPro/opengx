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

#include "arrays.h"

#include "debug.h"
#include "state.h"
#include "texture_gen_sw.h"
#include "utils.h"
#include "vbo.h"

#include <cstdlib>
#include <cstring>
#include <limits>
#include <ogc/gx.h>
#include <variant>

static char s_num_tex_arrays = 0;
static bool s_has_normals = false;
static uint8_t s_num_colors = 0;
static uint8_t s_tex_unit_mask = 0;

struct GxVertexFormat {
    uint8_t attribute;
    char num_components;
    uint8_t type;
    uint8_t size;

    int stride() const {
        int component_size;
        if (attribute == GX_VA_CLR0 || attribute == GX_VA_CLR1) {
            component_size = 1;
        } else {
            switch (size) {
            case GX_S8:
            case GX_U8:
                component_size = 1; break;
            case GX_S16:
            case GX_U16:
                component_size = 2; break;
            case GX_F32:
                component_size = 4; break;
            }
        }
        return component_size * num_components;
    }
};

struct TemplateSelectionInfo {
    GxVertexFormat format;
    bool same_type;
};

static uint8_t gl_type_to_gx_size(GLenum type)
{
    switch (type) {
    case GL_SHORT: return GX_S16;
    case GL_FLOAT: return GX_F32;
    }
    return 0xff;
}

static TemplateSelectionInfo select_template(GLenum type,
                                             uint8_t vertex_attribute,
                                             char num_components)
{
    TemplateSelectionInfo info = {
        {vertex_attribute, num_components, 0xff, 0xff},
    };
    switch (vertex_attribute) {
    case GX_VA_POS:
        info.format.type = num_components == 2 ? GX_POS_XY : GX_POS_XYZ;
        info.format.size = gl_type_to_gx_size(type);
        info.same_type = num_components <= 3;
        break;
    case GX_VA_NRM:
        info.format.type = GX_NRM_XYZ;
        info.format.size = gl_type_to_gx_size(type);
        info.same_type = num_components == 3;
        break;
    case GX_VA_TEX0:
    case GX_VA_TEX1:
    case GX_VA_TEX2:
    case GX_VA_TEX3:
    case GX_VA_TEX4:
    case GX_VA_TEX5:
    case GX_VA_TEX6:
    case GX_VA_TEX7:
        info.format.type = num_components == 1 ? GX_TEX_S : GX_TEX_ST;
        info.format.size = gl_type_to_gx_size(type);
        info.same_type = num_components <= 2;
        break;
    case GX_VA_CLR0:
    case GX_VA_CLR1:
        if (num_components == 4) {
            info.format.type = GX_CLR_RGBA;
            info.format.size = GX_RGBA8;
        } else {
            info.format.type = GX_CLR_RGB;
            info.format.size = GX_RGB8;
        }
        info.same_type = type == GL_UNSIGNED_BYTE;
        break;
    }

    if (info.format.size == 0xff) {
        info.format.size = GX_F32;
        info.same_type = false;
    }
    return info;
}

struct AbstractVertexReader {
    virtual void setup_draw() = 0;
    virtual void draw_done() = 0;

    virtual void set_tex_coord_source(uint8_t source) = 0;
    virtual uint8_t get_tex_coord_source() const = 0;

    virtual void process_element(int index) = 0;

    virtual void read_color(int index, GXColor *color) = 0;
    virtual void read_pos3f(int index, Pos3f pos) = 0;
    virtual void read_norm3f(int index, Norm3f norm) = 0;
    virtual void read_tex2f(int index, Tex2f tex) = 0;
};

struct GeneratedTexVertexReader: public AbstractVertexReader {
    GeneratedTexVertexReader(OgxArrayReader *orig):
        prev(orig) {}

    template <class T>
    static T *create_at(OgxArrayReader *location) {
        OgxArrayReader *orig =
            static_cast<OgxArrayReader*>(malloc(sizeof(OgxArrayReader)));
        memcpy(orig, location, sizeof(OgxArrayReader));
        return new (location) T(orig);
    }

    void setup_draw() override {
        u8 attribute = GX_VA_TEX0 + s_num_tex_arrays++;
        GX_SetVtxDesc(attribute, GX_DIRECT);
        GX_SetVtxAttrFmt(GX_VTXFMT0, attribute, GX_TEX_ST, GX_F32, 0);
    }

    void draw_done() override {
        if (prev) {
            void *old_copy = prev;
            memcpy(this, old_copy, sizeof(OgxArrayReader));
            free(old_copy);
        }
    }

    void set_tex_coord_source(uint8_t source) override {
        tex_coord_source = source;
    }
    uint8_t get_tex_coord_source() const override { return tex_coord_source; }

    void process_element(int index) override {
        Tex2f value;
        read_tex2f(index, value);
        GX_TexCoord2f32(value[0], value[1]);
    }

    void read_color(int index, GXColor *color) override {}
    void read_pos3f(int index, Pos3f pos) override {}
    void read_norm3f(int index, Norm3f norm) override {}

protected:
    /* This is the instance of the VertexReader that the
     * GeneratedTexVertexReader is replacing. It will have to be restored in
     * place after the drawing has been done. */
    OgxArrayReader *prev;
    uint8_t tex_coord_source;
};

struct SphereMapTexReader: public GeneratedTexVertexReader {
    using GeneratedTexVertexReader::GeneratedTexVertexReader;
    void read_tex2f(int index, Tex2f tex) override {
        _ogx_texture_gen_sw_sphere_map(index, tex);
    }
};

struct VertexReaderBase: public AbstractVertexReader {
    VertexReaderBase(GxVertexFormat format, VboType vbo, const void *data,
                     int stride):
        format(format),
        data(static_cast<const char *>(vbo ?
                                       _ogx_vbo_get_data(vbo, data) : data)),
        stride(stride), dup_color(false), vbo(vbo) {}

    void setup_draw() override {
        if (format.attribute >= GX_VA_TEX0 &&
            format.attribute <= GX_VA_TEX7) {
            /* Texture coordinates must be enable sequentially */
            format.attribute = GX_VA_TEX0 + s_num_tex_arrays++;
            /* And GX does not support more than two coordinates */
            if (format.num_components > 2) format.num_components = 2;
        }
        GX_SetVtxDesc(format.attribute, GX_DIRECT);
        GX_SetVtxAttrFmt(GX_VTXFMT0, format.attribute,
                         format.type, format.size, 0);
        if (dup_color) {
            GX_SetVtxDesc(format.attribute + 1, GX_DIRECT);
            GX_SetVtxAttrFmt(GX_VTXFMT0, format.attribute + 1,
                             format.type, format.size, 0);
        }
    }

    void enable_duplicate_color(bool dup_color) {
        this->dup_color = dup_color;
    }

    void draw_done() override {}

    void set_tex_coord_source(uint8_t source) override {
        tex_coord_source = source;
    }
    uint8_t get_tex_coord_source() const override { return tex_coord_source; }

    bool has_same_data(VertexReaderBase *other) const {
        return data == other->data && stride == other->stride;
    }

    GxVertexFormat format;
    const char *data;
    uint16_t stride;
    bool dup_color;
    uint8_t tex_coord_source;
    VboType vbo;
};

struct DirectVboReader: public VertexReaderBase {
    DirectVboReader(GxVertexFormat format,
                    VboType vbo, const void *data, int stride):
        VertexReaderBase(format, vbo, data, stride ? stride : format.stride())
    {
    }

    void setup_draw() override {
        if (format.attribute >= GX_VA_TEX0 &&
            format.attribute <= GX_VA_TEX7) {
            /* Texture coordinates must be enable sequentially */
            format.attribute = GX_VA_TEX0 + s_num_tex_arrays++;
        }
        GX_SetArray(format.attribute, const_cast<char*>(data), stride);
        GX_SetVtxDesc(format.attribute, GX_INDEX16);
        GX_SetVtxAttrFmt(GX_VTXFMT0, format.attribute,
                         format.type, format.size, 0);
        if (dup_color) {
            GX_SetVtxDesc(format.attribute + 1, GX_INDEX16);
            GX_SetVtxAttrFmt(GX_VTXFMT0, format.attribute + 1,
                             format.type, format.size, 0);
        }
    }

    void process_element(int index) {
        GX_Position1x16(index);
    }
    template<typename T> const T *elemAt(int index) {
        return reinterpret_cast<const T*>(data + stride * index);
    }

    template<typename T>
    void read_floats(int index, float *out) {
        const T *ptr = elemAt<T>(index);
        out[0] = *ptr++;
        out[1] = *ptr++;
        if (format.num_components >= 3) {
            out[2] = *ptr++;
        } else {
            out[2] = 0.0f;
        }
    }

    void read_float_components(int index, float *out) {
        switch (format.size) {
        case GX_F32: read_floats<float>(index, out); break;
        case GX_S16: read_floats<int16_t>(index, out); break;
        case GX_U16: read_floats<uint16_t>(index, out); break;
        case GX_S8: read_floats<int8_t>(index, out); break;
        case GX_U8: read_floats<uint8_t>(index, out); break;
        }
    }

    void read_pos3f(int index, Pos3f pos) override {
        read_float_components(index, pos);
    }
    void read_norm3f(int index, Norm3f norm) override {
        read_float_components(index, norm);
    }

    /* These should never be called on this class, since it doesn't make sense
     * to call glArrayElement() when a VBO is bound, and they are not used for
     * texture coordinate generation. */
    void read_color(int index, GXColor *color) override {}
    void read_tex2f(int index, Tex2f tex) override {}
};

template <typename T>
struct GenericVertexReader: public VertexReaderBase {
    GenericVertexReader(GxVertexFormat format, VboType vbo, const void *data,
                        int stride):
        VertexReaderBase(format, vbo, data,
                         stride > 0 ? stride : sizeof(T) * format.num_components)
    {
    }

    const T *elemAt(int index) {
        return reinterpret_cast<const T*>(data + stride * index);
    }

    uint8_t read_color_component(const T *ptr) {
        if constexpr (std::numeric_limits<T>::is_integer) {
            return sizeof(T) > 1 ?
                (*ptr * 255 / std::numeric_limits<T>::max()) : *ptr;
        } else {  // floating-point type
            return *ptr * 255.0f;
        }
    }

    /* The following methods are only kept because of glArrayElement() */
    void read_color(int index, GXColor *color) override {
        const T *ptr = elemAt(index);
        color->r = read_color_component(ptr++);
        color->g = read_color_component(ptr++);
        color->b = read_color_component(ptr++);
        color->a = format.num_components == 4 ?
            read_color_component(ptr++) : 255;
    }

    void read_pos3f(int index, Pos3f pos) override {
        const T *ptr = elemAt(index);
        pos[0] = *ptr++;
        pos[1] = *ptr++;
        if (format.num_components >= 3) {
            pos[2] = *ptr++;
            if (format.num_components == 4) {
                float w = *ptr++;
                pos[0] /= w;
                pos[1] /= w;
                pos[2] /= w;
            }
        } else {
            pos[2] = 0.0f;
        }
    }

    void read_norm3f(int index, Norm3f norm) override {
        const T *ptr = elemAt(index);
        norm[0] = *ptr++;
        norm[1] = *ptr++;
        norm[2] = *ptr++;
    }

    void read_tex2f(int index, Tex2f tex) override {
        const T *ptr = elemAt(index);
        tex[0] = *ptr++;
        if (format.num_components >= 2) {
            tex[1] = *ptr++;
        } else {
            tex[1] = 0.0f;
        }
    }
};

template <typename T>
struct SameTypeVertexReader: public GenericVertexReader<T> {
    using GenericVertexReader<T>::GenericVertexReader;
    using GenericVertexReader<T>::elemAt;
    using GenericVertexReader<T>::format;

    void process_element(int index) override {
        const T *ptr = elemAt(index);
        /* Directly write to the GX pipe. We don't really care if the values
         * are signed or unsigned, integers or floating point: we already know
         * that the attribute's type matches (or we wouldn't have selected the
         * SameTypeVertexReader subclass), the only thing that matters is the
         * size of the element that we put into the GX pipe. */
        for (int i = 0; i < format.num_components; i++, ptr++) {
            if constexpr (sizeof(T) == 1) {
                wgPipe->U8 = *ptr;
            } else if constexpr (sizeof(T) == 2) {
                wgPipe->U16 = *ptr;
            } else if constexpr (sizeof(T) == 4) {
                wgPipe->F32 = *ptr;
            }
        }
    }
};

template <typename T>
struct ColorVertexReader: public GenericVertexReader<T> {
    using GenericVertexReader<T>::GenericVertexReader;
    using GenericVertexReader<T>::elemAt;
    using GenericVertexReader<T>::format;
    using GenericVertexReader<T>::read_color_component;

    void process_element(int index) {
        GXColor color;
        const T *ptr = elemAt(index);
        color.r = read_color_component(ptr++);
        color.g = read_color_component(ptr++);
        color.b = read_color_component(ptr++);
        if (format.num_components == 4) {
            color.a = read_color_component(ptr++);
            GX_Color4u8(color.r, color.g, color.b, color.a);
            if (this->dup_color)
                GX_Color4u8(color.r, color.g, color.b, color.a);
        } else {
            GX_Color3u8(color.r, color.g, color.b);
            if (this->dup_color)
                GX_Color3u8(color.r, color.g, color.b);
        }
    }
};

template <typename T>
struct CoordVertexReader: public GenericVertexReader<T> {
    using GenericVertexReader<T>::elemAt;
    using GenericVertexReader<T>::format;
    using GenericVertexReader<T>::GenericVertexReader;

    void process_element(int index) {
        const T *ptr = elemAt(index);
        if (format.num_components == 4) {
            float x, y, z, w;
            x = *ptr++;
            y = *ptr++;
            z = *ptr++;
            w = *ptr++;
            x /= w;
            y /= w;
            z /= w;
            GX_Position3f32(x, y, z);
        } else {
            for (int i = 0; i < format.num_components; i++, ptr++) {
                wgPipe->F32 = float(*ptr++);
            }
        }
    }
};

void _ogx_array_reader_init(OgxArrayReader *reader,
                            uint8_t vertex_attribute,
                            const void *data,
                            int num_components, GLenum type, int stride)
{
    TemplateSelectionInfo info =
        select_template(type, vertex_attribute, num_components);

    VboType vbo = glparamstate.bound_vbo_array;

    if (info.same_type) {
        /* No conversions needed, just dump the data from the array directly
         * into the GX pipe. */
        if (vbo) {
            new (reader) DirectVboReader(info.format, vbo, data, stride);
            return;
        }
        switch (type) {
        case GL_UNSIGNED_BYTE:
            new (reader) SameTypeVertexReader<int8_t>(
                info.format, vbo, data, stride);
            return;
        case GL_SHORT:
            new (reader) SameTypeVertexReader<int16_t>(
                info.format, vbo, data, stride);
            return;
        case GL_INT:
            new (reader) SameTypeVertexReader<int32_t>(
                info.format, vbo, data, stride);
            return;
        case GL_FLOAT:
            new (reader) SameTypeVertexReader<float>(
                info.format, vbo, data, stride);
            return;
        }
    }

    if (vertex_attribute == GX_VA_CLR0 || vertex_attribute == GX_VA_CLR1) {
        switch (type) {
        /* The case GL_UNSIGNED_BYTE is handled by the SameTypeVertexReader */
        case GL_BYTE:
            new (reader) ColorVertexReader<char>(info.format, vbo, data, stride);
            return;
        case GL_SHORT:
            new (reader) ColorVertexReader<int16_t>(info.format, vbo, data, stride);
            return;
        case GL_INT:
            new (reader) ColorVertexReader<int32_t>(info.format, vbo, data, stride);
            return;
        case GL_FLOAT:
            new (reader) ColorVertexReader<float>(info.format, vbo, data, stride);
            return;
        case GL_DOUBLE:
            new (reader) ColorVertexReader<double>(info.format, vbo, data, stride);
            return;
        }
    }

    /* We can use the CoordVertexReader not only for positional
     * coordinates, but also for normals and texture coordinates because
     * the GX_Position*() functions are just storing floats into the GX
     * pipe (that is, GX_Position2f32() behaves exactly like
     * GX_TexCoord2f32()). */
    switch (type) {
    case GL_BYTE:
        new (reader) CoordVertexReader<char>(info.format, vbo, data, stride);
        return;
    case GL_SHORT:
        new (reader) CoordVertexReader<int16_t>(info.format, vbo, data, stride);
        return;
    case GL_INT:
        new (reader) CoordVertexReader<int32_t>(info.format, vbo, data, stride);
        return;
    case GL_FLOAT:
        new (reader) CoordVertexReader<float>(info.format, vbo, data, stride);
        return;
    case GL_DOUBLE:
        new (reader) CoordVertexReader<double>(info.format, vbo, data, stride);
        return;
    }

    warning("Unknown array data type %x for attribute %d\n",
            type, vertex_attribute);
}

static inline VertexReaderBase *get_reader(OgxArrayReader *reader)
{
    return reinterpret_cast<VertexReaderBase *>(reader);
}

void _ogx_arrays_setup_draw(bool has_normals, uint8_t num_colors,
                            uint8_t tex_unit_mask)
{
    GX_ClearVtxDesc();
    s_num_tex_arrays = 0;

    VertexReaderBase *vertex_reader = get_reader(&glparamstate.vertex_array);
    vertex_reader->setup_draw();

    VertexReaderBase *normal_reader = nullptr;
    if (has_normals) {
        normal_reader = get_reader(&glparamstate.normal_array);
        normal_reader->setup_draw();
    }
    if (num_colors > 0) {
        VertexReaderBase *r = get_reader(&glparamstate.color_array);
        r->enable_duplicate_color(num_colors > 1);
        r->setup_draw();
    }

    int sent_tex_arrays = 0;
    s_tex_unit_mask = 0;
    if (tex_unit_mask) {
        for (int i = 0; i < MAX_TEXTURE_UNITS; i++) {
            VertexReaderBase *r = get_reader(&glparamstate.texcoord_array[i]);
            u8 unit_bit = 1 << i;
            if (glparamstate.texture_unit[i].gen_enabled) {
                /* Some kinds of texture generation cannot be performed by the
                 * GPU, and we have to generate the texture coordinates in
                 * software. */
                if (_ogx_texture_gen_sw_enabled(i)) {
                    GeneratedTexVertexReader *gr;
                    switch (glparamstate.texture_unit[i].gen_mode) {
                    case GL_SPHERE_MAP:
                        gr = GeneratedTexVertexReader::create_at
                            <SphereMapTexReader>(&glparamstate.texcoord_array[i]);
                        break;
                    }
                    gr->setup_draw();
                    gr->set_tex_coord_source(GX_TG_TEX0 + sent_tex_arrays++);
                    s_tex_unit_mask |= unit_bit;
                    continue;
                }
            }

            if (tex_unit_mask & unit_bit) {
                /* See if the data array is the same as the positional or
                 * normal array. This is not just an optimization, it's
                 * actually needed because GX only supports up to two input
                 * coordinates for GX_VA_TEXx, but the client might provide
                 * three (along with an appropriate texture matrix). So, at
                 * least in those cases where these arrays coincide, we can
                 * support having three texture input coordinates. */
                if (r->has_same_data(vertex_reader)) {
                    r->set_tex_coord_source(GX_TG_POS);
                } else if (normal_reader &&
                           r->has_same_data(normal_reader)) {
                    r->set_tex_coord_source(GX_TG_NRM);
                } else {
                    /* We could go on and check if this array has the same data
                     * of another texture array sent earlier in this same loop,
                     * but let's leave this optimisation for later. */
                    r->setup_draw();
                    r->set_tex_coord_source(GX_TG_TEX0 + sent_tex_arrays++);
                    s_tex_unit_mask |= unit_bit;
                }
            }
        }
    }

    s_has_normals = has_normals;
    s_num_colors = num_colors;
    /* s_tex_unit_mask has been set in the loop above */
}

uint8_t _ogx_arrays_get_units_with_tex_coord()
{
    return s_tex_unit_mask;
}

void _ogx_arrays_process_element(int index)
{
    get_reader(&glparamstate.vertex_array)->process_element(index);

    if (s_has_normals) {
        get_reader(&glparamstate.normal_array)->process_element(index);
    }

    if (s_num_colors) {
        get_reader(&glparamstate.color_array)->process_element(index);
    }

    if (s_tex_unit_mask) {
        for (int i = 0; i < MAX_TEXTURE_UNITS; i++) {
            if (s_tex_unit_mask & (1 << i)) {
                get_reader(&glparamstate.texcoord_array[i])->
                    process_element(index);
            }
        }
    }
}

void _ogx_arrays_draw_done()
{
    get_reader(&glparamstate.vertex_array)->draw_done();

    if (s_has_normals) {
        get_reader(&glparamstate.normal_array)->draw_done();
    }

    if (s_num_colors) {
        get_reader(&glparamstate.color_array)->draw_done();
    }

    if (s_tex_unit_mask) {
        for (int i = 0; i < MAX_TEXTURE_UNITS; i++) {
            if (s_tex_unit_mask & (1 << i)) {
                get_reader(&glparamstate.texcoord_array[i])->draw_done();
            }
        }
    }
}

void _ogx_array_reader_enable_dup_color(OgxArrayReader *reader,
                                        bool dup_color)
{
    VertexReaderBase *r = reinterpret_cast<VertexReaderBase *>(reader);
    r->enable_duplicate_color(dup_color);
}

void _ogx_array_reader_process_element(OgxArrayReader *reader, int index)
{
    VertexReaderBase *r = reinterpret_cast<VertexReaderBase *>(reader);
    r->process_element(index);
}

uint8_t _ogx_array_reader_get_tex_coord_source(OgxArrayReader *reader)
{
    return get_reader(reader)->tex_coord_source;
}

void _ogx_array_reader_read_pos3f(OgxArrayReader *reader,
                                  int index, float *pos)
{
    VertexReaderBase *r = reinterpret_cast<VertexReaderBase *>(reader);
    r->read_pos3f(index, pos);
}

void _ogx_array_reader_read_norm3f(OgxArrayReader *reader,
                                   int index, float *norm)
{
    VertexReaderBase *r = reinterpret_cast<VertexReaderBase *>(reader);
    r->read_norm3f(index, norm);
}

void _ogx_array_reader_read_tex2f(OgxArrayReader *reader,
                                  int index, float *tex)
{
    VertexReaderBase *r = reinterpret_cast<VertexReaderBase *>(reader);
    r->read_tex2f(index, tex);
}

void _ogx_array_reader_read_color(OgxArrayReader *reader,
                                  int index, GXColor *color)
{
    VertexReaderBase *r = reinterpret_cast<VertexReaderBase *>(reader);
    r->read_color(index, color);
}
