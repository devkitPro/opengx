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

#define GL_GLEXT_PROTOTYPES 1
#include "opengx.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    void *address;
} ProcMap;

int _ogx_functions_c = 0; /* referenced by gc_gl.c, see the comment in there */

#define PROC(name) { #name, name }
static const ProcMap s_proc_map[] = {
    PROC(glAccum),
    PROC(glActiveTexture), /* OpenGL 1.3 */
    PROC(glAlphaFunc),
    //PROC(glAreTexturesResident),
    PROC(glArrayElement),
    PROC(glBegin),
    PROC(glBegin),
    //PROC(glBeginQuery), /* OpenGL 1.5 */
    PROC(glBindBuffer), /* OpenGL 1.5 */
    PROC(glBindTexture),
    PROC(glBitmap),
    PROC(glBlendFunc),
    PROC(glBufferData), /* OpenGL 1.5 */
    PROC(glBufferSubData), /* OpenGL 1.5 */
    PROC(glCallList),
    PROC(glCallLists),
    PROC(glClear),
    PROC(glClearAccum),
    PROC(glClearColor),
    PROC(glClearDepth),
    //PROC(glClearIndex),
    PROC(glClearStencil),
    PROC(glClientActiveTexture), /* OpenGL 1.3 */
    PROC(glClipPlane),
    PROC(glColor3b),
    PROC(glColor3bv),
    PROC(glColor3d),
    PROC(glColor3dv),
    PROC(glColor3f),
    PROC(glColor3fv),
    PROC(glColor3i),
    PROC(glColor3iv),
    PROC(glColor3s),
    PROC(glColor3sv),
    PROC(glColor3ub),
    PROC(glColor3ubv),
    PROC(glColor3ui),
    PROC(glColor3uiv),
    PROC(glColor3us),
    PROC(glColor3usv),
    PROC(glColor4b),
    PROC(glColor4bv),
    PROC(glColor4d),
    PROC(glColor4dv),
    PROC(glColor4f),
    PROC(glColor4fv),
    PROC(glColor4i),
    PROC(glColor4iv),
    PROC(glColor4s),
    PROC(glColor4sv),
    PROC(glColor4ub),
    PROC(glColor4ubv),
    PROC(glColor4ui),
    PROC(glColor4uiv),
    PROC(glColor4us),
    PROC(glColor4usv),
    PROC(glColorMask),
    PROC(glColorMaterial),
    PROC(glColorPointer),
    PROC(glCopyPixels),
    //PROC(glCopyTexImage1D),
    //PROC(glCopyTexImage2D),
    //PROC(glCopyTexSubImage1D),
    //PROC(glCopyTexSubImage2D),
    PROC(glCullFace),
    PROC(glDeleteBuffers), /* OpenGL 1.5 */
    PROC(glDeleteLists),
    //PROC(glDeleteQueries), /* OpenGL 1.5 */
    PROC(glDeleteTextures),
    PROC(glDepthFunc),
    PROC(glDepthMask),
    PROC(glDepthRange),
    PROC(glDisable),
    PROC(glDisableClientState),
    PROC(glDrawArrays),
    PROC(glDrawBuffer),
    PROC(glDrawElements),
    PROC(glDrawPixels),
    //PROC(glEdgeFlag),
    //PROC(glEdgeFlagPointer),
    //PROC(glEdgeFlagv),
    PROC(glEnable),
    PROC(glEnableClientState),
    PROC(glEnd),
    PROC(glEndList),
    //PROC(glEndQuery), /* OpenGL 1.5 */
    //PROC(glEvalCoord1d),
    //PROC(glEvalCoord1dv),
    //PROC(glEvalCoord1f),
    //PROC(glEvalCoord1fv),
    //PROC(glEvalCoord2d),
    //PROC(glEvalCoord2dv),
    //PROC(glEvalCoord2f),
    //PROC(glEvalCoord2fv),
    //PROC(glEvalMesh1),
    //PROC(glEvalMesh2),
    //PROC(glEvalPoint1),
    //PROC(glEvalPoint2),
    //PROC(glFeedbackBuffer),
    PROC(glFinish),
    PROC(glFlush),
    PROC(glFogf),
    PROC(glFogfv),
    PROC(glFogi),
    //PROC(glFogiv),
    PROC(glFrontFace),
    PROC(glFrustum),
    PROC(glGenBuffers), /* OpenGL 1.5 */
    PROC(glGenLists),
    //PROC(glGenQueries), /* OpenGL 1.5 */
    PROC(glGenTextures),
    //PROC(glGetBooleanv),
    PROC(glGetBufferParameteriv), /* OpenGL 1.5 */
    PROC(glGetBufferPointerv), /* OpenGL 1.5 */
    PROC(glGetBufferSubData), /* OpenGL 1.5 */
    //PROC(glGetClipPlane),
    PROC(glGetDoublev),
    PROC(glGetError),
    PROC(glGetFloatv),
    PROC(glGetIntegerv),
    //PROC(glGetLightfv),
    //PROC(glGetLightiv),
    //PROC(glGetMapdv),
    //PROC(glGetMapfv),
    //PROC(glGetMapiv),
    //PROC(glGetMaterialfv),
    //PROC(glGetMaterialiv),
    PROC(glGetPixelMapfv),
    PROC(glGetPixelMapuiv),
    PROC(glGetPixelMapusv),
    //PROC(glGetPointerv),
    //PROC(glGetPolygonStipple),
    //PROC(glGetQueryObjectiv), /* OpenGL 1.5 */
    //PROC(glGetQueryObjectuiv), /* OpenGL 1.5 */
    //PROC(glGetQueryiv), /* OpenGL 1.5 */
    PROC(glGetString),
    //PROC(glGetTexEnvfv),
    //PROC(glGetTexEnviv),
    //PROC(glGetTexGendv),
    //PROC(glGetTexGenfv),
    //PROC(glGetTexGeniv),
    //PROC(glGetTexImage),
    //PROC(glGetTexLevelParameterfv),
    PROC(glGetTexLevelParameteriv),
    //PROC(glGetTexParameterfv),
    //PROC(glGetTexParameteriv),
    PROC(glHint),
    //PROC(glIndexMask),
    //PROC(glIndexPointer),
    //PROC(glIndexd),
    //PROC(glIndexdv),
    //PROC(glIndexf),
    //PROC(glIndexfv),
    //PROC(glIndexi),
    //PROC(glIndexiv),
    //PROC(glIndexs),
    //PROC(glIndexsv),
    //PROC(glIndexub),
    //PROC(glIndexubv),
    PROC(glInitNames),
    PROC(glInterleavedArrays),
    PROC(glIsBuffer), /* OpenGL 1.5 */
    PROC(glIsEnabled),
    PROC(glIsList),
    //PROC(glIsQuery), /* OpenGL 1.5 */
    //PROC(glIsTexture),
    PROC(glLightModelf),
    PROC(glLightModelfv),
    PROC(glLightModeli),
    //PROC(glLightModeliv),
    PROC(glLightf),
    PROC(glLightfv),
    //PROC(glLighti),
    //PROC(glLightiv),
    PROC(glLineStipple),
    PROC(glLineWidth),
    //PROC(glListBase),
    PROC(glLoadIdentity),
    //PROC(glLoadMatrixd),
    PROC(glLoadMatrixf),
    PROC(glLoadName),
    //PROC(glLogicOp),
    //PROC(glMap1d),
    //PROC(glMap1f),
    //PROC(glMap2d),
    //PROC(glMap2f),
    PROC(glMapBuffer), /* OpenGL 1.5 */
    //PROC(glMapGrid1d),
    //PROC(glMapGrid1f),
    //PROC(glMapGrid2d),
    //PROC(glMapGrid2f),
    PROC(glMaterialf),
    PROC(glMaterialfv),
    //PROC(glMateriali),
    //PROC(glMaterialiv),
    PROC(glMatrixMode),
    PROC(glMultMatrixd),
    PROC(glMultMatrixf),
    PROC(glMultiTexCoord1d), /* OpenGL 1.3 */
    PROC(glMultiTexCoord1dv), /* OpenGL 1.3 */
    PROC(glMultiTexCoord1f), /* OpenGL 1.3 */
    PROC(glMultiTexCoord1fv), /* OpenGL 1.3 */
    PROC(glMultiTexCoord1i), /* OpenGL 1.3 */
    PROC(glMultiTexCoord1iv), /* OpenGL 1.3 */
    PROC(glMultiTexCoord1s), /* OpenGL 1.3 */
    PROC(glMultiTexCoord1sv), /* OpenGL 1.3 */
    PROC(glMultiTexCoord2d), /* OpenGL 1.3 */
    PROC(glMultiTexCoord2dv), /* OpenGL 1.3 */
    PROC(glMultiTexCoord2f), /* OpenGL 1.3 */
    PROC(glMultiTexCoord2fv), /* OpenGL 1.3 */
    PROC(glMultiTexCoord2i), /* OpenGL 1.3 */
    PROC(glMultiTexCoord2iv), /* OpenGL 1.3 */
    PROC(glMultiTexCoord2s), /* OpenGL 1.3 */
    PROC(glMultiTexCoord2sv), /* OpenGL 1.3 */
    PROC(glMultiTexCoord3d), /* OpenGL 1.3 */
    PROC(glMultiTexCoord3dv), /* OpenGL 1.3 */
    PROC(glMultiTexCoord3f), /* OpenGL 1.3 */
    PROC(glMultiTexCoord3fv), /* OpenGL 1.3 */
    PROC(glMultiTexCoord3i), /* OpenGL 1.3 */
    PROC(glMultiTexCoord3iv), /* OpenGL 1.3 */
    PROC(glMultiTexCoord3s), /* OpenGL 1.3 */
    PROC(glMultiTexCoord3sv), /* OpenGL 1.3 */
    PROC(glMultiTexCoord4d), /* OpenGL 1.3 */
    PROC(glMultiTexCoord4dv), /* OpenGL 1.3 */
    PROC(glMultiTexCoord4f), /* OpenGL 1.3 */
    PROC(glMultiTexCoord4fv), /* OpenGL 1.3 */
    PROC(glMultiTexCoord4i), /* OpenGL 1.3 */
    PROC(glMultiTexCoord4iv), /* OpenGL 1.3 */
    PROC(glMultiTexCoord4s), /* OpenGL 1.3 */
    PROC(glMultiTexCoord4sv), /* OpenGL 1.3 */
    PROC(glNewList),
    PROC(glNormal3b),
    PROC(glNormal3bv),
    PROC(glNormal3d),
    PROC(glNormal3dv),
    PROC(glNormal3f),
    PROC(glNormal3fv),
    PROC(glNormal3i),
    PROC(glNormal3iv),
    PROC(glNormal3s),
    PROC(glNormal3sv),
    PROC(glNormalPointer),
    PROC(glOrtho),
    //PROC(glPassThrough),
    PROC(glPixelMapfv),
    PROC(glPixelMapuiv),
    PROC(glPixelMapusv),
    PROC(glPixelStoref),
    PROC(glPixelStorei),
    //PROC(glPixelTransferf),
    //PROC(glPixelTransferi),
    PROC(glPixelZoom),
    PROC(glPointSize),
    PROC(glPolygonMode),
    PROC(glPolygonOffset),
    PROC(glPolygonStipple),
    PROC(glPopAttrib),
    PROC(glPopClientAttrib),
    PROC(glPopMatrix),
    PROC(glPopName),
    //PROC(glPrioritizeTextures),
    PROC(glPushAttrib),
    PROC(glPushClientAttrib),
    PROC(glPushMatrix),
    PROC(glPushName),
    PROC(glRasterPos2d),
    PROC(glRasterPos2dv),
    PROC(glRasterPos2f),
    PROC(glRasterPos2fv),
    PROC(glRasterPos2i),
    PROC(glRasterPos2iv),
    PROC(glRasterPos2s),
    PROC(glRasterPos2sv),
    PROC(glRasterPos3d),
    PROC(glRasterPos3dv),
    PROC(glRasterPos3f),
    PROC(glRasterPos3fv),
    PROC(glRasterPos3i),
    PROC(glRasterPos3iv),
    PROC(glRasterPos3s),
    PROC(glRasterPos3sv),
    PROC(glRasterPos4d),
    PROC(glRasterPos4dv),
    PROC(glRasterPos4f),
    PROC(glRasterPos4fv),
    PROC(glRasterPos4i),
    PROC(glRasterPos4iv),
    PROC(glRasterPos4s),
    PROC(glRasterPos4sv),
    PROC(glReadBuffer),
    PROC(glReadPixels),
    PROC(glRectd),
    PROC(glRectdv),
    PROC(glRectf),
    PROC(glRectfv),
    PROC(glRecti),
    PROC(glRectiv),
    PROC(glRects),
    PROC(glRectsv),
    PROC(glRenderMode),
    PROC(glRotated),
    PROC(glRotatef),
    PROC(glScaled),
    PROC(glScalef),
    PROC(glScissor),
    PROC(glSelectBuffer),
    PROC(glShadeModel),
    PROC(glStencilFunc),
    PROC(glStencilMask),
    PROC(glStencilOp),
    PROC(glTexCoord1d),
    PROC(glTexCoord1dv),
    PROC(glTexCoord1f),
    PROC(glTexCoord1fv),
    PROC(glTexCoord1i),
    PROC(glTexCoord1iv),
    PROC(glTexCoord1s),
    PROC(glTexCoord1sv),
    PROC(glTexCoord2d),
    PROC(glTexCoord2dv),
    PROC(glTexCoord2f),
    PROC(glTexCoord2fv),
    PROC(glTexCoord2i),
    PROC(glTexCoord2iv),
    PROC(glTexCoord2s),
    PROC(glTexCoord2sv),
    PROC(glTexCoord3d),
    PROC(glTexCoord3dv),
    PROC(glTexCoord3f),
    PROC(glTexCoord3fv),
    PROC(glTexCoord3i),
    PROC(glTexCoord3iv),
    PROC(glTexCoord3s),
    PROC(glTexCoord3sv),
    PROC(glTexCoord4d),
    PROC(glTexCoord4dv),
    PROC(glTexCoord4f),
    PROC(glTexCoord4fv),
    PROC(glTexCoord4i),
    PROC(glTexCoord4iv),
    PROC(glTexCoord4s),
    PROC(glTexCoord4sv),
    PROC(glTexCoordPointer),
    PROC(glTexEnvf),
    PROC(glTexEnvfv),
    PROC(glTexEnvi),
    PROC(glTexEnviv),
    PROC(glTexGend),
    PROC(glTexGendv),
    PROC(glTexGenf),
    PROC(glTexGenfv),
    PROC(glTexGeni),
    PROC(glTexGeniv),
    PROC(glTexImage1D),
    PROC(glTexImage2D),
    PROC(glTexParameterf),
    PROC(glTexParameterfv),
    PROC(glTexParameteri),
    PROC(glTexParameteriv),
    //PROC(glTexSubImage1D),
    PROC(glTexSubImage2D),
    PROC(glTranslated),
    PROC(glTranslatef),
    PROC(glUnmapBuffer), /* OpenGL 1.5 */
    PROC(glVertex2d),
    PROC(glVertex2dv),
    PROC(glVertex2f),
    PROC(glVertex2fv),
    PROC(glVertex2i),
    PROC(glVertex2iv),
    PROC(glVertex2s),
    PROC(glVertex2sv),
    PROC(glVertex3d),
    PROC(glVertex3dv),
    PROC(glVertex3f),
    PROC(glVertex3fv),
    PROC(glVertex3i),
    PROC(glVertex3iv),
    PROC(glVertex3s),
    PROC(glVertex3sv),
    PROC(glVertex4d),
    PROC(glVertex4dv),
    PROC(glVertex4f),
    PROC(glVertex4fv),
    PROC(glVertex4i),
    PROC(glVertex4iv),
    PROC(glVertex4s),
    PROC(glVertex4sv),
    PROC(glVertexPointer),
    PROC(glViewport),
};
#define NUM_PROCS (sizeof(s_proc_map) / sizeof(s_proc_map[0]))

static int compare_proc(const void *a_ptr, const void *b_ptr)
{
    const ProcMap *a = a_ptr;
    const ProcMap *b = b_ptr;
    return strcmp(a->name, b->name);
}

void *ogx_get_proc_address(const char *proc)
{
    char buffer[64];
    size_t len;

    /* If the string ends with the "ARB" suffix, remove it */
    len = strlen(proc);
    if (len > 3 && strcmp(proc + len - 3, "ARB") == 0) {
        strncpy(buffer, proc, len - 3);
        buffer[len - 3] = '\0';
        proc = buffer;
    }

    ProcMap search = { proc, NULL };
    ProcMap *elem = bsearch(&search, s_proc_map, NUM_PROCS, sizeof(ProcMap),
                            compare_proc);
    return elem ? elem->address : NULL;
}
