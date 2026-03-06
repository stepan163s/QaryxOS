#pragma once
#include "drm.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

typedef struct {
    EGLDisplay  display;
    EGLContext  context;
    EGLSurface  surface;
    EGLConfig   config;
} EglState;

/* Initialise EGL: create display, context, surface. Does NOT call eglMakeCurrent.
   Call egl_make_current() on whichever thread will own the GL context. */
int  egl_init(EglState *e, DrmState *drm);

/* Bind the EGL context to the calling thread. Must be called once from the
   render thread before any GL or mpv render calls. */
int  egl_make_current(EglState *e);

/* eglSwapBuffers → lock front GBM buffer → create DRM FB → queue page flip.
   Call after rendering each frame. */
void egl_swap(EglState *e, DrmState *drm);

/* Return the OpenGL proc address (used by libmpv get_proc_address). */
void *egl_get_proc_address(void *ctx, const char *name);

void egl_destroy(EglState *e);
