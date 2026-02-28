#pragma once
#include <stdint.h>

#define CONFIG_FILE "/etc/qaryxos/config.json"

typedef struct {
    uint16_t ws_port;           /* WebSocket port, default 8080 */
    char     data_dir[256];     /* /var/lib/qaryxos */
    char     font_path[256];    /* TTF path */
    int      volume;            /* 0-100, default 80 */
    int      screen_w;
    int      screen_h;
    char     ytdlp_proxy[256];  /* optional HTTP proxy for yt-dlp, e.g. http://127.0.0.1:10809 */
} Config;

/* Load config from CONFIG_FILE. Missing keys get defaults. */
void config_load(Config *cfg);
