#include "drm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define DRM_DEV "/dev/dri/card0"

static void page_flip_cb(int fd, unsigned int seq, unsigned int tv_sec,
                          unsigned int tv_usec, void *data) {
    (void)fd; (void)seq; (void)tv_sec; (void)tv_usec;
    DrmState *s = data;
    s->flip_pending = 0;

    /* Release the previous BO now that the new one is on screen */
    if (s->prev_bo) {
        drmModeRmFB(s->fd, s->prev_fb);
        gbm_surface_release_buffer(s->gbm_surf, s->prev_bo);
        s->prev_bo = NULL;
        s->prev_fb = 0;
    }
}

int drm_init(DrmState *s) {
    memset(s, 0, sizeof(*s));

    s->fd = open(DRM_DEV, O_RDWR | O_CLOEXEC);
    if (s->fd < 0) {
        perror("drm: open " DRM_DEV);
        return -1;
    }

    drmSetClientCap(s->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    drmSetClientCap(s->fd, DRM_CLIENT_CAP_ATOMIC, 1);

    drmModeRes *res = drmModeGetResources(s->fd);
    if (!res) {
        fprintf(stderr, "drm: drmModeGetResources failed\n");
        return -1;
    }

    /* Find the first connected HDMI connector */
    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors && !conn; i++) {
        drmModeConnector *c = drmModeGetConnector(s->fd, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED &&
            c->connector_type == DRM_MODE_CONNECTOR_HDMIA) {
            conn = c;
            s->connector_id = c->connector_id;
        } else if (c) {
            drmModeFreeConnector(c);
        }
    }
    /* Fallback: first connected connector of any type */
    if (!conn) {
        for (int i = 0; i < res->count_connectors && !conn; i++) {
            drmModeConnector *c = drmModeGetConnector(s->fd, res->connectors[i]);
            if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
                conn = c;
                s->connector_id = c->connector_id;
            } else if (c) {
                drmModeFreeConnector(c);
            }
        }
    }
    if (!conn) {
        fprintf(stderr, "drm: no connected connector found\n");
        drmModeFreeResources(res);
        return -1;
    }

    /* Prefer 1920x1080 @ 60 Hz; otherwise use the first mode */
    s->mode = conn->modes[0];
    for (int i = 0; i < conn->count_modes; i++) {
        drmModeModeInfo *m = &conn->modes[i];
        if (m->hdisplay == 1920 && m->vdisplay == 1080 && m->vrefresh == 60) {
            s->mode = *m;
            break;
        }
    }

    /* Find CRTC for this connector via encoder */
    drmModeEncoder *enc = NULL;
    if (conn->encoder_id)
        enc = drmModeGetEncoder(s->fd, conn->encoder_id);
    if (enc && enc->crtc_id) {
        s->crtc_id = enc->crtc_id;
    } else {
        /* Find any compatible CRTC */
        for (int i = 0; i < res->count_crtcs; i++) {
            if (conn->encoders[0]) {
                drmModeEncoder *e = drmModeGetEncoder(s->fd, conn->encoders[0]);
                if (e && (e->possible_crtcs & (1u << i))) {
                    s->crtc_id = res->crtcs[i];
                    drmModeFreeEncoder(e);
                    break;
                }
                if (e) drmModeFreeEncoder(e);
            }
        }
    }
    if (enc) drmModeFreeEncoder(enc);
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);

    if (!s->crtc_id) {
        fprintf(stderr, "drm: no CRTC found\n");
        return -1;
    }

    /* Create GBM device + surface */
    s->gbm_dev = gbm_create_device(s->fd);
    if (!s->gbm_dev) {
        fprintf(stderr, "drm: gbm_create_device failed\n");
        return -1;
    }

    s->gbm_surf = gbm_surface_create(
        s->gbm_dev,
        s->mode.hdisplay, s->mode.vdisplay,
        GBM_FORMAT_ARGB8888,
        GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!s->gbm_surf) {
        fprintf(stderr, "drm: gbm_surface_create failed\n");
        return -1;
    }

    fprintf(stderr, "drm: %dx%d@%d crtc=%u connector=%u\n",
            s->mode.hdisplay, s->mode.vdisplay, s->mode.vrefresh,
            s->crtc_id, s->connector_id);
    return 0;
}

int drm_fb_from_bo(DrmState *s, struct gbm_bo *bo, uint32_t *fb_id) {
    uint32_t width  = gbm_bo_get_width(bo);
    uint32_t height = gbm_bo_get_height(bo);
    uint32_t stride = gbm_bo_get_stride(bo);
    uint32_t handle = gbm_bo_get_handle(bo).u32;

    uint32_t handles[4] = { handle, 0, 0, 0 };
    uint32_t strides[4] = { stride, 0, 0, 0 };
    uint32_t offsets[4] = { 0, 0, 0, 0 };

    int ret = drmModeAddFB2(s->fd, width, height,
                             DRM_FORMAT_ARGB8888,
                             handles, strides, offsets,
                             fb_id, 0);
    if (ret) {
        fprintf(stderr, "drm: drmModeAddFB2 failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int drm_set_crtc(DrmState *s) {
    struct gbm_bo *bo = gbm_surface_lock_front_buffer(s->gbm_surf);
    if (!bo) {
        fprintf(stderr, "drm: gbm_surface_lock_front_buffer failed\n");
        return -1;
    }

    uint32_t fb_id = 0;
    if (drm_fb_from_bo(s, bo, &fb_id) < 0) {
        gbm_surface_release_buffer(s->gbm_surf, bo);
        return -1;
    }

    int ret = drmModeSetCrtc(s->fd, s->crtc_id, fb_id, 0, 0,
                              &s->connector_id, 1, &s->mode);
    if (ret) {
        fprintf(stderr, "drm: drmModeSetCrtc failed: %s\n", strerror(errno));
        drmModeRmFB(s->fd, fb_id);
        gbm_surface_release_buffer(s->gbm_surf, bo);
        return -1;
    }

    s->prev_bo = bo;
    s->prev_fb = fb_id;
    return 0;
}

int drm_queue_flip(DrmState *s, uint32_t fb_id) {
    int ret = drmModePageFlip(s->fd, s->crtc_id, fb_id,
                               DRM_MODE_PAGE_FLIP_EVENT, s);
    if (ret) {
        fprintf(stderr, "drm: page flip failed: %s\n", strerror(errno));
        return -1;
    }
    s->flip_pending = 1;
    return 0;
}

void drm_handle_flip_event(DrmState *s) {
    drmEventContext ev = {
        .version = DRM_EVENT_CONTEXT_VERSION,
        .page_flip_handler = page_flip_cb,
    };
    drmHandleEvent(s->fd, &ev);
}

void drm_destroy(DrmState *s) {
    if (s->gbm_surf)  gbm_surface_destroy(s->gbm_surf);
    if (s->gbm_dev)   gbm_device_destroy(s->gbm_dev);
    if (s->fd >= 0)   close(s->fd);
}
