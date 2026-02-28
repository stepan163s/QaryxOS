#include "iptv.h"
#include "home.h"
#include "../render.h"
#include "../font.h"
#include "../iptv.h"
#include "../mpv.h"
#include "../history.h"
#include <string.h>

#define ITEM_H    52
#define MARGIN_X  60
#define MARGIN_Y  130
#define GROUP_W   260
#define LIST_X    (MARGIN_X + GROUP_W + 30)
#define LIST_W    (g_screen_w - LIST_X - 60)
#define SEP_X     (MARGIN_X + GROUP_W + 14)

typedef enum { PANE_GROUPS, PANE_CHANNELS } Pane;

static Pane         g_pane       = PANE_GROUPS;
static int          g_group_idx  = 0;
static int          g_ch_idx     = 0;

static const char **g_groups     = NULL;
static int          g_group_n    = 0;
static IptvChannel *g_channels   = NULL;
static int          g_ch_n       = 0;

static void load_channels(void) {
    const char *grp = (g_groups && g_group_n > 0) ? g_groups[g_group_idx] : NULL;
    if (grp && !strcmp(grp, "All")) grp = NULL;
    g_channels = iptv_get_channels(NULL, grp, &g_ch_n);
    g_ch_idx = 0;
}

void ui_iptv_enter(void) {
    g_pane      = PANE_GROUPS;
    g_group_idx = 0;
    g_groups    = iptv_get_groups(&g_group_n);
    load_channels();
}

void ui_iptv_draw(void) {
    font_draw(MARGIN_X, 40, "📡  IPTV", 38, COL_ACCENT);
    font_draw(MARGIN_X, 90,
              "← → switch pane   ↑ ↓ navigate   OK play   Back home",
              20, COL_GRAY);

    /* Separator */
    render_rect(SEP_X, MARGIN_Y - 10, 2, g_screen_h - MARGIN_Y - 30, rgba(60,60,60,255));

    int visible = (g_screen_h - MARGIN_Y - 20) / ITEM_H;

    /* ── Groups pane ────────────────────────────────────────────────────── */
    int g_start = g_group_idx - visible/2;
    if (g_start < 0) g_start = 0;

    for (int i = 0; i < visible && (g_start + i) < g_group_n; i++) {
        int   idx = g_start + i;
        int   y   = MARGIN_Y + i * ITEM_H;
        int   sel = (idx == g_group_idx);
        int   act = (g_pane == PANE_GROUPS);

        if (sel) {
            uint32_t bg = act ? COL_TILE_HL : rgba(40,40,50,255);
            render_rect(MARGIN_X, y, GROUP_W, ITEM_H - 4, bg);
            if (act) render_rect_outline(MARGIN_X, y, GROUP_W, ITEM_H-4, COL_ACCENT, 2);
        }

        char label[24] = {0};
        strncpy(label, g_groups[idx], 22);
        font_draw(MARGIN_X + 10, y + 14, label, 22,
                  sel ? COL_WHITE : COL_GRAY);
    }

    /* ── Channels pane ───────────────────────────────────────────────────── */
    if (g_ch_n == 0) {
        font_draw(LIST_X, MARGIN_Y + 20, "No channels", 22, COL_GRAY);
    } else {
        int c_start = g_ch_idx - visible/2;
        if (c_start < 0) c_start = 0;

        for (int i = 0; i < visible && (c_start + i) < g_ch_n; i++) {
            int idx = c_start + i;
            int y   = MARGIN_Y + i * ITEM_H;
            int sel = (idx == g_ch_idx);
            int act = (g_pane == PANE_CHANNELS);

            if (sel) {
                uint32_t bg = act ? COL_TILE_HL : rgba(40,40,50,255);
                render_rect(LIST_X, y, LIST_W, ITEM_H-4, bg);
                if (act) render_rect_outline(LIST_X, y, LIST_W, ITEM_H-4, COL_ACCENT, 2);
            }

            char name[50] = {0};
            strncpy(name, g_channels[idx].name, 48);
            font_draw(LIST_X + 10, y + 14, name, 22,
                      sel ? COL_WHITE : COL_GRAY);
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
        if (g_pane == PANE_GROUPS && g_groups && g_group_idx < g_group_n - 1) {
            g_group_idx++;
            load_channels();
        } else if (g_pane == PANE_CHANNELS && g_channels && g_ch_idx < g_ch_n - 1) {
            g_ch_idx++;
        }
    } else if (!strcmp(key, "ok")) {
        if (g_pane == PANE_CHANNELS && g_channels && g_ch_n > 0) {
            IptvChannel *ch = &g_channels[g_ch_idx];
            mpv_core_load(ch->url, "live");
            history_record(ch->url, ch->name, "iptv", ch->name, ch->logo, 0);
        }
    } else if (!strcmp(key, "back") || !strcmp(key, "home")) {
        navigate("home");
    }
}
