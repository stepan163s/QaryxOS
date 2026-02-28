/*
 * Qaryx Core — main entry point
 * Single-process embedded TV UI for RK3566 / Radxa Zero 3W
 *
 * Event loop: epoll handles DRM page-flip, libinput, WebSocket,
 * yt-dlp pipes, mpv wakeup fd. All I/O is non-blocking.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#include "drm.h"
#include "egl.h"
#include "render.h"
#include "font.h"
#include "input.h"
#include "mpv.h"
#include "ws.h"
#include "ytdlp.h"
#include "iptv.h"
#include "history.h"
#include "config.h"
#include "../third_party/cjson.h"

#include "services.h"
#include "ui/home.h"
#include "ui/youtube.h"
#include "ui/iptv.h"

/* ── Global state ──────────────────────────────────────────────────────────── */

static DrmState g_drm;
static EglState g_egl;
static Config   g_cfg;
static int      g_epoll_fd  = -1;
static int      g_timer_fd  = -1;   /* 30ms frame timer */
static int      g_running   = 1;
static int      g_display_ok = 0;   /* 0 if no HDMI/DRM available */

/* ── epoll helpers ─────────────────────────────────────────────────────────── */

static void epoll_add(int fd, uint32_t events, void *ptr) {
    struct epoll_event ev = { .events = events, .data.ptr = ptr };
    epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

static void epoll_del(int fd) {
    epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
}

/* ── YouTube play callback ─────────────────────────────────────────────────── */

static void ws_play_cb(const char *stream_url, void *userdata) {
    const char *orig_url = (const char *)userdata;
    if (!stream_url) {
        fprintf(stderr, "play: yt-dlp FAILED for %s\n", orig_url);
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "type", "error");
        cJSON_AddStringToObject(err, "msg",  "yt-dlp resolve failed");
        char *s = cJSON_Print(err); cJSON_Delete(err);
        ws_broadcast(s); free(s);
        return;
    }
    fprintf(stderr, "play: stream_url=%.120s\n", stream_url);
    if (!g_display_ok) {
        fprintf(stderr, "play: no display, cannot play\n");
        return;
    }
    mpv_core_load(stream_url, NULL);
    history_record(orig_url, orig_url, "youtube", "", "", 0);
}

/* ── WebSocket message handler ─────────────────────────────────────────────── */

static void ws_dispatch_cmd(const char *json) {
    fprintf(stderr, "ws recv: %.200s\n", json);
    cJSON *j = cJSON_Parse(json);
    if (!j) { fprintf(stderr, "ws recv: JSON parse failed\n"); return; }

    const char *cmd = cJSON_GetString(j, "cmd", "");

    if (!strcmp(cmd, "play")) {
        const char *url  = cJSON_GetString(j, "url",  "");
        const char *type = cJSON_GetString(j, "type", "");

        if (!strcmp(type, "youtube") ||
            strstr(url, "youtube.com") || strstr(url, "youtu.be")) {
            fprintf(stderr, "play: youtube resolve -> %s\n", url);
            static char g_play_orig_url[512];
            strncpy(g_play_orig_url, url, sizeof(g_play_orig_url)-1);
            ytdlp_resolve(url, "1080", ws_play_cb, g_play_orig_url);
        } else {
            const char *profile = (!strcmp(type,"iptv")) ? "live" : NULL;
            fprintf(stderr, "play: direct -> %s (profile=%s)\n", url, profile ? profile : "none");
            mpv_core_load(url, profile);
            history_record(url, url, type[0] ? type : "direct", "", "", 0);
        }
    } else if (!strcmp(cmd, "pause")) {
        mpv_core_pause_toggle();
    } else if (!strcmp(cmd, "stop")) {
        mpv_core_stop();
    } else if (!strcmp(cmd, "seek")) {
        double secs = cJSON_GetNumber(j, "seconds", 0);
        mpv_core_seek(secs);
    } else if (!strcmp(cmd, "volume")) {
        int level = (int)cJSON_GetNumber(j, "level", 80);
        mpv_core_set_volume(level);
    } else if (!strcmp(cmd, "key")) {
        const char *key = cJSON_GetString(j, "key", "");
        switch (g_screen) {
            case SCREEN_HOME:     ui_home_key(key);     break;
            case SCREEN_YOUTUBE:  ui_youtube_key(key);  break;
            case SCREEN_IPTV:     ui_iptv_key(key);     break;
            case SCREEN_SETTINGS: ui_settings_key(key); break;
            default: break;
        }
    } else if (!strcmp(cmd, "navigate")) {
        navigate(cJSON_GetString(j, "screen", "home"));
    } else if (!strcmp(cmd, "history_get")) {
        int n; HistoryEntry *h = history_get_all(&n);
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "type", "history");
        cJSON *arr = cJSON_CreateArray();
        int lim = (int)cJSON_GetNumber(j, "limit", 20);
        if (lim > n) lim = n;
        for (int i = 0; i < lim; i++) {
            cJSON *e = cJSON_CreateObject();
            cJSON_AddStringToObject(e, "url",          h[i].url);
            cJSON_AddStringToObject(e, "title",        h[i].title);
            cJSON_AddStringToObject(e, "content_type", h[i].content_type);
            cJSON_AddStringToObject(e, "thumbnail",    h[i].thumbnail);
            cJSON_AddNumberToObject(e, "position",     h[i].position);
            cJSON_AddNumberToObject(e, "duration",     h[i].duration);
            cJSON_AddNumberToObject(e, "played_at",    (double)h[i].played_at);
            cJSON_AddItemToArray(arr, e);
        }
        cJSON_AddItemToObject(resp, "entries", arr);
        char *s = cJSON_Print(resp); cJSON_Delete(resp);
        ws_broadcast(s); free(s);

    } else if (!strcmp(cmd, "history_clear")) {
        history_clear();
    } else if (!strcmp(cmd, "playlists_get")) {
        int n; IptvPlaylist *pl = iptv_get_playlists(&n);
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "type", "playlists");
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < n; i++) {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "id",            pl[i].id);
            cJSON_AddStringToObject(o, "name",          pl[i].name);
            cJSON_AddStringToObject(o, "url",           pl[i].url);
            cJSON_AddNumberToObject(o, "channel_count", pl[i].channel_count);
            cJSON_AddItemToArray(arr, o);
        }
        cJSON_AddItemToObject(resp, "playlists", arr);
        char *s = cJSON_Print(resp); cJSON_Delete(resp);
        ws_broadcast(s); free(s);

    } else if (!strcmp(cmd, "playlist_add")) {
        const char *url  = cJSON_GetString(j, "url",  "");
        const char *name = cJSON_GetString(j, "name", "Playlist");
        iptv_add_playlist(url, name);  /* blocking — TODO: thread */
    } else if (!strcmp(cmd, "playlist_del")) {
        iptv_remove_playlist(cJSON_GetString(j, "id", ""));
    } else if (!strcmp(cmd, "service_get")) {
        const ServicesState *sv = services_get(1);
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "type", "services");
        cJSON *xr = cJSON_CreateObject();
        cJSON_AddBoolToObject(xr, "active",  sv->xray_active);
        cJSON_AddBoolToObject(xr, "enabled", sv->xray_enabled);
        cJSON_AddItemToObject(resp, "xray", xr);
        cJSON *ts = cJSON_CreateObject();
        cJSON_AddBoolToObject(ts, "active",  sv->tailscale_active);
        cJSON_AddBoolToObject(ts, "enabled", sv->tailscale_enabled);
        cJSON_AddItemToObject(resp, "tailscaled", ts);
        char *s = cJSON_Print(resp); cJSON_Delete(resp);
        ws_broadcast(s); free(s);

    } else if (!strcmp(cmd, "service_set")) {
        const char *name = cJSON_GetString(j, "name", "");
        int enable = cJSON_GetBool(j, "enabled", 0);
        services_set(name, enable);
        /* broadcast updated state */
        const ServicesState *sv = services_get(0);
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "type", "services");
        cJSON *xr = cJSON_CreateObject();
        cJSON_AddBoolToObject(xr, "active",  sv->xray_active);
        cJSON_AddBoolToObject(xr, "enabled", sv->xray_enabled);
        cJSON_AddItemToObject(resp, "xray", xr);
        cJSON *ts = cJSON_CreateObject();
        cJSON_AddBoolToObject(ts, "active",  sv->tailscale_active);
        cJSON_AddBoolToObject(ts, "enabled", sv->tailscale_enabled);
        cJSON_AddItemToObject(resp, "tailscaled", ts);
        char *s = cJSON_Print(resp); cJSON_Delete(resp);
        ws_broadcast(s); free(s);

    } else if (!strcmp(cmd, "reboot")) {
        system("systemctl reboot");
    }

    cJSON_Delete(j);
}

/* ── Status push ───────────────────────────────────────────────────────────── */

static void push_status(void) {
    MpvStatus st = mpv_core_get_status();
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "type",     "status");
    cJSON_AddStringToObject(j, "state",    st.state);
    cJSON_AddStringToObject(j, "url",      st.url);
    cJSON_AddNumberToObject(j, "position", st.position);
    cJSON_AddNumberToObject(j, "duration", st.duration);
    cJSON_AddNumberToObject(j, "volume",   st.volume);
    cJSON_AddBoolToObject  (j, "paused",   st.paused);
    char *s = cJSON_Print(j); cJSON_Delete(j);
    ws_broadcast(s); free(s);
}

/* ── Render frame ──────────────────────────────────────────────────────────── */

static void render_frame(void) {
    /* mpv video first (if playing) */
    if (mpv_core_wants_render())
        mpv_core_render(g_cfg.screen_w, g_cfg.screen_h);
    else
        render_begin_frame();

    /* UI overlay */
    switch (g_screen) {
        case SCREEN_HOME:     ui_home_draw();     break;
        case SCREEN_YOUTUBE:  ui_youtube_draw();  break;
        case SCREEN_IPTV:     ui_iptv_draw();     break;
        case SCREEN_SETTINGS: ui_settings_draw(); break;
        default: break;
    }

    egl_swap(&g_egl, &g_drm);
}

/* ── Signal handler ────────────────────────────────────────────────────────── */

static void on_signal(int sig) { (void)sig; g_running = 0; }

/* ── Tagged epoll data ─────────────────────────────────────────────────────── */

#define TAG_DRM     ((void*)1)
#define TAG_INPUT   ((void*)2)
#define TAG_WS_LISTEN ((void*)3)
#define TAG_TIMER   ((void*)4)
#define TAG_MPV     ((void*)5)
/* WS client fds: pointer value = (void*)(intptr_t)(fd + 100) */
/* ytdlp pipe fds: pointer value = (void*)(intptr_t)(fd + 10000) */

static void update_ytdlp_epoll(void) {
    /* Re-register yt-dlp pipe fds every frame (they come and go) */
    static int prev_fds[8];
    static int prev_n = 0;

    int n;
    int *fds = ytdlp_pending_fds(&n);

    /* Remove stale */
    for (int i = 0; i < prev_n; i++) {
        int found = 0;
        for (int j = 0; j < n; j++) if (fds[j] == prev_fds[i]) { found=1; break; }
        if (!found) epoll_del(prev_fds[i]);
    }
    /* Add new */
    for (int i = 0; i < n; i++) {
        int found = 0;
        for (int j = 0; j < prev_n; j++) if (prev_fds[j] == fds[i]) { found=1; break; }
        if (!found)
            epoll_add(fds[i], EPOLLIN,
                      (void*)(intptr_t)(fds[i] + 10000));
    }
    if (n <= 8) { memcpy(prev_fds, fds, n*sizeof(int)); prev_n = n; }
}

/* ── Main ──────────────────────────────────────────────────────────────────── */

int main(void) {
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    /* Config */
    config_load(&g_cfg);
    ytdlp_set_proxy(g_cfg.ytdlp_proxy);

    /* If proxy configured, set env vars so libcurl (used by libmpv) picks it up */
    if (g_cfg.ytdlp_proxy[0]) {
        setenv("http_proxy",  g_cfg.ytdlp_proxy, 1);
        setenv("https_proxy", g_cfg.ytdlp_proxy, 1);
        setenv("HTTP_PROXY",  g_cfg.ytdlp_proxy, 1);
        setenv("HTTPS_PROXY", g_cfg.ytdlp_proxy, 1);
        fprintf(stderr, "qaryx: http_proxy=%s\n", g_cfg.ytdlp_proxy);
    }

    /* DRM + EGL — non-fatal: WebSocket runs even without a display */
    if (drm_init(&g_drm) < 0) {
        fprintf(stderr, "qaryx: no display, running headless (WS only)\n");
    } else if (egl_init(&g_egl, &g_drm) < 0) {
        fprintf(stderr, "qaryx: EGL failed, running headless (WS only)\n");
    } else if (render_init(g_cfg.screen_w, g_cfg.screen_h) < 0) {
        fprintf(stderr, "qaryx: render init failed, running headless\n");
    } else {
        font_init(g_cfg.font_path);
        if (mpv_core_init(egl_get_proc_address, NULL) < 0)
            fprintf(stderr, "qaryx: mpv init failed\n");
        else
            mpv_core_set_volume(g_cfg.volume);
        g_display_ok = 1;
    }

    /* libinput */
    if (input_init() < 0) fprintf(stderr, "input: no input devices\n");

    /* WebSocket — always required */
    if (ws_init(g_cfg.ws_port, ws_dispatch_cmd) < 0) return 1;

    /* Data load */
    history_load();
    iptv_load();

    /* epoll */
    g_epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    if (g_display_ok)
        epoll_add(g_drm.fd, EPOLLIN, TAG_DRM);
    if (input_fd() >= 0)
        epoll_add(input_fd(), EPOLLIN, TAG_INPUT);
    epoll_add(ws_listen_fd(), EPOLLIN, TAG_WS_LISTEN);

    static int mpv_wfd = -1;
    if (g_display_ok) {
        mpv_wfd = mpv_core_wakeup_fd();
        if (mpv_wfd >= 0) epoll_add(mpv_wfd, EPOLLIN, TAG_MPV);
    }

    /* 30ms frame timer (used for status push even in headless mode) */
    g_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    struct itimerspec its = {
        .it_interval = { .tv_sec = 0, .tv_nsec = 33333333 },  /* ~30 FPS */
        .it_value    = { .tv_sec = 0, .tv_nsec = 1 },
    };
    timerfd_settime(g_timer_fd, 0, &its, NULL);
    epoll_add(g_timer_fd, EPOLLIN, TAG_TIMER);

    /* Initial screen */
    if (g_display_ok) {
        ui_home_enter();
        render_frame();
    }

    /* Status push every 500ms */
    static int status_counter = 0;

    fprintf(stderr, "qaryx: event loop started\n");

    while (g_running) {
        /* Update yt-dlp pipe fds in epoll */
        update_ytdlp_epoll();

        struct epoll_event events[32];
        int n = epoll_wait(g_epoll_fd, events, 32, 50);

        for (int i = 0; i < n; i++) {
            void *tag = events[i].data.ptr;

            if (tag == TAG_DRM) {
                drm_handle_flip_event(&g_drm);

            } else if (tag == TAG_INPUT) {
                const char *keys[16];
                int nk = input_dispatch(keys, 16);
                for (int k = 0; k < nk; k++) {
                    switch (g_screen) {
                        case SCREEN_HOME:     ui_home_key(keys[k]);     break;
                        case SCREEN_YOUTUBE:  ui_youtube_key(keys[k]);  break;
                        case SCREEN_IPTV:     ui_iptv_key(keys[k]);     break;
                        case SCREEN_SETTINGS: ui_settings_key(keys[k]); break;
                        default: break;
                    }
                }

            } else if (tag == TAG_WS_LISTEN) {
                int cfd = ws_accept();
                if (cfd >= 0)
                    epoll_add(cfd, EPOLLIN | EPOLLHUP | EPOLLERR,
                              (void*)(intptr_t)(cfd + 100));

            } else if (tag == TAG_TIMER) {
                uint64_t expirations;
                read(g_timer_fd, &expirations, sizeof(expirations));
                if (g_display_ok) render_frame();

                /* Push status every ~500ms (every 15 frames at 30fps) */
                if (++status_counter >= 15) { status_counter = 0; push_status(); }

            } else if (tag == TAG_MPV) {
                uint64_t dummy; read(mpv_wfd, &dummy, sizeof(dummy));
                mpv_core_handle_events();

            } else {
                intptr_t val = (intptr_t)tag;

                if (val >= 10000) {
                    /* yt-dlp pipe fd (tag = fd + 10000, fd is typically 3-50) */
                    ytdlp_dispatch((int)(val - 10000));

                } else if (val >= 100) {
                    /* WebSocket client fd (tag = fd + 100) */
                    int cfd = (int)(val - 100);
                    if (ws_client_read(cfd) < 0) {
                        epoll_del(cfd);
                    }
                }
            }
        }
    }

    /* Cleanup */
    fprintf(stderr, "qaryx: shutting down\n");
    ws_destroy();
    input_destroy();
    mpv_core_destroy();
    font_destroy();
    egl_destroy(&g_egl);
    drm_destroy(&g_drm);
    close(g_epoll_fd);
    close(g_timer_fd);
    return 0;
}
