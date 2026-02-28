#include "history.h"
#include "../third_party/cjson.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static HistoryEntry g_entries[HISTORY_MAX];
static int          g_count = 0;

void history_load(void) {
    g_count = 0;
    FILE *f = fopen(HISTORY_FILE, "r");
    if (!f) return;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz + 1); fread(buf, 1, sz, f); buf[sz] = '\0'; fclose(f);

    cJSON *arr = cJSON_Parse(buf); free(buf);
    if (!arr) return;
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n && g_count < HISTORY_MAX; i++) {
        cJSON *o = cJSON_GetArrayItem(arr, i);
        HistoryEntry *e = &g_entries[g_count++];
        strncpy(e->url,          cJSON_GetString(o,"url",""),          sizeof(e->url)-1);
        strncpy(e->title,        cJSON_GetString(o,"title",""),        sizeof(e->title)-1);
        strncpy(e->content_type, cJSON_GetString(o,"content_type","direct"), sizeof(e->content_type)-1);
        strncpy(e->channel_name, cJSON_GetString(o,"channel_name",""), sizeof(e->channel_name)-1);
        strncpy(e->thumbnail,    cJSON_GetString(o,"thumbnail",""),    sizeof(e->thumbnail)-1);
        e->duration  = cJSON_GetNumber(o,"duration",0);
        e->position  = cJSON_GetNumber(o,"position",0);
        e->played_at = (time_t)cJSON_GetNumber(o,"played_at",0);
    }
    cJSON_Delete(arr);
}

void history_save(void) {
    /* Ensure directory exists */
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < g_count; i++) {
        HistoryEntry *e = &g_entries[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "url",          e->url);
        cJSON_AddStringToObject(o, "title",        e->title);
        cJSON_AddStringToObject(o, "content_type", e->content_type);
        cJSON_AddStringToObject(o, "channel_name", e->channel_name);
        cJSON_AddStringToObject(o, "thumbnail",    e->thumbnail);
        cJSON_AddNumberToObject(o, "duration",     e->duration);
        cJSON_AddNumberToObject(o, "position",     e->position);
        cJSON_AddNumberToObject(o, "played_at",    (double)e->played_at);
        cJSON_AddItemToArray(arr, o);
    }
    char *s = cJSON_Print(arr); cJSON_Delete(arr);

    char tmp[512]; snprintf(tmp, sizeof(tmp), "%s.tmp", HISTORY_FILE);
    FILE *f = fopen(tmp, "w");
    if (f) { fputs(s, f); fclose(f); rename(tmp, HISTORY_FILE); }
    free(s);
}

void history_record(const char *url, const char *title,
                    const char *content_type, const char *channel_name,
                    const char *thumbnail, double duration) {
    /* Remove existing entry with same URL */
    for (int i = 0; i < g_count; i++) {
        if (!strcmp(g_entries[i].url, url)) {
            memmove(g_entries + i, g_entries + i + 1,
                    (g_count - i - 1) * sizeof(HistoryEntry));
            g_count--;
            break;
        }
    }

    /* Evict oldest if full */
    if (g_count >= HISTORY_MAX) g_count = HISTORY_MAX - 1;

    /* Shift and insert at front */
    memmove(g_entries + 1, g_entries, g_count * sizeof(HistoryEntry));
    HistoryEntry *e = &g_entries[0];
    memset(e, 0, sizeof(*e));
    strncpy(e->url,          url,          sizeof(e->url)-1);
    strncpy(e->title,        title ? title : url, sizeof(e->title)-1);
    strncpy(e->content_type, content_type ? content_type : "direct", sizeof(e->content_type)-1);
    strncpy(e->channel_name, channel_name ? channel_name : "", sizeof(e->channel_name)-1);
    strncpy(e->thumbnail,    thumbnail    ? thumbnail    : "", sizeof(e->thumbnail)-1);
    e->duration  = duration;
    e->position  = 0;
    e->played_at = time(NULL);
    g_count++;

    history_save();
}

void history_update_position(const char *url, double position) {
    for (int i = 0; i < g_count; i++) {
        if (!strcmp(g_entries[i].url, url)) {
            g_entries[i].position = position;
            history_save();
            return;
        }
    }
}

HistoryEntry *history_get_all(int *n) { if (n) *n = g_count; return g_entries; }
HistoryEntry *history_get_last(void)  { return g_count > 0 ? &g_entries[0] : NULL; }

void history_clear(void) { g_count = 0; history_save(); }
