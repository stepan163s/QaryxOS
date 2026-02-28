#include "http_dl.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct { char *buf; size_t len; } Buf;

static size_t write_cb(void *data, size_t size, size_t nmemb, void *ud) {
    size_t bytes = size * nmemb;
    Buf *b = ud;
    char *p = realloc(b->buf, b->len + bytes + 1);
    if (!p) return 0;
    b->buf = p;
    memcpy(b->buf + b->len, data, bytes);
    b->len += bytes;
    b->buf[b->len] = '\0';
    return bytes;
}

char *http_dl_string(const char *url) {
    CURL *c = curl_easy_init();
    if (!c) return NULL;

    Buf b = {0};
    curl_easy_setopt(c, CURLOPT_URL,            url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,      &b);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT,        30L);
    curl_easy_setopt(c, CURLOPT_USERAGENT,      "QaryxOS/2.0");

    CURLcode res = curl_easy_perform(c);
    curl_easy_cleanup(c);

    if (res != CURLE_OK) {
        fprintf(stderr, "http_dl: %s: %s\n", url, curl_easy_strerror(res));
        free(b.buf);
        return NULL;
    }
    return b.buf;
}

int http_dl_file(const char *url, const char *dest_path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", dest_path);

    CURL *c = curl_easy_init();
    if (!c) return -1;

    FILE *f = fopen(tmp, "wb");
    if (!f) { curl_easy_cleanup(c); return -1; }

    curl_easy_setopt(c, CURLOPT_URL,            url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION,  fwrite);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,      f);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT,        120L);
    curl_easy_setopt(c, CURLOPT_USERAGENT,      "QaryxOS/2.0");

    CURLcode res = curl_easy_perform(c);
    fclose(f);
    curl_easy_cleanup(c);

    if (res != CURLE_OK) {
        fprintf(stderr, "http_dl_file: %s: %s\n", url, curl_easy_strerror(res));
        remove(tmp);
        return -1;
    }
    return rename(tmp, dest_path);
}
