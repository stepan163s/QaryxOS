#pragma once
#include <time.h>

#define HISTORY_FILE    "/var/lib/qaryxos/history.json"
#define HISTORY_MAX     50

typedef struct {
    char   url[512];
    char   title[256];
    char   content_type[16];  /* "youtube" | "iptv" | "direct" */
    char   channel_name[128];
    char   thumbnail[512];
    double duration;
    double position;
    time_t played_at;
} HistoryEntry;

/* Load history from disk into internal buffer. */
void history_load(void);

/* Save internal buffer to disk (atomic write). */
void history_save(void);

/* Add or update an entry (deduplicates by URL). Moves to front. */
void history_record(const char *url, const char *title,
                    const char *content_type, const char *channel_name,
                    const char *thumbnail, double duration);

/* Update the playback position for a URL. */
void history_update_position(const char *url, double position);

/* Read-only access. count_out may be NULL. */
HistoryEntry *history_get_all(int *count_out);
HistoryEntry *history_get_last(void);

void history_clear(void);
