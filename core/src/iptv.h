#pragma once
#include <time.h>

#define IPTV_MAX_CHANNELS  5000
#define IPTV_MAX_PLAYLISTS 16
#define IPTV_CACHE_DIR     "/var/lib/qaryxos/iptv"
#define IPTV_PLAYLISTS_FILE "/var/lib/qaryxos/playlists.json"

typedef struct {
    char id[64];
    char name[128];
    char url[512];
    char group[64];
    char logo[256];
    char playlist_id[32];
} IptvChannel;

typedef struct {
    char   id[32];
    char   name[64];
    char   url[512];
    time_t updated_at;
    int    channel_count;
} IptvPlaylist;

/* Load playlists + channel caches from disk. */
void iptv_load(void);

/* Save playlists index to disk. */
void iptv_save_playlists(void);

/* Download M3U, parse, store cache. Returns channel count or -1. */
int  iptv_add_playlist(const char *url, const char *name);

/* Remove playlist and its cache. Returns 1 if found. */
int  iptv_remove_playlist(const char *id);

/* Refresh a single playlist from network. Returns channel count or -1. */
int  iptv_refresh_playlist(const char *id);

/* Import channels from a pre-parsed JSON array (file-upload path).
   channels_json: cJSON* array of {"name":"...","url":"...","group":"..."}.
   Returns channel count or -1. */
int  iptv_import_channels(const char *name, void *channels_cjson_array);

/* Queries */
IptvPlaylist *iptv_get_playlists(int *count_out);
IptvChannel  *iptv_get_channels(const char *playlist_id, const char *group,
                                 int *count_out);
IptvChannel  *iptv_get_channel(const char *id);
const char  **iptv_get_groups(int *count_out);
