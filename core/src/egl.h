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

/* Initialise EGL on top of the GBM device/surface created by drm_init().
   Shares the EGL context with libmpv. Returns 0 on success. */
int  egl_init(EglState *e, DrmState *drm);

/* eglSwapBuffers → lock front GBM buffer → create DRM FB → queue page flip.
   Call after rendering each frame. */
void egl_swap(EglState *e, DrmState *drm);

/* Return the OpenGL proc address (used by libmpv get_proc_address). */
void *egl_get_proc_address(void *ctx, const char *name);

void egl_destroy(EglState *e);
