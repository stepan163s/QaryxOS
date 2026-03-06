#include "home.h"
#include "iptv.h"
#include "../render.h"
#include "../font.h"
#include "../mpv.h"
#include "../services.h"
#include <string.h>
#include <stdio.h>

Screen g_screen = SCREEN_HOME;

void navigate(const char *name) {
    if      (!strcmp(name, "home"))     { g_screen = SCREEN_HOME; }
    else if (!strcmp(name, "youtube"))  { g_screen = SCREEN_YOUTUBE; }
    else if (!strcmp(name, "iptv"))     { ui_iptv_enter(); g_screen = SCREEN_IPTV; }
    else if (!strcmp(name, "settings")) { ui_settings_enter(); g_screen = SCREEN_SETTINGS; }
}

/* ── Home screen — 2×2 tile grid ─────────────────────────────────────────── */

#define N_TILES  4
#define N_COLS   2

static const struct {
    const char *label;
    const char *icon;   /* large ASCII glyph drawn at 54px */
    const char *hint;   /* one-line description */
    Screen      dest;
} TILES[N_TILES] = {
    { "YouTube",  "[ > ]",  "Browse videos",    SCREEN_YOUTUBE  },
    { "IPTV",     "[ TV ]", "Live TV channels", SCREEN_IPTV     },
    { "History",  "[ << ]", "Recently watched", SCREEN_HOME     },
    { "Settings", "[ :: ]", "System controls",  SCREEN_SETTINGS },
};

static int g_focused = 0;   /* 0=YT 1=IPTV 2=History 3=Settings */

/* Layout — computed at draw time from actual screen dims */
#define HM       80    /* left/right margin */
#define VT      108    /* y where the tile grid starts */
#define TGAP     36    /* gap between tiles (both axes) */
#define FTR_H    44    /* footer bar height */
#define FTR_PAD  12    /* gap between last tile row and footer line */

void ui_home_draw(void) {
    int W = g_screen_w, H = g_screen_h;
    int TW = (W - 2*HM - TGAP) / 2;
    int TH = (H - VT - FTR_H - FTR_PAD - TGAP) / 2;

    /* Slim left-edge accent stripe */
    render_rect(0, 0, 3, H, COL_ACCENT);

    /* ── Header ─────────────────────────────────────────────────────────── */
    font_draw(HM, 22, "QaryxOS", 38, COL_ACCENT);

    /* Playback status — right-aligned */
    MpvStatus st = mpv_core_get_status();
    char sbuf[128] = "";
    if (!strcmp(st.state, "playing") || !strcmp(st.state, "paused"))
        snprintf(sbuf, sizeof(sbuf), "%s  %d:%02d / %d:%02d  vol %d%%",
                 !strcmp(st.state, "paused") ? "||" : ">",
                 (int)st.position/60, (int)st.position%60,
                 (int)st.duration/60, (int)st.duration%60,
                 st.volume);
    if (sbuf[0]) {
        float sw = font_measure(sbuf, 19);
        font_draw((int)(W - HM - sw), 34, sbuf, 19, COL_GRAY);
    }

    /* Header separator */
    render_rect(HM, VT - 8, W - 2*HM, 1, rgba(35, 35, 55, 255));

    /* ── 2×2 Tile grid ───────────────────────────────────────────────────── */
    for (int i = 0; i < N_TILES; i++) {
        int col = i % N_COLS;
        int row = i / N_COLS;
        int tx  = HM + col * (TW + TGAP);
        int ty  = VT + row * (TH + TGAP);
        int sel = (i == g_focused);

        /* Tile background */
        uint32_t bg = sel ? rgba(22, 26, 52, 255) : rgba(15, 16, 30, 255);
        render_rect(tx, ty, TW, TH, bg);

        /* Left accent stripe — focused only */
        if (sel)
            render_rect(tx, ty, 4, TH, COL_ACCENT);

        /* Outline — focused only */
        if (sel)
            render_rect_outline(tx, ty, TW, TH, COL_ACCENT, 2);

        /* Icon — centered horizontally, upper 55% of tile */
        float iw = font_measure(TILES[i].icon, 54);
        int   ix = tx + (TW - (int)iw) / 2;
        int   iy = ty + TH * 55 / 100 - 80;
        uint32_t icol = sel ? COL_WHITE : rgba(50, 55, 95, 255);
        font_draw(ix, iy, TILES[i].icon, 54, icol);

        /* Divider between icon area and text area */
        render_rect(tx + 36, ty + TH - 120, TW - 72, 1, rgba(30, 32, 55, 255));

        /* Tile label */
        uint32_t lcol = sel ? COL_WHITE : rgba(145, 150, 190, 255);
        font_draw(tx + 36, ty + TH - 105, TILES[i].label, 27, lcol);

        /* One-line hint */
        uint32_t hcol = sel ? rgba(130, 145, 215, 255) : rgba(55, 58, 88, 255);
        font_draw(tx + 36, ty + TH - 65, TILES[i].hint, 19, hcol);
    }

    /* ── Footer ──────────────────────────────────────────────────────────── */
    render_rect(0, H - FTR_H, W, 1, rgba(35, 35, 55, 255));
    font_draw(HM, H - FTR_H + 13,
              "< > ^ v  navigate   OK -- open   Back -- stop playback",
              19, rgba(72, 75, 105, 255));
}

void ui_home_key(const char *key) {
    if (!strcmp(key, "right") || !strcmp(key, "left")) {
        g_focused ^= 1;              /* flip column bit */
    } else if (!strcmp(key, "down") || !strcmp(key, "up")) {
        g_focused ^= 2;              /* flip row bit */
    } else if (!strcmp(key, "ok")) {
        if (TILES[g_focused].dest != SCREEN_HOME)
            navigate(TILES[g_focused].dest == SCREEN_IPTV     ? "iptv"     :
                     TILES[g_focused].dest == SCREEN_YOUTUBE  ? "youtube"  :
                     TILES[g_focused].dest == SCREEN_SETTINGS ? "settings" : "home");
    } else if (!strcmp(key, "back")) {
        mpv_core_stop();
    } else if (!strcmp(key, "play")) {
        mpv_core_pause_toggle();
    }
}

void ui_home_enter(void) { /* nothing to preload on home screen */ }

/* ── Settings screen ─────────────────────────────────────────────────────── */

#define N_SVCITEMS 2
static const char *SVC_NAMES[N_SVCITEMS]  = { "xray",       "tailscaled"    };
static const char *SVC_LABELS[N_SVCITEMS] = { "Xray proxy", "Tailscale VPN" };
static int g_settings_focused = 0;

void ui_settings_draw(void) {
    font_draw(60, 40, "Settings", 42, COL_ACCENT);
    font_draw(60, 100, "Service control — OK to toggle, Back to home", 22, COL_GRAY);

    const ServicesState *sv = services_get(0);
    int active_arr[N_SVCITEMS]  = { sv->xray_active,  sv->tailscale_active  };
    int enabled_arr[N_SVCITEMS] = { sv->xray_enabled, sv->tailscale_enabled };

    for (int i = 0; i < N_SVCITEMS; i++) {
        int row_y = 180 + i * 140;
        int row_w = g_screen_w - 120;

        uint32_t bg = (i == g_settings_focused) ? COL_TILE_HL : COL_TILE;
        render_rect(60, row_y, row_w, 110, bg);
        if (i == g_settings_focused)
            render_rect_outline(60, row_y, row_w, 110, COL_ACCENT, 3);

        /* Service label */
        font_draw(90, row_y + 18, SVC_LABELS[i], 30,
                  i == g_settings_focused ? COL_WHITE : COL_GRAY);

        /* Running / stopped */
        font_draw(90, row_y + 68,
                  active_arr[i] ? "running" : "stopped", 20,
                  active_arr[i] ? COL_ACCENT : COL_GRAY);

        /* ON / OFF toggle — right-aligned */
        const char *tog = enabled_arr[i] ? "[ON ]" : "[OFF]";
        float tw = font_measure(tog, 32);
        font_draw(60 + row_w - (int)tw - 30, row_y + 36, tog, 32,
                  enabled_arr[i] ? COL_ACCENT : COL_GRAY);
    }

    font_draw(60, g_screen_h - 48,
              "^ v select   OK toggle on/off   Back — home", 20, COL_GRAY);
}

void ui_settings_key(const char *key) {
    if (!strcmp(key, "up")) {
        g_settings_focused = (g_settings_focused - 1 + N_SVCITEMS) % N_SVCITEMS;
    } else if (!strcmp(key, "down")) {
        g_settings_focused = (g_settings_focused + 1) % N_SVCITEMS;
    } else if (!strcmp(key, "ok")) {
        const ServicesState *sv = services_get(0);
        int cur = (g_settings_focused == 0) ? sv->xray_enabled
                                             : sv->tailscale_enabled;
        services_set(SVC_NAMES[g_settings_focused], !cur);
    } else if (!strcmp(key, "back")) {
        g_screen = SCREEN_HOME;
        g_settings_focused = 0;
    }
}

void ui_settings_enter(void) {
    services_get(1);   /* force-refresh state on entry */
    g_settings_focused = 0;
}
