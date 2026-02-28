#include "config.h"
#include "../third_party/cjson.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void set_defaults(Config *cfg) {
    cfg->ws_port  = 8080;
    strcpy(cfg->data_dir,  "/var/lib/qaryxos");
    strcpy(cfg->font_path,
           "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    cfg->volume   = 80;
    cfg->screen_w = 1920;
    cfg->screen_h = 1080;
}

void config_load(Config *cfg) {
    set_defaults(cfg);

    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) {
        fprintf(stderr, "config: %s not found, using defaults\n", CONFIG_FILE);
        return;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    cJSON *j = cJSON_Parse(buf);
    free(buf);
    if (!j) { fprintf(stderr, "config: JSON parse error\n"); return; }

    cfg->ws_port  = (uint16_t)cJSON_GetNumber(j, "ws_port",  cfg->ws_port);
    cfg->volume   = (int)     cJSON_GetNumber(j, "volume",   cfg->volume);
    cfg->screen_w = (int)     cJSON_GetNumber(j, "screen_w", cfg->screen_w);
    cfg->screen_h = (int)     cJSON_GetNumber(j, "screen_h", cfg->screen_h);

    const char *s;
    if ((s = cJSON_GetString(j, "data_dir",    NULL))) strncpy(cfg->data_dir,    s, sizeof(cfg->data_dir)-1);
    if ((s = cJSON_GetString(j, "font_path",   NULL))) strncpy(cfg->font_path,   s, sizeof(cfg->font_path)-1);
    if ((s = cJSON_GetString(j, "ytdlp_proxy", NULL))) strncpy(cfg->ytdlp_proxy, s, sizeof(cfg->ytdlp_proxy)-1);

    cJSON_Delete(j);
}
