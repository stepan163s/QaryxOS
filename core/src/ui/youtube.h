#pragma once
#include "../ytdlp.h"
void ui_youtube_draw(void);
void ui_youtube_key(const char *key);
void ui_youtube_enter(void);
/* Load video list fetched by a background thread (thread-safe). */
void ui_youtube_set_videos(YoutubeVideo *vids, int n);
