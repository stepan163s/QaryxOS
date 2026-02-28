#include "youtube.h"
#include "home.h"
#include "../render.h"
#include "../font.h"
#include "../ytdlp.h"
#include "../mpv.h"
#include "../history.h"
#include <string.h>
#include <stdio.h>

#define TILE_W      300
#define TILE_H      200
#define TILE_GAP     20
#define TILES_PER_ROW 5
#define MARGIN_X     60
#define MARGIN_Y    130

static YoutubeVideo g_videos[200];
static int          g_count   = 0;
static int          g_focused = 0;
static int          g_resolving = 0;

typedef struct {
    char url[512];
    int  video_idx;
} PendingPlay;

static PendingPlay g_pending_play;

static void on_resolved(const char *stream_url, void *ud) {
    PendingPlay *p = ud;
    g_resolving = 0;
    if (!stream_url) {
        fprintf(stderr, "youtube: resolve failed for %s\n", p->url);
        return;
    }
    mpv_core_load(stream_url, NULL);
    YoutubeVideo *v = &g_videos[p->video_idx];
    history_record(p->url, v->title, "youtube", v->channel_name,
                   v->thumbnail, (double)v->duration);
}

void ui_youtube_enter(void) {
    /* Videos are loaded by the app at startup and refresh.
       Here we just ensure focused is in range. */
    if (g_focused >= g_count) g_focused = g_count > 0 ? g_count-1 : 0;
}

/* Called from main after channel refresh */
void ui_youtube_set_videos(YoutubeVideo *vids, int n) {
    int copy = n < 200 ? n : 200;
    memcpy(g_videos, vids, copy * sizeof(YoutubeVideo));
    g_count = copy;
    if (g_focused >= g_count) g_focused = g_count > 0 ? g_count-1 : 0;
}

void ui_youtube_draw(void) {
    font_draw(MARGIN_X, 40, "▶  YouTube", 38, COL_ACCENT);
    font_draw(MARGIN_X, 90,
              "OK — play   ← → ↑ ↓ — navigate   Back — home",
              20, COL_GRAY);

    if (g_count == 0) {
        font_draw(MARGIN_X, 400,
                  "No videos — add channels in Qaryx Remote app",
                  28, COL_GRAY);
        return;
    }

    int visible_rows = (g_screen_h - MARGIN_Y - 40) / (TILE_H + TILE_GAP);
    int focused_row  = g_focused / TILES_PER_ROW;
    int scroll_row   = focused_row - visible_rows / 2;
    if (scroll_row < 0) scroll_row = 0;

    for (int i = 0; i < g_count; i++) {
        int row = i / TILES_PER_ROW;
        int col = i % TILES_PER_ROW;
        if (row < scroll_row) continue;

        int x = MARGIN_X + col * (TILE_W + TILE_GAP);
        int y = MARGIN_Y + (row - scroll_row) * (TILE_H + TILE_GAP);
        if (y + TILE_H > g_screen_h - 40) break;

        int sel = (i == g_focused);
        render_rect(x, y, TILE_W, TILE_H, sel ? COL_TILE_HL : COL_TILE);
        if (sel) render_rect_outline(x, y, TILE_W, TILE_H, COL_ACCENT, 2);

        /* Thumbnail placeholder */
        render_rect(x+6, y+6, TILE_W-12, 110, rgba(40,40,40,255));

        /* Title — max 2 lines */
        char line1[48] = {0}, line2[48] = {0};
        const char *t = g_videos[i].title;
        if (strlen(t) <= 28) {
            strncpy(line1, t, sizeof(line1)-1);
        } else {
            strncpy(line1, t, 28); line1[28] = '\0';
            strncpy(line2, t+28, sizeof(line2)-1);
        }
        font_draw(x+6, y+122, line1, 18, sel ? COL_WHITE : rgba(210,210,210,255));
        if (line2[0]) font_draw(x+6, y+144, line2, 18, COL_GRAY);

        /* Duration */
        if (g_videos[i].duration > 0) {
            char dur[16];
            snprintf(dur, sizeof(dur), "%d:%02d",
                     g_videos[i].duration/60, g_videos[i].duration%60);
            float dw = font_measure(dur, 16);
            font_draw(x + TILE_W - (int)dw - 6, y + TILE_H - 20, dur, 16, COL_GRAY);
        }

        /* Channel */
        font_draw(x+6, y+TILE_H-22, g_videos[i].channel_name, 16, COL_GRAY);
    }

    /* Resolving spinner */
    if (g_resolving)
        font_draw(MARGIN_X, g_screen_h - 40, "Resolving YouTube URL...", 22, COL_ACCENT);
}

void ui_youtube_key(const char *key) {
    int n = g_count > 0 ? g_count : 1;

    if (!strcmp(key, "right"))
        g_focused = (g_focused + 1 < n) ? g_focused + 1 : g_focused;
    else if (!strcmp(key, "left"))
        g_focused = (g_focused > 0) ? g_focused - 1 : 0;
    else if (!strcmp(key, "down"))
        g_focused = (g_focused + TILES_PER_ROW < n) ? g_focused + TILES_PER_ROW : g_focused;
    else if (!strcmp(key, "up"))
        g_focused = (g_focused - TILES_PER_ROW >= 0) ? g_focused - TILES_PER_ROW : g_focused;
    else if (!strcmp(key, "ok") && g_count > 0 && !g_resolving) {
        YoutubeVideo *v = &g_videos[g_focused];
        g_pending_play.video_idx = g_focused;
        strncpy(g_pending_play.url, v->url, sizeof(g_pending_play.url)-1);
        g_resolving = 1;
        ytdlp_resolve(v->url, "1080", on_resolved, &g_pending_play);
    } else if (!strcmp(key, "back") || !strcmp(key, "home")) {
        navigate("home");
    }
}
