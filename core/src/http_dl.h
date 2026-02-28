#pragma once
#include <stddef.h>

/* Download URL content into a heap-allocated string (caller must free()).
   Returns NULL on error. Uses libcurl synchronously. */
char *http_dl_string(const char *url);

/* Download URL content to a file path.
   Returns 0 on success. */
int http_dl_file(const char *url, const char *dest_path);
