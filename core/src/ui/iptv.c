#include "iptv.h"
#include "home.h"
#include "../render.h"
#include "../font.h"
#include "../iptv.h"
#include "../mpv.h"
#include "../history.h"
#include <string.h>
#include <stdio.h>

/* ── Layout constants (1920×1080 base) ─────────────────────────────────── */
#define HEADER_H   112   /* height of top header bar */
#define FOOTER_H    44   /* height of bottom hint bar */
#define MARGIN_X    48
#define GROUP_W    288   /* width of the left groups pane */
#define SEP_W        2   /* separator between panes */
#define SEP_X       (MARGIN_X + GROUP_W)
#define LIST_X      (SEP_X + SEP_W + 10)
#define ITEM_H      56   /* row height for both panes */
#define NUM_W       56   /* channel number column width */

typedef enum { PANE_GROUPS, PANE_CHANNELS } Pane;

static Pane         g_pane      = PANE_GROUPS;
static int          g_group_idx = 0;   /* 0 = "All channels" virtual group */
static int          g_ch_idx    = 0;

static const char **g_groups    = NULL;
static int          g_group_n   = 0;   /* count of real groups */

static IptvChannel *g_channels  = NULL;
static int          g_ch_n      = 0;

/* URL of the channel currently being played — for the "> playing" indicator */
static char g_playing_url[512] = "";

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* Total virtual group count including the "All channels" entry at index 0. */
static int total_groups(void) { return g_group_n + 1; }

/* Label for a virtual group index. */
static const char *group_label(int idx) {
    if (idx == 0) return "All channels";
    return g_groups[idx - 1];
}

static void load_channels(void) {
    const char *grp = (g_group_idx == 0) ? NULL : g_groups[g_group_idx - 1];
    g_channels = iptv_get_channels(NULL, grp, &g_ch_n);
    g_ch_idx   = 0;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void ui_iptv_enter(void) {
    g_pane      = PANE_GROUPS;
    g_group_idx = 0;
    g_groups    = iptv_get_groups(&g_group_n);
    load_channels();
}

void ui_iptv_draw(void) {
    int W = g_screen_w;
    int H = g_screen_h;
    int list_w    = W - LIST_X - MARGIN_X;
    int content_h = H - HEADER_H - FOOTER_H;
    int visible   = content_h / ITEM_H;

    /* ── Header ────────────────────────────────────────────────────────── */
    font_draw(MARGIN_X, 24, "IPTV", 54, COL_ACCENT);

    /* Group name + channel count on the right side of header */
    char hdr[96];
    snprintf(hdr, sizeof(hdr), "%s  /  %d", group_label(g_group_idx), g_ch_n);
    font_draw(LIST_X, 38, hdr, 24, COL_GRAY);

    /* Thin separator below header */
    render_rect(0, HEADER_H, W, 1, rgba(50, 50, 70, 255));

    /* ── Groups pane ───────────────────────────────────────────────────── */
    int g_total = total_groups();
    int g_start = g_group_idx - visible / 2;
    if (g_start < 0) g_start = 0;

    for (int i = 0; i < visible && (g_start + i) < g_total; i++) {
        int idx = g_start + i;
        int y   = HEADER_H + i * ITEM_H;
        int sel = (idx == g_group_idx);
        int act = sel && (g_pane == PANE_GROUPS);

        if (sel) {
            uint32_t bg = act ? COL_ACCENT : rgba(38, 38, 58, 255);
            render_rect(MARGIN_X + 4, y + 5, GROUP_W - 8, ITEM_H - 10, bg);
            if (act)
                render_rect_outline(MARGIN_X + 4, y + 5,
                                    GROUP_W - 8, ITEM_H - 10, COL_ACCENT, 2);
        }

        char label[34] = {0};
        strncpy(label, group_label(idx), 32);
        uint32_t col = act  ? COL_WHITE :
                       sel  ? rgba(180, 195, 255, 255) :
                               COL_GRAY;
        font_draw(MARGIN_X + 14, y + ITEM_H / 2 - 12, label, 22, col);
    }

    /* Groups scroll indicator (thin bar on the far left) */
    if (g_total > visible) {
        int bar_h = content_h * visible / g_total;
        int bar_y = HEADER_H + content_h * g_start / g_total;
        render_rect(MARGIN_X, HEADER_H, 3, content_h, rgba(30, 30, 45, 255));
        render_rect(MARGIN_X, bar_y, 3, bar_h, rgba(80, 90, 160, 255));
    }

    /* Vertical separator between panes */
    render_rect(SEP_X, HEADER_H, SEP_W, content_h, rgba(45, 45, 65, 255));

    /* ── Channels pane ─────────────────────────────────────────────────── */
    if (g_ch_n == 0) {
        font_draw(LIST_X + 20, HEADER_H + 40, "No channels", 24, COL_GRAY);
    } else {
        int c_start = g_ch_idx - visible / 2;
        if (c_start < 0) c_start = 0;

        for (int i = 0; i < visible && (c_start + i) < g_ch_n; i++) {
            int idx     = c_start + i;
            int y       = HEADER_H + i * ITEM_H;
            int sel     = (idx == g_ch_idx);
            int act     = sel && (g_pane == PANE_CHANNELS);
            int playing = g_playing_url[0] &&
                          !strcmp(g_channels[idx].url, g_playing_url);

            /* Row background */
            if (act) {
                render_rect(LIST_X, y + 4, list_w, ITEM_H - 8, COL_TILE_HL);
                render_rect_outline(LIST_X, y + 4, list_w, ITEM_H - 8,
                                    COL_ACCENT, 2);
            } else if (sel) {
                render_rect(LIST_X, y + 4, list_w, ITEM_H - 8,
                            rgba(38, 38, 56, 255));
            } else if (playing) {
                render_rect(LIST_X, y + 4, list_w, ITEM_H - 8,
                            rgba(18, 42, 18, 255));
            }

            /* Channel number */
            char num[8];
            snprintf(num, sizeof(num), "%d", idx + 1);
            font_draw(LIST_X + 8, y + ITEM_H / 2 - 11, num, 19,
                      act ? COL_ACCENT : rgba(90, 90, 115, 255));

            /* Channel name */
            char name[54] = {0};
            strncpy(name, g_channels[idx].name, 52);
            uint32_t col = act     ? COL_WHITE  :
                           sel     ? rgba(220, 225, 255, 255) :
                           playing ? rgba(110, 210, 110, 255) :
                                     rgba(185, 185, 200, 255);
            font_draw(LIST_X + NUM_W, y + ITEM_H / 2 - 11, name, 24, col);

            /* Playing indicator */
            if (playing) {
                float pw = font_measure(">", 22);
                font_draw(LIST_X + list_w - (int)pw - 12,
                          y + ITEM_H / 2 - 11, ">", 22,
                          rgba(90, 210, 90, 255));
            }
        }

        /* Channels scroll bar (right edge) */
        if (g_ch_n > visible) {
            int bar_h = content_h * visible / g_ch_n;
            int bar_y = HEADER_H + content_h * c_start / g_ch_n;
            render_rect(W - MARGIN_X + 2, HEADER_H,
                        4, content_h, rgba(28, 28, 42, 255));
            render_rect(W - MARGIN_X + 2, bar_y,
                        4, bar_h,     rgba(80, 90, 160, 255));
        }
    }

    /* ── Footer ────────────────────────────────────────────────────────── */
    render_rect(0, H - FOOTER_H, W, 1, rgba(50, 50, 70, 255));

    const char *hint = (g_pane == PANE_GROUPS)
        ? "up/down: group   right: channels   back: home"
        : "up/down: channel   left: groups   ok: play   back: home";
    font_draw(MARGIN_X, H - FOOTER_H + 13, hint, 19, COL_GRAY);

    /* Now-playing name (right side of footer) */
    if (g_playing_url[0]) {
        for (int i = 0; i < g_ch_n; i++) {
            if (!strcmp(g_channels[i].url, g_playing_url)) {
                char np[80];
                snprintf(np, sizeof(np), "> %.50s", g_channels[i].name);
                float tw = font_measure(np, 19);
                font_draw((int)(W - tw - MARGIN_X), H - FOOTER_H + 13,
                          np, 19, rgba(90, 210, 90, 255));
                break;
            }
        }
    }
}

void ui_iptv_key(const char *key) {
    if (!strcmp(key, "right") && g_pane == PANE_GROUPS) {
        g_pane = PANE_CHANNELS;

    } else if (!strcmp(key, "left") && g_pane == PANE_CHANNELS) {
        g_pane = PANE_GROUPS;

    } else if (!strcmp(key, "up")) {
        if (g_pane == PANE_GROUPS && g_group_idx > 0) {
            g_group_idx--;
            load_channels();
        } else if (g_pane == PANE_CHANNELS && g_ch_idx > 0) {
            g_ch_idx--;
        }

    } else if (!strcmp(key, "down")) {
        if (g_pane == PANE_GROUPS && g_group_idx < total_groups() - 1) {
            g_group_idx++;
            load_channels();
        } else if (g_pane == PANE_CHANNELS &&
                   g_channels && g_ch_idx < g_ch_n - 1) {
            g_ch_idx++;
        }

    } else if (!strcmp(key, "ok")) {
        if (g_pane == PANE_CHANNELS && g_channels && g_ch_n > 0) {
            IptvChannel *ch = &g_channels[g_ch_idx];
            strncpy(g_playing_url, ch->url, sizeof(g_playing_url) - 1);
            mpv_core_load(ch->url, "live");
            history_record(ch->url, ch->name, "iptv", ch->name, ch->logo, 0);
        } else if (g_pane == PANE_GROUPS) {
            /* Enter channels pane on OK from groups */
            g_pane = PANE_CHANNELS;
        }

    } else if (!strcmp(key, "back") || !strcmp(key, "home")) {
        /* Stop playback before going home so the home screen renders */
        mpv_core_stop();
        navigate("home");
    }
}
