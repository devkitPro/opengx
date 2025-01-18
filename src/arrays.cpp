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
#include "utils.h"
#include "vbo.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <ogc/gx.h>
#include <variant>

#define MAX_TEXCOORDS 8 /* GX_VA_TEX7 - GX_VA_TEX8 */

OgxArrayReader s_readers[1 /* positions (always)*/
                         + 1 /* normals (if s_has_normals) */
                         + MAX_COLOR_ARRAYS /* as s_num_colors */
                         + MAX_TEXCOORD_ARRAYS] /* as s_num_tex_arrays */;

/* The difference between s_num_tex_arrays and s_num_tex_coords is that the
 * former is used to count the number of active OgxArrayReader elements; but
 * not all of them consume a tex coordinate, because TexCoordProxyVertexReader
 * elements do not emit any new coordinates. So, s_num_tex_coords counts the
 * number of arrays that produce texture coordinates. */
static char s_num_tex_arrays = 0;
static char s_num_tex_coords = 0;

static bool s_has_normals = false;
static uint8_t s_num_colors = 0;
static OgxDrawFlags s_draw_flags = OGX_DRAW_FLAG_NONE;

static inline int count_attributes() {
    return 1 /* pos */ + s_has_normals + s_num_colors + s_num_tex_arrays;
}

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

static int compute_array_stride(const OgxVertexAttribArray *array)
{
    return array->stride > 0 ? array->stride :
        array->size * sizeof_gl_type(array->type);
}

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
        /* The hardware does not support sending more than 2 texture
         * coordinates */
        if (num_components > 2) info.format.num_components = 2;
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

/* This function assumes that num_components is at least 2 */
template <typename T>
static void to_pos3f(const T *components, int num_components, Pos3f pos) {
    pos[0] = *components++;
    pos[1] = *components++;
    if (num_components >= 3) {
        pos[2] = *components++;
        if (num_components == 4) {
            float w = *components++;
            pos[0] /= w;
            pos[1] /= w;
            pos[2] /= w;
        }
    } else {
        pos[2] = 0.0f;
    }
}

template <typename T>
static void to_norm3f(const T *components, Norm3f norm) {
    norm[0] = *components++;
    norm[1] = *components++;
    norm[2] = *components++;
}

/* This function assumes that num_components is at least 3 */
template <typename T>
static void to_color(const T *components, int num_components, GXColor *color) {
    auto read_color_component = [](const T value) {
        if constexpr (std::numeric_limits<T>::is_integer) {
            return value * 255 / std::numeric_limits<T>::max();
        } else {  // floating-point type
            return value * 255.0f;
        }
    };

    color->r = read_color_component(*components++);
    color->g = read_color_component(*components++);
    color->b = read_color_component(*components++);
    color->a = num_components == 4 ?
        read_color_component(*components++) : 255;
}

template <typename T>
static void to_tex2f(const T *components, int num_components, Tex2f tex) {
    tex[0] = *components++;
    if (num_components >= 2) {
        tex[1] = *components++;
    } else {
        tex[1] = 0.0f;
    }
}

struct AbstractVertexReader {
    AbstractVertexReader(GxVertexFormat format): format(format) {}

    virtual void setup_draw() {
        GX_SetVtxDesc(format.attribute, GX_DIRECT);
        GX_SetVtxAttrFmt(GX_VTXFMT0, format.attribute,
                         format.type, format.size, 0);
    }

    virtual void draw_done() {};

    virtual void get_format(uint8_t *attribute, uint8_t *inputmode,
                            uint8_t *type, uint8_t *size) const {
        *attribute = format.attribute;
        *inputmode = GX_DIRECT;
        *type = format.type;
        *size = format.size;
    }

    virtual uint8_t get_tex_coord_source() const {
        switch (format.attribute) {
        case GX_VA_POS:
            return GX_TG_POS;
        case GX_VA_NRM:
            return GX_TG_NRM;
        case GX_VA_CLR0:
        case GX_VA_CLR1:
            return GX_TG_COLOR0 + (format.attribute - GX_VA_CLR0);
        case GX_VA_TEX0:
        case GX_VA_TEX1:
        case GX_VA_TEX2:
        case GX_VA_TEX3:
        case GX_VA_TEX4:
        case GX_VA_TEX5:
        case GX_VA_TEX6:
        case GX_VA_TEX7:
            return GX_TG_TEX0 + (format.attribute - GX_VA_TEX0);
        default:
            return 0xff;
        }
    }

    virtual void process_element(int index) = 0;

    virtual void read_color(int index, GXColor *color) const = 0;
    virtual void read_pos3f(int index, Pos3f pos) const = 0;
    virtual void read_norm3f(int index, Norm3f norm) const = 0;
    virtual void read_tex2f(int index, Tex2f tex) const = 0;

protected:
    GxVertexFormat format;
};

template <typename T>
struct ConstantVertexReader: public AbstractVertexReader {
    ConstantVertexReader(GxVertexFormat format, const T *values):
        AbstractVertexReader(format) {
        for (int i = 0; i < format.num_components; i++) {
            this->values[i] = values[i];
        }
    }

    void process_element(int /*index*/) override {
        const T *ptr = values;
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

    void read_pos3f(int index, Pos3f pos) const override {
        to_pos3f(values, format.num_components, pos);
    }
    void read_norm3f(int index, Norm3f norm) const override {
        to_norm3f(values, norm);
    }
    void read_color(int index, GXColor *color) const override {
        to_color(values, format.num_components, color);
    }
    void read_tex2f(int index, Tex2f tex) const override {
        to_tex2f(values, format.num_components, tex);
    }

protected:
    T values[4];
};

struct GeneratorVertexReader: public AbstractVertexReader {
    GeneratorVertexReader(GxVertexFormat format, OgxGenerator_fv generator):
        AbstractVertexReader(format),
        generator(generator) {
    }

    void process_element(int index) override {
        float values[2];
        generator(index, values);
        for (int i = 0; i < format.num_components; i++) {
            GX_TexCoord1f32(values[i]);
        }
    }

    void read_pos3f(int index, Pos3f pos) const override {}
    void read_norm3f(int index, Norm3f norm) const override {}
    void read_color(int index, GXColor *color) const override {}
    void read_tex2f(int index, Tex2f tex) const override {
        generator(index, tex);
    }

protected:
    OgxGenerator_fv generator;
};

struct TexCoordProxyVertexReader: public AbstractVertexReader {
    TexCoordProxyVertexReader(GxVertexFormat format,
                              const AbstractVertexReader *source_reader):
        AbstractVertexReader(format),
        source_reader(source_reader) {}

    /* On these methods we do nothing, since we are just referencing data
     * already sent by another array */
    void process_element(int index) override {}
    void setup_draw() override {}
    void get_format(uint8_t *attribute, uint8_t *inputmode,
                    uint8_t *type, uint8_t *size) const override {
        *inputmode = GX_NONE;
    }

    /* For the other methods, we proxy the source array */
    uint8_t get_tex_coord_source() const override {
        return source_reader->get_tex_coord_source();
    }
    void read_color(int index, GXColor *color) const override {
        return source_reader->read_color(index, color);
    }
    void read_pos3f(int index, Pos3f pos) const override {
        return source_reader->read_pos3f(index, pos);
    }
    void read_norm3f(int index, Norm3f norm) const override {
        return source_reader->read_norm3f(index, norm);
    }
    void read_tex2f(int index, Tex2f tex) const override {
        return source_reader->read_tex2f(index, tex);
    }

protected:
    const AbstractVertexReader *source_reader;
};

struct VertexReaderBase: public AbstractVertexReader {
    VertexReaderBase(GxVertexFormat format, VboType vbo, const void *data,
                     int stride):
        AbstractVertexReader(format),
        data(static_cast<const char *>(vbo ?
                                       _ogx_vbo_get_data(vbo, data) : data)),
        stride(stride), vbo(vbo) {}

    void draw_done() override {}

    bool has_same_data(const OgxVertexAttribArray *array) const {
        int array_stride = compute_array_stride(array);
        VboType array_vbo = glparamstate.bound_vbo_array;
        const void *array_data = array_vbo ?
            _ogx_vbo_get_data(array_vbo, array->pointer) : array->pointer;
        return data == array_data && stride == array_stride;
    }

    const char *data;
    uint16_t stride;
    VboType vbo;
};

struct DirectVboReader: public VertexReaderBase {
    DirectVboReader(GxVertexFormat format,
                    VboType vbo, const void *data, int stride):
        VertexReaderBase(format, vbo, data, stride ? stride : format.stride())
    {
    }

    void setup_draw() override {
        GX_SetArray(format.attribute, const_cast<char*>(data), stride);
        GX_SetVtxDesc(format.attribute, GX_INDEX16);
        GX_SetVtxAttrFmt(GX_VTXFMT0, format.attribute,
                         format.type, format.size, 0);
    }

    void get_format(uint8_t *attribute, uint8_t *inputmode,
                    uint8_t *type, uint8_t *size) const override {
        VertexReaderBase::get_format(attribute, inputmode, type, size);
        *inputmode = GX_INDEX16;
    }

    void process_element(int index) {
        GX_Position1x16(index);
    }
    template<typename T> const T *elemAt(int index) const {
        return reinterpret_cast<const T*>(data + stride * index);
    }

    template<typename T>
    void read_floats(int index, float *out) const {
        const T *ptr = elemAt<T>(index);
        out[0] = *ptr++;
        out[1] = *ptr++;
        if (format.num_components >= 3) {
            out[2] = *ptr++;
        } else {
            out[2] = 0.0f;
        }
    }

    void read_float_components(int index, float *out) const {
        switch (format.size) {
        case GX_F32: read_floats<float>(index, out); break;
        case GX_S16: read_floats<int16_t>(index, out); break;
        case GX_U16: read_floats<uint16_t>(index, out); break;
        case GX_S8: read_floats<int8_t>(index, out); break;
        case GX_U8: read_floats<uint8_t>(index, out); break;
        }
    }

    void read_pos3f(int index, Pos3f pos) const override {
        read_float_components(index, pos);
    }
    void read_norm3f(int index, Norm3f norm) const override {
        read_float_components(index, norm);
    }

    /* These should never be called on this class, since it doesn't make sense
     * to call glArrayElement() when a VBO is bound, and they are not used for
     * texture coordinate generation. */
    void read_color(int index, GXColor *color) const override {}
    void read_tex2f(int index, Tex2f tex) const override {}
};

template <typename T>
struct GenericVertexReader: public VertexReaderBase {
    GenericVertexReader(GxVertexFormat format, VboType vbo, const void *data,
                        int stride):
        VertexReaderBase(format, vbo, data,
                         stride > 0 ? stride : sizeof(T) * format.num_components)
    {
    }

    const T *elemAt(int index) const {
        return reinterpret_cast<const T*>(data + stride * index);
    }

    static uint8_t read_color_component(const T *ptr) {
        if constexpr (std::numeric_limits<T>::is_integer) {
            return sizeof(T) > 1 ?
                (*ptr * 255 / std::numeric_limits<T>::max()) : *ptr;
        } else {  // floating-point type
            return *ptr * 255.0f;
        }
    }

    /* The following methods are only kept because of glArrayElement() */
    void read_color(int index, GXColor *color) const override {
        const T *ptr = elemAt(index);
        color->r = read_color_component(ptr++);
        color->g = read_color_component(ptr++);
        color->b = read_color_component(ptr++);
        color->a = format.num_components == 4 ?
            read_color_component(ptr++) : 255;
    }

    void read_pos3f(int index, Pos3f pos) const override {
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

    void read_norm3f(int index, Norm3f norm) const override {
        const T *ptr = elemAt(index);
        norm[0] = *ptr++;
        norm[1] = *ptr++;
        norm[2] = *ptr++;
    }

    void read_tex2f(int index, Tex2f tex) const override {
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
        } else {
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
                wgPipe->F32 = float(*ptr);
            }
        }
    }
};

static inline VertexReaderBase *get_reader(OgxArrayReader *reader)
{
    return reinterpret_cast<VertexReaderBase *>(reader);
}

void _ogx_arrays_setup_draw(const OgxDrawData *draw_data,
                            OgxDrawFlags flags)
{
    GX_ClearVtxDesc();

    s_draw_flags = flags;

    int num_arrays = s_draw_flags & OGX_DRAW_FLAG_FLAT ?
        1 : count_attributes();

    for (int i = 0; i < num_arrays; i++) {
        VertexReaderBase *r = get_reader(&s_readers[i]);
        r->setup_draw();
    }
}

void _ogx_arrays_process_element(int index)
{
    int num_arrays = s_draw_flags & OGX_DRAW_FLAG_FLAT ?
        1 : count_attributes();

    for (int i = 0; i < num_arrays; i++) {
        get_reader(&s_readers[i])->process_element(index);
    }
}

void _ogx_arrays_draw_done()
{
    int num_arrays = count_attributes();

    for (int i = 0; i < num_arrays; i++) {
        get_reader(&s_readers[i])->draw_done();
    }
}

void _ogx_array_reader_process_element(OgxArrayReader *reader, int index)
{
    VertexReaderBase *r = reinterpret_cast<VertexReaderBase *>(reader);
    r->process_element(index);
}

uint8_t _ogx_array_reader_get_tex_coord_source(OgxArrayReader *reader)
{
    return get_reader(reader)->get_tex_coord_source();
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

static OgxArrayReader *allocate_reader_for_format(GxVertexFormat *format)
{
    int attr;
    switch (format->attribute) {
    case GX_VA_POS:
        return &s_readers[0];
    case GX_VA_NRM:
        s_has_normals = true;
        return &s_readers[1];
    case GX_VA_CLR0:
        if (s_num_colors >= MAX_COLOR_ARRAYS) return NULL;
        format->attribute += s_num_colors;
        attr = 1 + s_has_normals;
        return &s_readers[attr + s_num_colors++];
    case GX_VA_TEX0:
        if (s_num_tex_arrays >= MAX_TEXCOORD_ARRAYS) return NULL;
        format->attribute += s_num_tex_coords;
        attr = 1 + s_has_normals + s_num_colors;
        return &s_readers[attr + s_num_tex_arrays++];
    }
    return NULL;
}

void _ogx_arrays_reset()
{
    s_has_normals = 0;
    s_num_colors = 0;
    s_num_tex_arrays = 0;
    s_num_tex_coords = 0;
}

OgxArrayReader *_ogx_array_add_constant_fv(uint8_t attribute, int size,
                                const float *values)
{
    TemplateSelectionInfo info = select_template(GL_FLOAT, attribute, size);
    OgxArrayReader *reader = allocate_reader_for_format(&info.format);
    if (!reader) return NULL;
    new (reader) ConstantVertexReader(info.format, values);
    if (attribute == GX_VA_TEX0)
        s_num_tex_coords++;
    return reader;
}

OgxArrayReader *_ogx_array_add(uint8_t attribute, const OgxVertexAttribArray *array)
{
    TemplateSelectionInfo info =
        select_template(array->type, attribute, array->size);
    OgxArrayReader *reader = allocate_reader_for_format(&info.format);
    if (!reader) return NULL;

    if (attribute == GX_VA_TEX0) {
        /* See if the data array is the same as the positional or normal array.
         * This is not just an optimization, it's actually needed because GX
         * only supports up to two input coordinates for GX_VA_TEXx, but the
         * client might provide three (along with an appropriate texture
         * matrix). So, at least in those cases where these arrays coincide, we
         * can support having three texture input coordinates. */
        OgxArrayReader *source_reader;
        if (get_reader(&s_readers[0])->has_same_data(array)) {
            source_reader = &s_readers[0];
        } else if (s_has_normals &&
                   get_reader(&s_readers[1])->has_same_data(array)) {
            source_reader = &s_readers[1];
        } else {
            /* We could go on and check if this array has the same data of
             * another texture array sent earlier in this same loop, but let's
             * leave this optimisation for later. */
            source_reader = NULL;
        }

        if (source_reader) {
            new (reader) TexCoordProxyVertexReader(info.format,
                                                   get_reader(source_reader));
            return reader;
        }

        /* Otherwise, this is an array providing its own texture coordinates;
         * increment the counter */
        s_num_tex_coords++;
    }

    VboType vbo = glparamstate.bound_vbo_array;
    GLenum type = array->type;
    int stride = array->stride;
    const void *data = array->pointer;

    if (info.same_type) {
        /* No conversions needed, just dump the data from the array directly
         * into the GX pipe. */
        if (vbo) {
            new (reader) DirectVboReader(info.format, vbo, data, stride);
            return reader;
        }
        switch (type) {
        case GL_UNSIGNED_BYTE:
            new (reader) SameTypeVertexReader<int8_t>(
                info.format, vbo, data, stride);
            return reader;
        case GL_SHORT:
            new (reader) SameTypeVertexReader<int16_t>(
                info.format, vbo, data, stride);
            return reader;
        case GL_INT:
            new (reader) SameTypeVertexReader<int32_t>(
                info.format, vbo, data, stride);
            return reader;
        case GL_FLOAT:
            new (reader) SameTypeVertexReader<float>(
                info.format, vbo, data, stride);
            return reader;
        }
    }

    if (attribute == GX_VA_CLR0 || attribute == GX_VA_CLR1) {
        switch (type) {
        /* The case GL_UNSIGNED_BYTE is handled by the SameTypeVertexReader */
        case GL_BYTE:
            new (reader) ColorVertexReader<char>(info.format, vbo, data, stride);
            return reader;
        case GL_SHORT:
            new (reader) ColorVertexReader<int16_t>(info.format, vbo, data, stride);
            return reader;
        case GL_INT:
            new (reader) ColorVertexReader<int32_t>(info.format, vbo, data, stride);
            return reader;
        case GL_FLOAT:
            new (reader) ColorVertexReader<float>(info.format, vbo, data, stride);
            return reader;
        case GL_DOUBLE:
            new (reader) ColorVertexReader<double>(info.format, vbo, data, stride);
            return reader;
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
        return reader;
    case GL_SHORT:
        new (reader) CoordVertexReader<int16_t>(info.format, vbo, data, stride);
        return reader;
    case GL_INT:
        new (reader) CoordVertexReader<int32_t>(info.format, vbo, data, stride);
        return reader;
    case GL_FLOAT:
        new (reader) CoordVertexReader<float>(info.format, vbo, data, stride);
        return reader;
    case GL_DOUBLE:
        new (reader) CoordVertexReader<double>(info.format, vbo, data, stride);
        return reader;
    }

    warning("Unknown array data type %x for attribute %d\n",
            type, attribute);
    return NULL;
}

OgxArrayReader *_ogx_array_add_generator_fv(uint8_t attribute, int size,
                                            OgxGenerator_fv generator)
{
    assert(attribute == GX_VA_TEX0);
    TemplateSelectionInfo info = select_template(GL_FLOAT, attribute, size);
    OgxArrayReader *reader = allocate_reader_for_format(&info.format);
    if (!reader) return NULL;

    new (reader) GeneratorVertexReader(info.format, generator);
    s_num_tex_coords++;
    return reader;
}

OgxArrayReader *_ogx_array_reader_next(OgxArrayReader *reader)
{
    if (!reader) return &s_readers[0];

    int attr = (reader - s_readers) + 1;
    return attr < count_attributes() ? &s_readers[attr] : NULL;
}

OgxArrayReader *_ogx_array_reader_for_attribute(uint8_t attribute)
{
    int n;
    switch (attribute) {
    case GX_VA_POS: return &s_readers[0];
    case GX_VA_NRM: return s_has_normals ? &s_readers[1] : NULL;
    case GX_VA_CLR0:
    case GX_VA_CLR1:
        n = attribute - GX_VA_CLR0;
        return s_num_colors > n ? &s_readers[1 + s_has_normals + n] : NULL;
    default: /* This can only be a GX_VA_TEX* */
        n = attribute - GX_VA_TEX0;
        return s_num_tex_arrays > n ?
            &s_readers[1 + s_has_normals + s_num_colors + n] : NULL;
    }
}

void _ogx_array_reader_get_format(OgxArrayReader *reader,
                                  uint8_t *attribute, uint8_t *inputmode,
                                  uint8_t *type, uint8_t *size)
{
    get_reader(reader)->get_format(attribute, inputmode, type, size);
}
