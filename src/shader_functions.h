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

#ifndef INCLUDED_FROM_SHADER_C
#error "This file can only be included by shader.c"
#endif

#define PROC(name) { #name, name }
static const OgxProcMap s_proc_map[] = {
    PROC(glAttachShader),
    PROC(glBindAttribLocation),
    PROC(glCompileShader),
    PROC(glCreateProgram),
    PROC(glCreateShader),
    PROC(glDeleteProgram),
    PROC(glDeleteShader),
    PROC(glDetachShader),
    PROC(glDisableVertexAttribArray),
    PROC(glEnableVertexAttribArray),
    PROC(glGetActiveAttrib),
    PROC(glGetActiveUniform),
    PROC(glGetAttachedShaders),
    PROC(glGetAttribLocation),
    PROC(glGetProgramInfoLog),
    PROC(glGetProgramiv),
    PROC(glGetShaderInfoLog),
    PROC(glGetShaderSource),
    PROC(glGetShaderiv),
    PROC(glGetUniformLocation),
    PROC(glGetUniformfv),
    PROC(glGetUniformiv),
    PROC(glGetVertexAttribPointerv),
    PROC(glGetVertexAttribiv),
    PROC(glIsProgram),
    PROC(glIsShader),
    PROC(glLinkProgram),
    PROC(glShaderSource),
    PROC(glUniform1f),
    PROC(glUniform1fv),
    PROC(glUniform1i),
    PROC(glUniform1iv),
    PROC(glUniform2f),
    PROC(glUniform2fv),
    PROC(glUniform2i),
    PROC(glUniform2iv),
    PROC(glUniform3f),
    PROC(glUniform3fv),
    PROC(glUniform3i),
    PROC(glUniform3iv),
    PROC(glUniform4f),
    PROC(glUniform4fv),
    PROC(glUniform4i),
    PROC(glUniform4iv),
    PROC(glUniformMatrix2fv),
    PROC(glUniformMatrix3fv),
    PROC(glUniformMatrix4fv),
    PROC(glUseProgram),
    PROC(glValidateProgram),
    PROC(glVertexAttrib1d),
    PROC(glVertexAttrib1dv),
    PROC(glVertexAttrib1f),
    PROC(glVertexAttrib1fv),
    PROC(glVertexAttrib1s),
    PROC(glVertexAttrib1sv),
    PROC(glVertexAttrib2d),
    PROC(glVertexAttrib2dv),
    PROC(glVertexAttrib2f),
    PROC(glVertexAttrib2fv),
    PROC(glVertexAttrib2s),
    PROC(glVertexAttrib2sv),
    PROC(glVertexAttrib3d),
    PROC(glVertexAttrib3dv),
    PROC(glVertexAttrib3f),
    PROC(glVertexAttrib3fv),
    PROC(glVertexAttrib3s),
    PROC(glVertexAttrib3sv),
    PROC(glVertexAttrib4Nbv),
    PROC(glVertexAttrib4Niv),
    PROC(glVertexAttrib4Nsv),
    PROC(glVertexAttrib4Nub),
    PROC(glVertexAttrib4Nubv),
    PROC(glVertexAttrib4Nuiv),
    PROC(glVertexAttrib4Nusv),
    PROC(glVertexAttrib4bv),
    PROC(glVertexAttrib4d),
    PROC(glVertexAttrib4dv),
    PROC(glVertexAttrib4f),
    PROC(glVertexAttrib4fv),
    PROC(glVertexAttrib4iv),
    PROC(glVertexAttrib4s),
    PROC(glVertexAttrib4sv),
    PROC(glVertexAttrib4ubv),
    PROC(glVertexAttrib4uiv),
    PROC(glVertexAttrib4usv),
    PROC(glVertexAttribPointer),
};
#define NUM_PROCS (sizeof(s_proc_map) / sizeof(s_proc_map[0]))

OgxFunctions _ogx_shader_functions = {
    NUM_PROCS,
    s_proc_map,
};
