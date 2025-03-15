#define GL_GLEXT_PROTOTYPES
#include "opengx.h"

#include <SDL_opengl.h>
#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>

static inline GXColor gxcol_new_fv(const float *components)
{
    GXColor c = {
        (u8)(components[0] * 255.0f),
        (u8)(components[1] * 255.0f),
        (u8)(components[2] * 255.0f),
        (u8)(components[3] * 255.0f)
    };
    return c;
}

typedef struct {
    GLint mvp_loc;
    GLint normal_matrix_loc;
    GLint mat_color_loc;
    GLint light_pos_loc;
} Gl2GearsData;

static void gl2gears_setup_draw(GLuint program, const OgxDrawData *draw_data,
                                void *user_data)
{
    Gl2GearsData *data = user_data;
    float m[16];
    glGetUniformfv(program, data->mvp_loc, m);
    float normal_matrix[16];
    glGetUniformfv(program, data->normal_matrix_loc, normal_matrix);
    float colorf[4];
    glGetUniformfv(program, data->mat_color_loc, colorf);
    float light_dir[4];
    glGetUniformfv(program, data->light_pos_loc, light_dir);
    ogx_shader_set_mvp_gl(m);

    Mtx normalm;
    ogx_matrix_gl_to_mtx(normal_matrix, normalm);
    GX_LoadNrmMtxImm(normalm, GX_PNMTX0);

    uint8_t stage = GX_TEVSTAGE0 + ogx_gpu_resources->tevstage_first++;

    GXLightObj light;
    // Increase distance to simulate a directional light
    float light_pos[3] = {
        light_dir[0] * 100000,
        light_dir[1] * 100000,
        light_dir[2] * 100000,
    };
    GX_InitLightPosv(&light, light_pos);
    GXColor white = { 255, 255, 255, 255 };
    GX_InitLightColor(&light, white);
    GX_InitLightAttn(&light, 1, 0, 0, 1, 0, 0); // no attenuation
    GX_LoadLightObj(&light, 1);


    GXColor mat_color = gxcol_new_fv(colorf);
    GX_SetNumChans(1);
    GX_SetChanMatColor(GX_COLOR0A0, mat_color);
    GX_SetChanCtrl(GX_COLOR0A0, GX_ENABLE, GX_SRC_REG, GX_SRC_REG,
                   1, GX_DF_CLAMP, GX_AF_NONE);

    GX_SetTevOp(stage, GX_PASSCLR);
}

typedef struct {
    GLint mvp_loc;
    GLint tex_sampler_loc;
} CubeTexData;

static void cube_tex_setup_matrices(GLuint program, void *user_data)
{
    CubeTexData *data = user_data;
    float m[16];
    glGetUniformfv(program, data->mvp_loc, m);
    ogx_shader_set_mvp_gl(m);
}

static void cube_tex_setup_draw(GLuint program, const OgxDrawData *draw_data,
                                void *user_data)
{
    CubeTexData *data = user_data;
    GLint texture_unit;
    glGetUniformiv(program, data->tex_sampler_loc, &texture_unit);

    uint8_t tex_map = GX_TEXMAP0 + ogx_gpu_resources->texmap_first++;
    uint8_t tex_coord = GX_TEXCOORD0 + ogx_gpu_resources->texcoord_first++;
    uint8_t input_coordinates = GX_TG_TEX0;
    uint8_t stage = GX_TEVSTAGE0 + ogx_gpu_resources->tevstage_first++;
    GXTexObj *texture;
    texture = ogx_shader_get_texobj(texture_unit);
    GX_LoadTexObj(texture, tex_map);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
                   0, GX_DF_CLAMP, GX_AF_NONE);

    // In data: c: Texture Color b: raster value, Operation: b*c
    GX_SetTevColorIn(stage, GX_CC_ZERO, GX_CC_RASC, GX_CC_TEXC, GX_CC_CPREV);
    GX_SetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_RASA, GX_CA_TEXA, GX_CA_APREV);
    GX_SetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE,
                     GX_TEVPREV);
    GX_SetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE,
                     GX_TEVPREV);
    GX_SetTexCoordGen(tex_coord, GX_TG_MTX2x4, input_coordinates, GX_IDENTITY);

    GX_SetTevOrder(stage, tex_coord, tex_map, GX_COLOR0A0);
}

static bool shader_compile(GLuint shader)
{
    uint32_t source_hash = ogx_shader_get_source_hash(shader);

    fprintf(stderr, "%s:%d shader %x hash %08x\n", __FILE__, __LINE__, shader, source_hash);
    if (source_hash == 0x5b32d27f) {
        /* gl2gears.c vertex shader */
        ogx_shader_add_uniforms(shader, 4,
            "ModelViewProjectionMatrix", GL_FLOAT_MAT4,
            "NormalMatrix", GL_FLOAT_MAT4,
            "LightSourcePosition", GL_FLOAT_VEC4,
            "MaterialColor", GL_FLOAT_VEC4);
        ogx_shader_add_attributes(shader, 2,
                                  "position", GL_FLOAT_VEC3, GX_VA_POS,
                                  "normal", GL_FLOAT_VEC3, GX_VA_NRM);
    } else if (source_hash == 0x53560768) {
        /* cube_tex.cpp vertex shader */
        ogx_shader_add_uniforms(shader, 2,
            "MVP", GL_FLOAT_MAT4,
            "myTextureSampler", GL_SAMPLER_2D);
        ogx_shader_add_attributes(shader, 3,
            "vertexPosition_modelspace", GL_FLOAT_VEC3, GX_VA_POS,
            "vertexUV", GL_FLOAT_VEC2, GX_VA_TEX0,
            "vertexColor", GL_FLOAT_VEC4, GX_VA_CLR0);
    }
}

static GLenum link_program(GLuint program)
{
    GLuint shaders[2];
    int count = 0;
    glGetAttachedShaders(program, 2, &count, shaders);
    uint32_t vertex_shader_hash = ogx_shader_get_source_hash(shaders[0]);
    if (vertex_shader_hash == 0x5b32d27f) { /* gl2gears */
        Gl2GearsData *data = calloc(1, sizeof(Gl2GearsData));
        data->mvp_loc = glGetUniformLocation(program, "ModelViewProjectionMatrix");
        data->normal_matrix_loc = glGetUniformLocation(program, "NormalMatrix");
        data->mat_color_loc = glGetUniformLocation(program, "MaterialColor");
        data->light_pos_loc = glGetUniformLocation(program, "LightSourcePosition");
        ogx_shader_program_set_user_data(program, data, free);
        ogx_shader_program_set_setup_draw_cb(program, gl2gears_setup_draw);
    } else if (vertex_shader_hash == 0x53560768) {
        /* cube_tex.cpp vertex shader */
        CubeTexData *data = calloc(1, sizeof(CubeTexData));
        data->mvp_loc = glGetUniformLocation(program, "MVP");
        data->tex_sampler_loc = glGetUniformLocation(program, "myTextureSampler");
        ogx_shader_program_set_user_data(program, data, free);
        ogx_shader_program_set_setup_matrices_cb(program, cube_tex_setup_matrices);
        ogx_shader_program_set_setup_draw_cb(program, cube_tex_setup_draw);
    }
    return GL_NO_ERROR;
}

static const OgxProgramProcessor s_processor = {
    .compile_shader = shader_compile,
    .link_program = link_program,
};

void setup_opengx_shaders()
{
    ogx_shader_register_program_processor(&s_processor);
}
