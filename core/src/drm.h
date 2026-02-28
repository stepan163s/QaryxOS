#pragma once
#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>

typedef struct {
    int              fd;
    uint32_t         crtc_id;
    uint32_t         connector_id;
    uint32_t         plane_id;
    drmModeModeInfo  mode;
    struct gbm_device   *gbm_dev;
    struct gbm_surface  *gbm_surf;
    struct gbm_bo       *prev_bo;
    uint32_t             prev_fb;
    int                  flip_pending;
} DrmState;

/* Open /dev/dri/card0, find HDMI connector + preferred 1080p mode,
   create GBM device + surface.
   Returns 0 on success, -1 on error. */
int  drm_init(DrmState *s);

/* Create a DRM framebuffer from a GBM buffer object.
   Stores fb_id into *fb_id. Returns 0 on success. */
int  drm_fb_from_bo(DrmState *s, struct gbm_bo *bo, uint32_t *fb_id);

/* Perform the initial drmModeSetCrtc (called once after first eglSwapBuffers). */
int  drm_set_crtc(DrmState *s);

/* Queue page flip. Sets flip_pending=1.
   The DRM fd becomes readable when done; call drm_handle_flip_event(). */
int  drm_queue_flip(DrmState *s, uint32_t fb_id);

/* Read pending DRM event (call when epoll says drm fd is readable). */
void drm_handle_flip_event(DrmState *s);

void drm_destroy(DrmState *s);
