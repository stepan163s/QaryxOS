#pragma once
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <stdint.h>

typedef struct {
    char  state[16];   /* "idle" | "playing" | "paused" | "error" */
    char  url[512];
    double position;
    double duration;
    int    volume;
    int    paused;
} MpvStatus;

/* Initialise libmpv handle + OpenGL render context.
   get_proc_addr: EGL proc address callback (pass egl_get_proc_address).
   Returns 0 on success. */
int  mpv_core_init(void *(*get_proc_addr)(void *ctx, const char *name), void *ctx);

/* Returns the mpv wakeup fd — add to epoll with EPOLLIN.
   When readable, call mpv_core_handle_events(). */
int  mpv_core_wakeup_fd(void);

/* Process all pending mpv events (call when wakeup fd is readable). */
void mpv_core_handle_events(void);

/* Returns 1 if a new frame is available and should be rendered. */
int  mpv_core_wants_render(void);

/* Render mpv video into the current GL framebuffer.
   Call before drawing UI overlay. */
void mpv_core_render(int w, int h);

/* Playback controls */
void mpv_core_load(const char *url, const char *profile); /* profile: "live" or NULL */
void mpv_core_pause_toggle(void);
void mpv_core_stop(void);
void mpv_core_seek(double seconds);      /* relative seek */
void mpv_core_set_volume(int level);     /* 0–100 */

/* Get current playback status. Thread-safe (reads properties synchronously). */
MpvStatus mpv_core_get_status(void);

void mpv_core_destroy(void);
