#pragma once
#include <stddef.h>
#include <time.h>

#define YTDLP_BIN     "/usr/local/bin/yt-dlp"
#define YTDLP_TIMEOUT 30     /* seconds */
#define CACHE_TTL     (4*3600)
#define CACHE_MAX     50

typedef struct {
    char    id[64];
    char    title[256];
    char    url[512];
    char    channel_name[128];
    int     duration;
    char    thumbnail[512];
} YoutubeVideo;

/* Callback invoked when URL is resolved (or on error).
   stream_url is NULL on failure. */
typedef void (*YtdlpCb)(const char *stream_url, void *userdata);

/* Asynchronously resolve a YouTube URL to a direct stream URL.
   On success, callback is called with the stream URL.
   Pipe fds are added to the app's epoll via ytdlp_get_pending_fd().
   Caches results for CACHE_TTL seconds. */
void ytdlp_resolve(const char *url, const char *quality,
                   YtdlpCb cb, void *userdata);

/* Call when any ytdlp pipe fd is readable. */
void ytdlp_dispatch(int fd);

/* Fetch latest N videos from a channel URL using --flat-playlist.
   Blocking (called in a separate thread for channel refresh).
   Returns number of videos filled in 'out'. */
int  ytdlp_get_channel_videos(const char *channel_url, int max,
                               YoutubeVideo *out);

/* Return all active pipe fds (for epoll registration). Count in n_out. */
int *ytdlp_pending_fds(int *n_out);
