#include "home.h"
#include "../render.h"
#include "../font.h"
#include "../mpv.h"
#include <string.h>
#include <stdio.h>

Screen g_screen = SCREEN_HOME;

void navigate(const char *name) {
    if      (!strcmp(name, "home"))     g_screen = SCREEN_HOME;
    else if (!strcmp(name, "youtube"))  g_screen = SCREEN_YOUTUBE;
    else if (!strcmp(name, "iptv"))     g_screen = SCREEN_IPTV;
    else if (!strcmp(name, "settings")) g_screen = SCREEN_SETTINGS;
}

/* ── Home screen ─────────────────────────────────────────────────────────── */

/* 4 tiles: YouTube / IPTV / History / Settings */
#define N_TILES 4

static const struct {
    const char *label;
    const char *icon;
    Screen      dest;
} TILES[N_TILES] = {
    { "YouTube",  "▶", SCREEN_YOUTUBE  },
    { "IPTV",     "📡", SCREEN_IPTV    },
    { "History",  "🕐", SCREEN_HOME    },   /* TODO: SCREEN_HISTORY */
    { "Settings", "⚙", SCREEN_SETTINGS },
};

static int g_focused = 0;

#define TILE_W  380
#define TILE_H  240
#define TILE_GAP 40
#define START_X  ((g_screen_w - (N_TILES * TILE_W + (N_TILES-1) * TILE_GAP)) / 2)
#define START_Y  ((g_screen_h - TILE_H) / 2)

void ui_home_draw(void) {
    /* Header */
    font_draw(60, 40, "QaryxOS", 42, COL_ACCENT);

    /* Status bar */
    MpvStatus st = mpv_core_get_status();
    char status_text[128];
    if (!strcmp(st.state, "playing") || !strcmp(st.state, "paused"))
        snprintf(status_text, sizeof(status_text),
                 "%s  %d:%02d / %d:%02d  vol %d%%",
                 !strcmp(st.state,"paused") ? "⏸" : "▶",
                 (int)st.position/60, (int)st.position%60,
                 (int)st.duration/60, (int)st.duration%60,
                 st.volume);
    else
        snprintf(status_text, sizeof(status_text), "Idle");
    font_draw(60, 100, status_text, 22, COL_GRAY);

    /* Tiles */
    for (int i = 0; i < N_TILES; i++) {
        int x = START_X + i * (TILE_W + TILE_GAP);
        int y = START_Y;

        uint32_t bg = (i == g_focused) ? COL_TILE_HL : COL_TILE;
        render_rect(x, y, TILE_W, TILE_H, bg);

        if (i == g_focused)
            render_rect_outline(x, y, TILE_W, TILE_H, COL_ACCENT, 3);

        /* Icon */
        font_draw(x + TILE_W/2 - 24, y + 60, TILES[i].icon, 48,
                  i == g_focused ? COL_WHITE : COL_GRAY);
        /* Label */
        float tw = font_measure(TILES[i].label, 28);
        font_draw(x + (int)(TILE_W - tw)/2, y + TILE_H - 60,
                  TILES[i].label, 28,
                  i == g_focused ? COL_WHITE : COL_GRAY);
    }

    /* Hint */
    font_draw(60, g_screen_h - 48,
              "← → navigate   OK — open   Back — stop playback",
              20, COL_GRAY);
}

void ui_home_key(const char *key) {
    if (!strcmp(key, "right")) {
        g_focused = (g_focused + 1) % N_TILES;
    } else if (!strcmp(key, "left")) {
        g_focused = (g_focused - 1 + N_TILES) % N_TILES;
    } else if (!strcmp(key, "ok")) {
        if (TILES[g_focused].dest != SCREEN_HOME)
            g_screen = TILES[g_focused].dest;
    } else if (!strcmp(key, "back")) {
        mpv_core_stop();
    } else if (!strcmp(key, "play")) {
        mpv_core_pause_toggle();
    }
}

void ui_home_enter(void) {
    /* Nothing to load on home screen */
}
