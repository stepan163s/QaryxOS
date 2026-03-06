#pragma once
#include <GLES2/gl2.h>

/* Initialize thumbnail cache. Creates cache dir under data_dir/thumbcache/.
   Must be called before any other thumbcache_* function. */
void thumbcache_init(const char *data_dir);

/* Return GL texture for thumbnail URL, or 0 if not yet ready.
   Triggers async download + decode on first call for a given URL.
   Call each frame — returns 0 while loading, texture id once ready. */
GLuint thumbcache_get(const char *url);

/* Upload any decoded thumbnails to GL (MUST be called from the main/GL thread).
   Call once per frame before drawing. */
void thumbcache_tick(void);
