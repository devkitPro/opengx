/*****************************************************************************
Copyright (c) 2024  Alberto Mardegan (mardy@users.sourceforge.net)

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

/* Code loosely based on
 * https://github.com/opengl-tutorials/ogl/tree/2.1_branch/tutorial05_textured_cube
 * Rewritten for SDL2 and embedded textures.
 */

#define GL_GLEXT_PROTOTYPES

#include "textures.h"

#if defined(__wii__) || defined(__gamecube__)
#include "opengx_shaders.h"
#endif

#include <SDL.h>
#include <SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
using namespace glm;

static SDL_GameController *controller = NULL;
static SDL_Window *window = NULL;
static bool done = false;
static bool clip_enabled = false;
static bool stencil_enabled = false;

static const char *vertex_shader =
"#version 120\n"
"\n"
"// Input vertex data, different for all executions of this shader.\n"
"attribute vec3 vertexPosition_modelspace;\n"
"attribute vec2 vertexUV;\n"
"attribute vec4 vertexColor;\n"
"\n"
"// Output data ; will be interpolated for each fragment.\n"
"varying vec2 UV;\n"
"varying vec4 Color;\n"
"\n"
"// Values that stay constant for the whole mesh.\n"
"uniform mat4 MVP;\n"
"\n"
"void main(){\n"
"   // Output position of the vertex, in clip space : MVP * position\n"
"   gl_Position =  MVP * vec4(vertexPosition_modelspace,1);\n"
"\n"
"   // UV of the vertex. No special space for this one.\n"
"   UV = vertexUV;\n"
"   Color = vertexColor;\n"
"}\n";

static const char *fragment_shader =
"#version 120\n"
"\n"
"// Interpolated values from the vertex shaders\n"
"varying vec2 UV;\n"
"varying vec4 Color;\n"
"\n"
"// Values that stay constant for the whole mesh.\n"
"uniform sampler2D myTextureSampler;\n"
"\n"
"void main(){\n"
"   // Output color = color of the texture at the specified UV\n"
"   gl_FragColor = texture2D( myTextureSampler, UV ) * Color;\n"
"}\n";

static void
process_event(SDL_Event *event)
{
    switch (event->type) {
    case SDL_CONTROLLERBUTTONDOWN:
        switch (event->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_START:
        case SDL_CONTROLLER_BUTTON_BACK:
            done = true;
            break;
        case SDL_CONTROLLER_BUTTON_A:
            clip_enabled = !clip_enabled;
            break;
        case SDL_CONTROLLER_BUTTON_B:
            stencil_enabled = !stencil_enabled;
            break;
        }
        break;
    case SDL_KEYDOWN:
        switch (event->key.keysym.sym) {
        case SDLK_c:
            clip_enabled = !clip_enabled;
            break;
        case SDLK_s:
            stencil_enabled = !stencil_enabled;
            break;
        }
        break;
    case SDL_QUIT:
        done = true;
        break;

    case SDL_JOYDEVICEADDED:
        /* Of course, we should dispose the old one, etc etc :-) */
        controller = SDL_GameControllerOpen(event->jdevice.which);
        break;
    }
}

int main(int argc, char **argv)
{
#if defined(__wii__) || defined(__gamecube__)
    setup_opengx_shaders();
#endif

    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_Log("SDL init error: %s", SDL_GetError());
        return EXIT_FAILURE;
    }

    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 1);
    window = SDL_CreateWindow("Cube",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              640, 480,
                              SDL_WINDOW_OPENGL);
    if (!window) {
        SDL_Log("Unable to create window: %s", SDL_GetError());
        return EXIT_FAILURE;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        SDL_Log("Unable to create GL context: %s", SDL_GetError());
        return EXIT_FAILURE;
    }

    // Dark blue background
    glClearColor(0.0f, 0.0f, 0.4f, 0.0f);
    glClearStencil(0);
    glStencilMask(1);

    // Enable depth test
    glEnable(GL_DEPTH_TEST);
    // Accept fragment if it closer to the camera than the former one
    glDepthFunc(GL_LESS);

    SDL_Log("info: %s:%d", __func__, __LINE__);

    /* Compile the vertex shader */
    char msg[512];
    GLuint v = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(v, 1, &vertex_shader, NULL);
    glCompileShader(v);
    glGetShaderInfoLog(v, sizeof(msg), NULL, msg);
    SDL_Log("vertex shader info: %s\n", msg);

    /* Compile the fragment shader */
    GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(f, 1, &fragment_shader, NULL);
    glCompileShader(f);
    glGetShaderInfoLog(f, sizeof(msg), NULL, msg);
    SDL_Log("fragment shader info: %s\n", msg);

    // Create and compile our GLSL program from the shaders
    GLuint programID = glCreateProgram();
    glAttachShader(programID, v);
    glAttachShader(programID, f);
    glLinkProgram(programID);
    glGetProgramInfoLog(programID, sizeof msg, NULL, msg);
    SDL_Log("info: %s", msg);

    // Get a handle for our "MVP" uniform
    GLuint MatrixID = glGetUniformLocation(programID, "MVP");

    // Get a handle for our buffers
    GLuint vertexPosition_modelspaceID = glGetAttribLocation(programID, "vertexPosition_modelspace");
    GLuint vertexUVID = glGetAttribLocation(programID, "vertexUV");
    GLuint vertexColorID = glGetAttribLocation(programID, "vertexColor");

    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    glViewport(0, 0, w, h);

    // Projection matrix : 45° Field of View, 4:3 ratio, display range : 0.1 unit <-> 100 units
    glm::mat4 Projection = glm::perspective(45.0f, 4.0f / 3.0f, 0.1f, 100.0f);
    // Camera matrix
    glm::mat4 View = glm::lookAt(
        glm::vec3(4,3,3), // Camera is at (4,3,3), in World Space
        glm::vec3(0,0,0), // and looks at the origin
        glm::vec3(0,1,0)  // Head is up (set to 0,-1,0 to look upside-down)
    );
    // Model matrix : an identity matrix (model will be at the origin)
    glm::mat4 Model = glm::mat4(1.0f);
    // Our ModelViewProjection : multiplication of our 3 matrices
    glm::mat4 MVP = Projection * View * Model; // Remember, matrix multiplication is the other way around

    // Load the texture
    GLuint Texture = textures_load(grid512_png);

    // Get a handle for our "myTextureSampler" uniform
    GLuint TextureID  = glGetUniformLocation(programID, "myTextureSampler");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Our vertices. Tree consecutive floats give a 3D vertex; Three consecutive vertices give a triangle.
    // A cube has 6 faces with 2 triangles each, so this makes 6*2=12 triangles, and 12*3 vertices
    static const GLfloat g_vertex_buffer_data[] = {
        -1.0f,-1.0f,-1.0f,
        -1.0f,-1.0f, 1.0f,
        -1.0f, 1.0f, 1.0f,
        1.0f, 1.0f,-1.0f,
        -1.0f,-1.0f,-1.0f,
        -1.0f, 1.0f,-1.0f,
        1.0f,-1.0f, 1.0f,
        -1.0f,-1.0f,-1.0f,
        1.0f,-1.0f,-1.0f,
        1.0f, 1.0f,-1.0f,
        1.0f,-1.0f,-1.0f,
        -1.0f,-1.0f,-1.0f,
        -1.0f,-1.0f,-1.0f,
        -1.0f, 1.0f, 1.0f,
        -1.0f, 1.0f,-1.0f,
        1.0f,-1.0f, 1.0f,
        -1.0f,-1.0f, 1.0f,
        -1.0f,-1.0f,-1.0f,
        -1.0f, 1.0f, 1.0f,
        -1.0f,-1.0f, 1.0f,
        1.0f,-1.0f, 1.0f,
        1.0f, 1.0f, 1.0f,
        1.0f,-1.0f,-1.0f,
        1.0f, 1.0f,-1.0f,
        1.0f,-1.0f,-1.0f,
        1.0f, 1.0f, 1.0f,
        1.0f,-1.0f, 1.0f,
        1.0f, 1.0f, 1.0f,
        1.0f, 1.0f,-1.0f,
        -1.0f, 1.0f,-1.0f,
        1.0f, 1.0f, 1.0f,
        -1.0f, 1.0f,-1.0f,
        -1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f,
        -1.0f, 1.0f, 1.0f,
        1.0f,-1.0f, 1.0f
    };

    // Two UV coordinatesfor each vertex. They were created with Blender.
    static const GLfloat g_uv_buffer_data[] = {
        0.000059f, 1.0f-0.000004f,
        0.000103f, 1.0f-0.336048f,
        0.335973f, 1.0f-0.335903f,
        1.000023f, 1.0f-0.000013f,
        0.667979f, 1.0f-0.335851f,
        0.999958f, 1.0f-0.336064f,
        0.667979f, 1.0f-0.335851f,
        0.336024f, 1.0f-0.671877f,
        0.667969f, 1.0f-0.671889f,
        1.000023f, 1.0f-0.000013f,
        0.668104f, 1.0f-0.000013f,
        0.667979f, 1.0f-0.335851f,
        0.000059f, 1.0f-0.000004f,
        0.335973f, 1.0f-0.335903f,
        0.336098f, 1.0f-0.000071f,
        0.667979f, 1.0f-0.335851f,
        0.335973f, 1.0f-0.335903f,
        0.336024f, 1.0f-0.671877f,
        1.000004f, 1.0f-0.671847f,
        0.999958f, 1.0f-0.336064f,
        0.667979f, 1.0f-0.335851f,
        0.668104f, 1.0f-0.000013f,
        0.335973f, 1.0f-0.335903f,
        0.667979f, 1.0f-0.335851f,
        0.335973f, 1.0f-0.335903f,
        0.668104f, 1.0f-0.000013f,
        0.336098f, 1.0f-0.000071f,
        0.000103f, 1.0f-0.336048f,
        0.000004f, 1.0f-0.671870f,
        0.336024f, 1.0f-0.671877f,
        0.000103f, 1.0f-0.336048f,
        0.336024f, 1.0f-0.671877f,
        0.335973f, 1.0f-0.335903f,
        0.667969f, 1.0f-0.671889f,
        1.000004f, 1.0f-0.671847f,
        0.667979f, 1.0f-0.335851f
    };

    GLuint vertexbuffer;
    glGenBuffers(1, &vertexbuffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertexbuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_vertex_buffer_data), g_vertex_buffer_data, GL_STATIC_DRAW);

    GLuint uvbuffer;
    glGenBuffers(1, &uvbuffer);
    glBindBuffer(GL_ARRAY_BUFFER, uvbuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_uv_buffer_data), g_uv_buffer_data, GL_STATIC_DRAW);

    float t = SDL_GetTicks() / 1000.0;
    do {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            process_event(&event);
        }

        float now = SDL_GetTicks() / 1000.0;
        float dt = now - t;

        // Clear the screen
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        if (clip_enabled) {
            /* Add a clipping plane that rotates around the cube */
            GLdouble clip_plane[4] = { sin(dt), cos(dt), 0.2, 0.1 };
            glClipPlane(GL_CLIP_PLANE0, clip_plane);
            glEnable(GL_CLIP_PLANE0);
        } else {
            glDisable(GL_CLIP_PLANE0);
        }

        // Use our shader
        glUseProgram(programID);

        // Bind our texture in Texture Unit 0
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, Texture);
        // Set our "myTextureSampler" sampler to user Texture Unit 0
        glUniform1i(TextureID, 0);

        // 1st attribute buffer : vertices
        glEnableVertexAttribArray(vertexPosition_modelspaceID);
        glBindBuffer(GL_ARRAY_BUFFER, vertexbuffer);
        glVertexAttribPointer(
            vertexPosition_modelspaceID,  // The attribute we want to configure
            3,                            // size
            GL_FLOAT,                     // type
            GL_FALSE,                     // normalized?
            0,                            // stride
            (void*)0                      // array buffer offset
        );

        // 2nd attribute buffer : UVs
        glEnableVertexAttribArray(vertexUVID);
        glBindBuffer(GL_ARRAY_BUFFER, uvbuffer);
        glVertexAttribPointer(
            vertexUVID,                   // The attribute we want to configure
            2,                            // size : U+V => 2
            GL_FLOAT,                     // type
            GL_FALSE,                     // normalized?
            0,                            // stride
            (void*)0                      // array buffer offset
        );

        /* This could have been simply a uniform, but we want to test the
         * glVertexAttrib*() calls too. */
        glVertexAttrib4f(vertexColorID,
                         0.5 + sinf(dt) / 2,
                         0.5 + cosf(dt) / 2,
                         0.5 + sinf(dt + M_PI) / 2,
                         1.0);

        if (stencil_enabled) {
            /* Create the stencil buffer */
            glEnable(GL_STENCIL_TEST);
            glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
            glStencilFunc(GL_ALWAYS, 1, 1);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glDepthMask(GL_FALSE);
            /* Draw our cube with a different model matrix, so that it won't
             * completely match the cube drawn below. */
            glm::mat4 stencilModel = glm::scale(Model, glm::vec3(0.5 + sinf(dt) / 2,
                                                                 0.5 + cosf(dt) / 2,
                                                                 1.0));
            glm::mat4 stencilMVP = Projection * View * stencilModel;
            glUniformMatrix4fv(MatrixID, 1, GL_FALSE, &stencilMVP[0][0]);
            glDrawArrays(GL_TRIANGLES, 0, 12*3);
            glStencilFunc(GL_EQUAL, 1, 1);  /* draw if ==1 */
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(GL_TRUE);
        } else {
            glDisable(GL_STENCIL_TEST);
        }

        // Send our transformation to the currently bound shader,
        // in the "MVP" uniform
        glUniformMatrix4fv(MatrixID, 1, GL_FALSE, &MVP[0][0]);

        // Draw the triangles !
        glDrawArrays(GL_TRIANGLES, 0, 12*3); // 12*3 indices starting at 0 -> 12 triangles

        glDisableVertexAttribArray(vertexPosition_modelspaceID);
        glDisableVertexAttribArray(vertexUVID);

        SDL_GL_SwapWindow(window);
    } while (!done);

    // Cleanup VBO and shader
    glDeleteBuffers(1, &vertexbuffer);
    glDeleteBuffers(1, &uvbuffer);
    glDeleteProgram(programID);
    glDeleteTextures(1, &TextureID);

    return EXIT_SUCCESS;
}
