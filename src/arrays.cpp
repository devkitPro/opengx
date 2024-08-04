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

#include <limits>
#include <variant>

typedef float Pos3f[3];
typedef float Norm3f[3];
typedef float Tex2f[2];

struct GenericDataReaderBase {
    GenericDataReaderBase(int stride, int element_size):
        given_stride(stride), element_size(element_size) {}

    virtual void read_float(int index, float *elements) = 0;
    virtual void read_color(int index, GXColor *color) = 0;
    virtual void read_pos3f(int index, Pos3f pos) = 0;
    virtual void read_norm3f(int index, Norm3f norm) = 0;
    virtual void read_tex2f(int index, Tex2f tex) = 0;

    void set_num_elements(int n) {
        num_elements = n;
    }

    char element_size;
    char num_elements = 1;
    uint16_t given_stride = 0;
};

template <typename T>
struct GenericDataReader: public GenericDataReaderBase {
    GenericDataReader(const void *data, int stride):
        GenericDataReaderBase(stride, sizeof(T)),
        data(static_cast<const char *>(data))
        {}

    int compute_stride() const {
        return given_stride != 0 ? given_stride : (sizeof(T) * num_elements);
    }

    const T *elemAt(int index) {
        int stride = compute_stride();
        return reinterpret_cast<const T*>(data + stride * index);
    }

    template <typename R>
    void read(int index, R *elements) {
        const T *ptr = elemAt(index);
        for (int i = 0; i < num_elements; i++) {
            elements[i] = *ptr;
            ptr++;
        }
    }

    uint8_t read_color_component(const T *ptr) {
        if constexpr (std::numeric_limits<T>::is_integer) {
            return sizeof(T) > 1 ?
                (*ptr * 255 / std::numeric_limits<T>::max()) : *ptr;
        } else {  // floating-point type
            return *ptr * 255.0f;
        }
    }

    void read_float(int index, float *elements) override {
        read(index, elements);
    }

    void read_color(int index, GXColor *color) override {
        const T *ptr = elemAt(index);
        color->r = read_color_component(ptr++);
        color->g = read_color_component(ptr++);
        color->b = read_color_component(ptr++);
        color->a = num_elements == 4 ?
            read_color_component(ptr++) : 255;
    }

    void read_pos3f(int index, Pos3f pos) override {
        const T *ptr = elemAt(index);
        pos[0] = *ptr++;
        pos[1] = *ptr++;
        if (num_elements >= 3) {
            pos[2] = *ptr++;
            if (num_elements == 4) {
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
        if (num_elements >= 2) {
            tex[1] = *ptr++;
        } else {
            tex[1] = 0.0f;
        }
    }

    const char *data;
};

using ReaderVariant = std::variant<OgxArrayReader,
                                   GenericDataReaderBase,
                                   GenericDataReader<float>,
                                   GenericDataReader<double>,
                                   GenericDataReader<uint8_t>,
                                   GenericDataReader<int16_t>,
                                   GenericDataReader<int32_t>>;

void _ogx_array_reader_init(OgxArrayReader *reader,
                                  const void *data,
                                  GLenum type, int stride)
{
    switch (type) {
    case GL_UNSIGNED_BYTE:
        new (reader) GenericDataReader<int8_t>(data, stride);
        break;
    case GL_SHORT:
        new (reader) GenericDataReader<int16_t>(data, stride);
        break;
    case GL_INT:
        new (reader) GenericDataReader<int32_t>(data, stride);
        break;
    case GL_FLOAT:
        new (reader) GenericDataReader<float>(data, stride);
        break;
    case GL_DOUBLE:
        new (reader) GenericDataReader<double>(data, stride);
        break;
    default:
        warning("Unknown array data type %x\n", type);
    }
}

void _ogx_array_reader_set_num_elements(OgxArrayReader *reader, int n)
{
    GenericDataReaderBase *r = reinterpret_cast<GenericDataReaderBase *>(reader);
    r->set_num_elements(n);
}

void _ogx_array_reader_read_float(OgxArrayReader *reader,
                                  int index, float *elements)
{
    GenericDataReaderBase *r = reinterpret_cast<GenericDataReaderBase *>(reader);
    r->read_float(index, elements);
}

void _ogx_array_reader_read_pos3f(OgxArrayReader *reader,
                                  int index, float *pos)
{
    GenericDataReaderBase *r = reinterpret_cast<GenericDataReaderBase *>(reader);
    r->read_pos3f(index, pos);
}

void _ogx_array_reader_read_norm3f(OgxArrayReader *reader,
                                   int index, float *norm)
{
    GenericDataReaderBase *r = reinterpret_cast<GenericDataReaderBase *>(reader);
    r->read_norm3f(index, norm);
}

void _ogx_array_reader_read_tex2f(OgxArrayReader *reader,
                                  int index, float *tex)
{
    GenericDataReaderBase *r = reinterpret_cast<GenericDataReaderBase *>(reader);
    r->read_tex2f(index, tex);
}

void _ogx_array_reader_read_color(OgxArrayReader *reader,
                                  int index, GXColor *color)
{
    GenericDataReaderBase *r = reinterpret_cast<GenericDataReaderBase *>(reader);
    r->read_color(index, color);
}
