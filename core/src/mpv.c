#include "mpv.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>

static mpv_handle          *g_mpv    = NULL;
static mpv_render_context  *g_mpv_gl = NULL;
static atomic_int           g_wants_render = 0;

static void on_mpv_render_update(void *ctx) {
    (void)ctx;
    atomic_store(&g_wants_render, 1);
}

int mpv_core_init(void *(*get_proc_addr)(void *ctx, const char *name), void *ctx) {
    g_mpv = mpv_create();
    if (!g_mpv) {
        fprintf(stderr, "mpv: mpv_create failed\n");
        return -1;
    }

    /* Logging */
    mpv_request_log_messages(g_mpv, "warn");

    /* Hardware decode */
    mpv_set_option_string(g_mpv, "hwdec",        "rkmpp");
    mpv_set_option_string(g_mpv, "hwdec-codecs",  "h264,hevc,vp9,av1");
    mpv_set_option_string(g_mpv, "vo",            "libmpv");
    mpv_set_option_string(g_mpv, "ao",            "alsa");

    /* IPC file removed — we control via API directly */
    mpv_set_option_string(g_mpv, "input-terminal", "no");
    mpv_set_option_string(g_mpv, "idle",            "yes");
    mpv_set_option_string(g_mpv, "keep-open",       "yes");

    /* Cache for VOD */
    mpv_set_option_string(g_mpv, "cache",           "yes");
    mpv_set_option_string(g_mpv, "cache-secs",      "30");
    mpv_set_option_string(g_mpv, "demuxer-max-bytes","50MiB");

    /* Profiles for IPTV (live) */
    mpv_set_option_string(g_mpv, "profile-restore", "copy");

    /* Network */
    mpv_set_option_string(g_mpv, "network-timeout", "10");
    mpv_set_option_string(g_mpv, "user-agent",
                          "Mozilla/5.0 (X11; Linux aarch64) AppleWebKit/537.36");

    /* Sub + OSD */
    mpv_set_option_string(g_mpv, "sub-auto",  "no");
    mpv_set_option_string(g_mpv, "osd-level", "1");

    if (mpv_initialize(g_mpv) < 0) {
        fprintf(stderr, "mpv: mpv_initialize failed\n");
        return -1;
    }

    /* OpenGL render context */
    mpv_opengl_init_params gl_init = {
        .get_proc_address      = get_proc_addr,
        .get_proc_address_ctx  = ctx,
    };
    mpv_render_param params[] = {
        { MPV_RENDER_PARAM_API_TYPE,            MPV_RENDER_API_TYPE_OPENGL },
        { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS,  &gl_init },
        { MPV_RENDER_PARAM_ADVANCED_CONTROL,    &(int){1} },
        { 0 },
    };

    if (mpv_render_context_create(&g_mpv_gl, g_mpv, params) < 0) {
        fprintf(stderr, "mpv: mpv_render_context_create failed\n");
        return -1;
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
        if (ev->event_id == MPV_EVENT_LOG_MESSAGE) {
            mpv_event_log_message *msg = ev->data;
            fprintf(stderr, "mpv [%s]: %s", msg->level, msg->text);
        }
    }
}

int mpv_core_wants_render(void) {
    return atomic_exchange(&g_wants_render, 0);
}

void mpv_core_render(int w, int h) {
    if (!g_mpv_gl) return;

    int flip = 1;   /* OpenGL convention: y-flip */
    mpv_opengl_fbo fbo = {
        .fbo    = 0,    /* default framebuffer */
        .w      = w,
        .h      = h,
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
        /* Apply profile properties directly */
        if (!strcmp(profile, "live")) {
            mpv_set_property_string(g_mpv, "cache",           "no");
            mpv_set_property_string(g_mpv, "cache-pause",     "no");
            mpv_set_property_string(g_mpv, "demuxer-max-bytes","8MiB");
        } else {
            mpv_set_property_string(g_mpv, "cache",           "yes");
            mpv_set_property_string(g_mpv, "cache-pause",     "yes");
            mpv_set_property_string(g_mpv, "demuxer-max-bytes","50MiB");
        }
    }

    const char *cmd[] = { "loadfile", url, "replace", NULL };
    mpv_command_async(g_mpv, 0, cmd);
}

void mpv_core_pause_toggle(void) {
    if (!g_mpv) return;
    int paused = 0;
    mpv_get_property(g_mpv, "pause", MPV_FORMAT_FLAG, &paused);
    int new_state = !paused;
    mpv_set_property(g_mpv, "pause", MPV_FORMAT_FLAG, &new_state);
}

void mpv_core_stop(void) {
    if (!g_mpv) return;
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
    double v = (double)level;
    mpv_set_property(g_mpv, "volume", MPV_FORMAT_DOUBLE, &v);
}

MpvStatus mpv_core_get_status(void) {
    MpvStatus s = { .state = "idle" };
    if (!g_mpv) return s;

    int idle = 0;
    mpv_get_property(g_mpv, "idle-active", MPV_FORMAT_FLAG, &idle);
    if (idle) { strcpy(s.state, "idle"); return s; }

    int paused = 0;
    mpv_get_property(g_mpv, "pause", MPV_FORMAT_FLAG, &paused);
    strcpy(s.state, paused ? "paused" : "playing");
    s.paused = paused;

    char *path = NULL;
    mpv_get_property(g_mpv, "path", MPV_FORMAT_STRING, &path);
    if (path) { strncpy(s.url, path, sizeof(s.url)-1); mpv_free(path); }

    mpv_get_property(g_mpv, "time-pos",  MPV_FORMAT_DOUBLE, &s.position);
    mpv_get_property(g_mpv, "duration",  MPV_FORMAT_DOUBLE, &s.duration);
    double vol = 80;
    mpv_get_property(g_mpv, "volume",    MPV_FORMAT_DOUBLE, &vol);
    s.volume = (int)vol;

    return s;
}

void mpv_core_destroy(void) {
    if (g_mpv_gl) { mpv_render_context_free(g_mpv_gl); g_mpv_gl = NULL; }
    if (g_mpv)    { mpv_terminate_destroy(g_mpv);       g_mpv    = NULL; }
}
