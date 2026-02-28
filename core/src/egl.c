#include "egl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

static const EGLint ctx_attribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE,
};

int egl_init(EglState *e, DrmState *drm) {
    memset(e, 0, sizeof(*e));

    /* Use eglGetPlatformDisplayEXT if available, fall back to eglGetDisplay */
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");

    if (get_platform_display) {
        e->display = get_platform_display(EGL_PLATFORM_GBM_KHR, drm->gbm_dev, NULL);
    } else {
        e->display = eglGetDisplay((EGLNativeDisplayType)drm->gbm_dev);
    }

    if (e->display == EGL_NO_DISPLAY) {
        fprintf(stderr, "egl: eglGetDisplay failed\n");
        return -1;
    }

    EGLint major, minor;
    if (!eglInitialize(e->display, &major, &minor)) {
        fprintf(stderr, "egl: eglInitialize failed\n");
        return -1;
    }
    fprintf(stderr, "egl: EGL %d.%d\n", major, minor);

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "egl: eglBindAPI failed\n");
        return -1;
    }

    /* Find a config matching our GBM format (ARGB8888) */
    EGLint n;
    EGLint cfg_attribs[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_NONE,
    };

    EGLConfig configs[64];
    if (!eglChooseConfig(e->display, cfg_attribs, configs, 64, &n) || n == 0) {
        fprintf(stderr, "egl: eglChooseConfig failed\n");
        return -1;
    }

    /* Pick config whose EGL_NATIVE_VISUAL_ID matches GBM_FORMAT_ARGB8888 */
    e->config = configs[0];
    for (int i = 0; i < n; i++) {
        EGLint vis_id;
        eglGetConfigAttrib(e->display, configs[i], EGL_NATIVE_VISUAL_ID, &vis_id);
        if ((uint32_t)vis_id == GBM_FORMAT_ARGB8888) {
            e->config = configs[i];
            break;
        }
    }

    e->context = eglCreateContext(e->display, e->config, EGL_NO_CONTEXT, ctx_attribs);
    if (e->context == EGL_NO_CONTEXT) {
        fprintf(stderr, "egl: eglCreateContext failed: 0x%x\n", eglGetError());
        return -1;
    }

    e->surface = eglCreateWindowSurface(e->display, e->config,
                                         (EGLNativeWindowType)drm->gbm_surf, NULL);
    if (e->surface == EGL_NO_SURFACE) {
        fprintf(stderr, "egl: eglCreateWindowSurface failed: 0x%x\n", eglGetError());
        return -1;
    }

    if (!eglMakeCurrent(e->display, e->surface, e->surface, e->context)) {
        fprintf(stderr, "egl: eglMakeCurrent failed: 0x%x\n", eglGetError());
        return -1;
    }

    fprintf(stderr, "egl: GL_VERSION=%s\n", glGetString(GL_VERSION));
    return 0;
}

void egl_swap(EglState *e, DrmState *drm) {
    eglSwapBuffers(e->display, e->surface);

    struct gbm_bo *bo = gbm_surface_lock_front_buffer(drm->gbm_surf);
    if (!bo) return;

    uint32_t fb_id = 0;
    if (drm_fb_from_bo(drm, bo, &fb_id) < 0) {
        gbm_surface_release_buffer(drm->gbm_surf, bo);
        return;
    }

    if (drm->prev_bo == NULL) {
        /* First frame — set CRTC directly */
        drmModeSetCrtc(drm->fd, drm->crtc_id, fb_id, 0, 0,
                        &drm->connector_id, 1, &drm->mode);
        drm->prev_bo = bo;
        drm->prev_fb = fb_id;
    } else if (!drm->flip_pending) {
        /* Schedule page flip */
        if (drm_queue_flip(drm, fb_id) == 0) {
            /* prev_bo/prev_fb will be released in the flip callback */
            struct gbm_bo *old_bo = drm->prev_bo;
            uint32_t old_fb = drm->prev_fb;
            drm->prev_bo = bo;
            drm->prev_fb = fb_id;
            /* The old ones are released inside page_flip_cb */
            (void)old_bo; (void)old_fb;
        } else {
            drmModeRmFB(drm->fd, fb_id);
            gbm_surface_release_buffer(drm->gbm_surf, bo);
        }
    } else {
        /* Flip still pending — drop this frame */
        drmModeRmFB(drm->fd, fb_id);
        gbm_surface_release_buffer(drm->gbm_surf, bo);
    }
}

void *egl_get_proc_address(void *ctx, const char *name) {
    (void)ctx;
    return (void *)eglGetProcAddress(name);
}

void egl_destroy(EglState *e) {
    if (e->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(e->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (e->surface != EGL_NO_SURFACE) eglDestroySurface(e->display, e->surface);
        if (e->context != EGL_NO_CONTEXT) eglDestroyContext(e->display, e->context);
        eglTerminate(e->display);
    }
}
