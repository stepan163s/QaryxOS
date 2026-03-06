#include "mpv.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>

static mpv_handle          *g_mpv    = NULL;
static mpv_render_context  *g_mpv_gl = NULL;
static atomic_int           g_wants_render = 0;
static atomic_int           g_video_active = 0;  /* 1 when file loaded */

/* Optional render condvar — signalled from mpv's internal thread when a
   new frame is ready. The render thread waits on this instead of polling. */
static pthread_mutex_t *g_render_mu   = NULL;
static pthread_cond_t  *g_render_cond = NULL;

/* ── Non-blocking status cache ─────────────────────────────────────────────
 * Updated from mpv events (MPV_EVENT_PROPERTY_CHANGE) so that
 * mpv_core_get_status() never calls blocking mpv_get_property().
 * Without this, every push_status() call (500ms timer) would block the
 * entire epoll event loop on a live-stream network stall → board freeze. */
static double g_cached_pos    = 0.0;
static double g_cached_dur    = 0.0;
static int    g_cached_vol    = 80;
static int    g_cached_paused = 0;
static char   g_cached_url[512] = "";

static void on_mpv_render_update(void *ctx) {
    (void)ctx;
    atomic_store(&g_wants_render, 1);
    /* Wake render thread immediately (called from mpv internal thread) */
    if (g_render_mu) {
        pthread_mutex_lock(g_render_mu);
        pthread_cond_signal(g_render_cond);
        pthread_mutex_unlock(g_render_mu);
    }
}

void mpv_core_set_render_cond(pthread_mutex_t *mu, pthread_cond_t *cond) {
    g_render_mu   = mu;
    g_render_cond = cond;
}

int mpv_core_init(void *(*get_proc_addr)(void *ctx, const char *name), void *ctx,
                  int drm_fd, uint32_t crtc_id) {
    g_mpv = mpv_create();
    if (!g_mpv) {
        fprintf(stderr, "mpv: mpv_create failed\n");
        return -1;
    }

    mpv_request_log_messages(g_mpv, "warn");

    /* Hardware decode — drm-prime enables zero-copy import via EGL dmabuf.
     * Falls back to auto-safe if DRM PRIME path is unavailable. */
    mpv_set_option_string(g_mpv, "hwdec",
                          drm_fd >= 0 ? "drm-prime" : "auto-safe");
    mpv_set_option_string(g_mpv, "hwdec-codecs",  "h264,hevc,vp9");
    mpv_set_option_string(g_mpv, "vd-lavc-dr",    "yes");   /* skip extra buffer copy */
    mpv_set_option_string(g_mpv, "vo",            "libmpv");
    mpv_set_option_string(g_mpv, "ao",            "alsa");
    mpv_set_option_string(g_mpv, "audio-device",  "alsa/default"); /* explicit: HDMI = ALSA default on embedded boards */
    mpv_set_option_string(g_mpv, "video-sync",    "audio"); /* audio master, lighter on CPU */
    mpv_set_option_string(g_mpv, "framedrop",     "vo");   /* drop frames when CPU can't keep up */

    /* Fast bilinear scaling — lanczos is expensive on weak ARM */
    mpv_set_option_string(g_mpv, "scale",                 "bilinear");
    mpv_set_option_string(g_mpv, "dscale",                "bilinear");
    mpv_set_option_string(g_mpv, "correct-downscaling",   "no");
    mpv_set_option_string(g_mpv, "linear-downscaling",    "no");
    mpv_set_option_string(g_mpv, "interpolation",         "no");

    mpv_set_option_string(g_mpv, "audio-channels",        "stereo"); /* no surround upmix */

    mpv_set_option_string(g_mpv, "input-terminal", "no");
    mpv_set_option_string(g_mpv, "idle",            "yes");
    mpv_set_option_string(g_mpv, "keep-open",       "yes");

    /* Cache: small buffer → playback starts fast, not after 30s wait */
    mpv_set_option_string(g_mpv, "cache",              "yes");
    mpv_set_option_string(g_mpv, "cache-secs",         "8");
    mpv_set_option_string(g_mpv, "cache-pause-initial","no");
    mpv_set_option_string(g_mpv, "cache-pause-wait",   "1");
    mpv_set_option_string(g_mpv, "demuxer-max-bytes",  "15MiB");

    mpv_set_option_string(g_mpv, "profile-restore", "copy");

    /* Network */
    mpv_set_option_string(g_mpv, "network-timeout", "10");
    mpv_set_option_string(g_mpv, "user-agent",
                          "Mozilla/5.0 (X11; Linux aarch64) AppleWebKit/537.36");

    mpv_set_option_string(g_mpv, "sub-auto",  "no");
    mpv_set_option_string(g_mpv, "osd-level", "1");

    if (mpv_initialize(g_mpv) < 0) {
        fprintf(stderr, "mpv: mpv_initialize failed\n");
        return -1;
    }

    /* Observe properties — changes delivered via wakeup fd, no blocking poll */
    mpv_observe_property(g_mpv, 0, "time-pos",  MPV_FORMAT_DOUBLE);
    mpv_observe_property(g_mpv, 0, "duration",  MPV_FORMAT_DOUBLE);
    mpv_observe_property(g_mpv, 0, "volume",    MPV_FORMAT_DOUBLE);
    mpv_observe_property(g_mpv, 0, "pause",     MPV_FORMAT_FLAG);

    /* OpenGL render context with optional DRM PRIME zero-copy path.
     * When drm_fd >= 0, mpv imports decoded frames as EGL images (dmabufs)
     * directly into GL — no CPU copy, GPU receives raw frame data. */
    mpv_opengl_init_params gl_init = {
        .get_proc_address      = get_proc_addr,
        .get_proc_address_ctx  = ctx,
    };

    /* DRM PRIME params — tells mpv which DRM device/CRTC we're rendering on */
    mpv_opengl_drm_params_v2 drm_params = {
        .fd           = drm_fd,
        .crtc_id      = (int)crtc_id,
        .connector_id = 0,   /* 0 = don't manage connector */
        .render_fd    = drm_fd,
    };

    mpv_render_param params_with_drm[] = {
        { MPV_RENDER_PARAM_API_TYPE,            MPV_RENDER_API_TYPE_OPENGL },
        { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS,  &gl_init },
        { MPV_RENDER_PARAM_ADVANCED_CONTROL,    &(int){1} },
        { MPV_RENDER_PARAM_DRM_DISPLAY_V2,      &drm_params },
        { 0 },
    };
    mpv_render_param params_no_drm[] = {
        { MPV_RENDER_PARAM_API_TYPE,            MPV_RENDER_API_TYPE_OPENGL },
        { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS,  &gl_init },
        { MPV_RENDER_PARAM_ADVANCED_CONTROL,    &(int){1} },
        { 0 },
    };
    mpv_render_param *params = (drm_fd >= 0) ? params_with_drm : params_no_drm;

    if (mpv_render_context_create(&g_mpv_gl, g_mpv, params) < 0) {
        if (drm_fd >= 0) {
            /* DRM PRIME init failed (e.g. older mpv or driver missing ext) — retry without */
            fprintf(stderr, "mpv: DRM PRIME init failed, retrying without zero-copy\n");
            mpv_set_option_string(g_mpv, "hwdec", "auto-safe");
            if (mpv_render_context_create(&g_mpv_gl, g_mpv, params_no_drm) < 0) {
                fprintf(stderr, "mpv: mpv_render_context_create failed\n");
                return -1;
            }
        } else {
            fprintf(stderr, "mpv: mpv_render_context_create failed\n");
            return -1;
        }
    } else if (drm_fd >= 0) {
        fprintf(stderr, "mpv: DRM PRIME zero-copy path enabled (no CPU copy for video)\n");
    }

    mpv_render_context_set_update_callback(g_mpv_gl, on_mpv_render_update, NULL);

    fprintf(stderr, "mpv: libmpv ready (version %lu)\n", mpv_client_api_version());
    return 0;
}

int mpv_core_wakeup_fd(void) {
    return g_mpv ? mpv_get_wakeup_pipe(g_mpv) : -1;
}

void mpv_core_handle_events(void) {
    if (!g_mpv) return;
    mpv_event *ev;
    while ((ev = mpv_wait_event(g_mpv, 0)) && ev->event_id != MPV_EVENT_NONE) {
        switch (ev->event_id) {
            case MPV_EVENT_START_FILE:
                atomic_store(&g_video_active, 1);
                break;
            case MPV_EVENT_END_FILE:
            case MPV_EVENT_IDLE:
                atomic_store(&g_video_active, 0);
                atomic_store(&g_wants_render, 0);
                g_cached_pos    = 0.0;
                g_cached_dur    = 0.0;
                g_cached_paused = 0;
                break;
            case MPV_EVENT_PROPERTY_CHANGE: {
                mpv_event_property *p = ev->data;
                if (p->format == MPV_FORMAT_DOUBLE) {
                    if      (!strcmp(p->name, "time-pos"))
                        g_cached_pos = *(double *)p->data;
                    else if (!strcmp(p->name, "duration"))
                        g_cached_dur = *(double *)p->data;
                    else if (!strcmp(p->name, "volume"))
                        g_cached_vol = (int)*(double *)p->data;
                } else if (p->format == MPV_FORMAT_FLAG &&
                           !strcmp(p->name, "pause")) {
                    g_cached_paused = *(int *)p->data;
                }
                break;
            }
            case MPV_EVENT_LOG_MESSAGE: {
                mpv_event_log_message *msg = ev->data;
                fprintf(stderr, "mpv [%s]: %s", msg->level, msg->text);
                break;
            }
            default: break;
        }
    }
}

int mpv_core_is_video_active(void) {
    return atomic_load(&g_video_active);
}

int mpv_core_wants_render(void) {
    return atomic_exchange(&g_wants_render, 0);
}

void mpv_core_render(int w, int h) {
    if (!g_mpv_gl) return;

    int flip = 1;
    mpv_opengl_fbo fbo = {
        .fbo             = 0,
        .w               = w,
        .h               = h,
        .internal_format = 0,
    };
    mpv_render_param params[] = {
        { MPV_RENDER_PARAM_OPENGL_FBO, &fbo  },
        { MPV_RENDER_PARAM_FLIP_Y,     &flip },
        { 0 },
    };
    mpv_render_context_render(g_mpv_gl, params);
}

void mpv_core_load(const char *url, const char *profile) {
    if (!g_mpv || !url) return;

    if (profile) {
        if (!strcmp(profile, "live")) {
            mpv_set_property_string(g_mpv, "cache",            "no");
            mpv_set_property_string(g_mpv, "cache-pause",      "no");
            mpv_set_property_string(g_mpv, "demuxer-max-bytes","8MiB");
        } else {
            mpv_set_property_string(g_mpv, "cache",            "yes");
            mpv_set_property_string(g_mpv, "cache-pause",      "yes");
            mpv_set_property_string(g_mpv, "demuxer-max-bytes","15MiB");
        }
    }

    /* Cache URL immediately so push_status() can return it without blocking */
    strncpy(g_cached_url, url, sizeof(g_cached_url) - 1);
    g_cached_url[sizeof(g_cached_url) - 1] = '\0';

    const char *cmd[] = { "loadfile", url, "replace", NULL };
    mpv_command_async(g_mpv, 0, cmd);
}

void mpv_core_pause_toggle(void) {
    if (!g_mpv) return;
    /* Use cached pause state — no blocking mpv_get_property() */
    int new_state = !g_cached_paused;
    mpv_set_property_async(g_mpv, 0, "pause", MPV_FORMAT_FLAG, &new_state);
}

void mpv_core_stop(void) {
    if (!g_mpv) return;
    /* Clear immediately so the UI snaps back to the current screen without
     * waiting for END_FILE (which can take seconds when mpv tears down a
     * live-stream network connection). END_FILE will still fire later and
     * set g_video_active=0 again — harmless double-write. */
    atomic_store(&g_video_active, 0);
    atomic_store(&g_wants_render, 0);
    g_cached_pos    = 0.0;
    g_cached_dur    = 0.0;
    g_cached_paused = 0;
    const char *cmd[] = { "stop", NULL };
    mpv_command_async(g_mpv, 0, cmd);
}

void mpv_core_seek(double seconds) {
    if (!g_mpv) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", seconds);
    const char *cmd[] = { "seek", buf, "relative", NULL };
    mpv_command_async(g_mpv, 0, cmd);
}

void mpv_core_set_volume(int level) {
    if (!g_mpv) return;
    g_cached_vol = level;
    double v = (double)level;
    mpv_set_property_async(g_mpv, 0, "volume", MPV_FORMAT_DOUBLE, &v);
}

/* Returns cached status — never blocks.
 * Previously called mpv_get_property() 5+ times which could freeze
 * the event loop for seconds on a live-stream network stall. */
MpvStatus mpv_core_get_status(void) {
    MpvStatus s = {0};
    s.volume = g_cached_vol;
    strncpy(s.url, g_cached_url, sizeof(s.url) - 1);

    if (!atomic_load(&g_video_active)) {
        strcpy(s.state, "idle");
        return s;
    }

    strcpy(s.state, g_cached_paused ? "paused" : "playing");
    s.paused   = g_cached_paused;
    s.position = g_cached_pos;
    s.duration = g_cached_dur;
    return s;
}

void mpv_core_destroy(void) {
    if (g_mpv_gl) { mpv_render_context_free(g_mpv_gl); g_mpv_gl = NULL; }
    if (g_mpv)    { mpv_terminate_destroy(g_mpv);       g_mpv    = NULL; }
}
