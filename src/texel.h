/*****************************************************************************
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

#ifndef OPENGX_TEXEL_H
#define OPENGX_TEXEL_H

#include "opengx.h"

#include <algorithm>
#include <math.h>
#include <ogc/gx.h>

static inline uint8_t luminance_from_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    /* TODO: fix this, the three components do not have the same weight to the
     * human eye. Use a formula from
     * https://songho.ca/dsp/luminance/luminance.html
     */
    int luminance = int(r) + int(g) + int(b);
    return luminance / 3;
}

struct Texel {
    virtual void set_color(GXColor color) = 0;
    virtual void store() = 0;
    virtual GXColor read() = 0;
    virtual int pitch_for_width(int width) = 0;

    virtual void set_area(void *data, int x, int y, int width, int height,
                          int pitch) {
        m_data = data;
        m_start_x = m_x = x;
        m_start_y = m_y = y;
        m_width = width;
        m_height = height;
        m_pitch = pitch;
    }

    void next() {
        m_x++;
        if (m_x == m_start_x + m_width) {
            m_y++;
            m_x = m_start_x;
        }
    }

    void *operator new(size_t size) { return malloc(size); }
    void operator delete(void * p) { free(p); }

    void *m_data;
    int m_x;
    int m_y;
    int m_start_x;
    int m_start_y;
    int m_width;
    int m_height;
    int m_pitch;
};

struct TexelRGBA8: public Texel {
    static constexpr bool has_rgb = true;
    static constexpr bool has_alpha = true;
    static constexpr bool has_luminance = false;

    TexelRGBA8() = default;
    void set_color(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
        r = red; g = green; b =blue; a = alpha;
    }

    void set_color(GXColor c) override {
        set_color(c.r, c.g, c.b, c.a);
    }

    uint8_t *current_address() {
        int block_x = m_x / 4;
        int block_y = m_y / 4;
        return static_cast<uint8_t*>(m_data) +
            block_y * m_pitch * 4 + block_x * 64 + (m_y % 4) * 8 + (m_x % 4) * 2;
    }

    void store() override {
        uint8_t *d = current_address();
        d[0] = a;
        d[1] = r;
        d[32] = g;
        d[33] = b;
        next();
    }

    GXColor read() override {
        uint8_t *d = current_address();
        next();
        return { d[1], d[32], d[33], d[0] };
    }

    static inline int compute_pitch(int width) {
        /* texel are in pairs of 4x4 blocks, each element 2 bytes wide */
        return ((width + 3) / 4) * 16;
    }

    int pitch_for_width(int width) override { return compute_pitch(width); }

    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

struct Texel16: public Texel {
    Texel16() = default;
    void setWord(uint16_t value) { word = value; }

    uint16_t *current_address() {
        int block_x = m_x / 4;
        int block_y = m_y / 4;
        return reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(m_data) +
            block_y * m_pitch * 4 + block_x * 32 + (m_y % 4) * 8 + (m_x % 4) * 2);
    }

    void store() override {
        uint16_t *d = current_address();
        d[0] = word;
        next();
    }

    static inline int compute_pitch(int width) {
        /* texel are in 4x4 blocks, each element 2 bytes wide */
        return ((width + 3) / 4) * 8;
    }

    int pitch_for_width(int width) override { return compute_pitch(width); }

    uint16_t word;
};

struct TexelIA8: public Texel16 {
    static constexpr bool has_rgb = false;
    static constexpr bool has_alpha = true;
    static constexpr bool has_luminance = true;

    TexelIA8() = default;
    void set_luminance_alpha(uint8_t luminance, uint8_t alpha) {
        setWord((alpha << 8) | luminance);
    }

    void set_color(GXColor c) override {
        int luminance = luminance_from_rgb(c.r, c.g, c.b);
        set_luminance_alpha(luminance, c.a);
    }

    GXColor read() override {
        uint16_t *d = current_address();
        next();
        uint8_t alpha = *d >> 8;
        uint8_t luminance = *d & 0xff;
        return { luminance, luminance, luminance, alpha };
    }
};

struct TexelRGB565: public Texel16 {
    static constexpr bool has_alpha = false;
    static constexpr bool has_rgb = true;

    TexelRGB565() = default;
    void set_color(uint8_t r, uint8_t g, uint8_t b) {
        setWord(((r & 0xf8) << 8) |
                ((g & 0xfc) << 3) |
                ((b & 0xf8) >> 3));
    }

    void set_color(GXColor c) override { set_color(c.r, c.g, c.b); }

    GXColor read() override {
        uint16_t *d = current_address();
        next();
        uint8_t red = (*d >> 8) & 0xf8;
        uint8_t green = (*d >> 3) & 0xfc;
        uint8_t blue = (*d << 3) & 0xf8;
        /* fill the lowest bits by repeating the highest ones */
        return {
            uint8_t(red | (red >> 5)),
            uint8_t(green | (green >> 6)),
            uint8_t(blue | (blue >> 5)),
            255
        };
    }
};

struct Texel8: public Texel {
    Texel8() = default;
    void setByte(uint8_t b) { value = b; }

    uint8_t *current_address() {
        int block_x = m_x / 8;
        int block_y = m_y / 4;
        return static_cast<uint8_t*>(m_data) +
            block_y * m_pitch * 4 + block_x * 32 + (m_y % 4) * 8 + (m_x % 8);
    }

    void store() override {
        uint8_t *d = current_address();
        next();
        d[0] = value;
    }

    static inline int compute_pitch(int width) {
        /* texel are in 8x4 blocks, each element 1 bytes wide */
        return ((width + 7) / 8) * 8;
    }

    int pitch_for_width(int width) override { return compute_pitch(width); }

    uint8_t value;
};

struct TexelI8: public Texel8 {
    static constexpr bool has_rgb = false;
    static constexpr bool has_alpha = false;
    static constexpr bool has_luminance = true;

    using Texel8::Texel8;
    void set_color(GXColor c) override {
        setByte(luminance_from_rgb(c.r, c.g, c.b));
    }
    void set_luminance(uint8_t l) { setByte(l); }

    GXColor read() override {
        uint8_t *d = current_address();
        next();
        return { d[0], d[0], d[0], 255 };
    }
};

struct TexelA8: public Texel8 {
    static constexpr bool has_rgb = false;
    static constexpr bool has_alpha = true;
    static constexpr bool has_luminance = false;

    using Texel8::Texel8;
    void set_color(GXColor c) override { setByte(c.a); }
    void set_alpha(uint8_t a) { setByte(a); }

    GXColor read() override {
        uint8_t *d = current_address();
        next();
        return { 255, 255, 255, d[0] };
    }
};

struct TexelI4: public Texel {
    TexelI4() = default;
    void set_luminance(uint8_t luminance) { value = luminance >> 4; }

    uint8_t *current_address() const {
        int block_x = m_x / 8;
        int block_y = m_y / 8;
        return static_cast<uint8_t*>(m_data) +
            block_y * m_pitch * 8 + block_x * 32 + (m_y % 8) * 4 + (m_x % 8) / 2;
    }

    void read_first_odd_pixel_in_line() {
        if (m_start_x % 2 != 0) {
            /* We start drawing at the second half of a byte, so read the first
             * half which we need to preserve */
            uint8_t *d = current_address();
            last_texel = d[0] & 0xf0;
        }
    }

    virtual void set_area(void *data, int x, int y, int width, int height,
                          int pitch) {
        Texel::set_area(data, x, y, width, height, pitch);
        read_first_odd_pixel_in_line();
    }

    void store() override {
        uint8_t *d = nullptr;
        if (m_x % 2 == 0) {
            last_texel = value << 4;
        } else {
            d = current_address();
            d[0] = last_texel | (value & 0xf);
        }
        m_x++;
        if (m_x == m_start_x + m_width) { /* new line */
            if (!d) { /* write the lonely last pixel of this line*/
                d = current_address();
                uint8_t b = d[0] & 0xf;
                d[0] = b | last_texel;
            }
            m_y++;
            m_x = m_start_x;
            if (m_y < m_start_y + m_height) {
                read_first_odd_pixel_in_line();
            }
        }
    }

    GXColor read() override {
        uint8_t *d = current_address();
        uint8_t c = m_x % 2 == 0 ? (d[0] & 0xf0) : (d[0] << 4);
        m_x++;
        if (m_x == m_start_x + m_width) { /* new line */
            m_y++;
            m_x = m_start_x;
        }
        /* fill the lowest bits by repeating the highest ones */
        c |= (c >> 4);
        return { c, c, c, 255 };
    }

    static inline int compute_pitch(int width) {
        /* texel are in 8x8 blocks, each element 4 bits wide */
        return ((width + 7) / 8) * 4;
    }

    int pitch_for_width(int width) override { return compute_pitch(width); }

    void set_color(GXColor c) override { set_luminance(c.r); }

    uint8_t value;
    uint8_t last_texel;
};

#endif /* OPENGX_TEXEL_H */
