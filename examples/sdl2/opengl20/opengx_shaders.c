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
    ogx_set_mvp_matrix(m);

    Mtx normalm;
    ogx_matrix_gl_to_mtx(normal_matrix, normalm);
    GX_LoadNrmMtxImm(normalm, GX_PNMTX0);

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

    GX_SetNumTevStages(1);
    GX_SetNumTexGens(0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
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
    }
    return GL_NO_ERROR;
}

const OgxProgramProcessor s_processor = {
    .compile_shader = shader_compile,
    .link_program = link_program,
};

void setup_opengx_shaders()
{
    ogx_shader_register_program_processor(&s_processor);
}
