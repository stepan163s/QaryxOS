#pragma once
#include <stddef.h>

/* Call once at startup (before any other http_dl_* call).
   Initialises libcurl global state and the shared TLS/connection cache. */
void http_dl_init(void);

/* Download URL content into a heap-allocated string (caller must free()).
   proxy: optional HTTP/SOCKS proxy URL (e.g. "http://127.0.0.1:10809"), or NULL.
   Returns NULL on error. Uses libcurl synchronously. */
char *http_dl_string(const char *url, const char *proxy);

/* Download URL content to a file path.
   Returns 0 on success. */
int http_dl_file(const char *url, const char *dest_path);
