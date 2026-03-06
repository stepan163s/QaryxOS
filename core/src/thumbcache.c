#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_FAILURE_STRINGS
#include "stb_image.h"

#include "thumbcache.h"
#include "http_dl.h"
#include "sha1.h"
#include <GLES2/gl2.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THUMB_TTL         (7 * 24 * 3600) /* 7 days disk cache */
#define THUMB_MAX         64              /* max textures in memory */
#define UPLOAD_QUEUE_SZ   8               /* pending GL uploads */

/* ── In-memory entry ─────────────────────────────────────────────────────── */

typedef struct {
    char   url[512];
    GLuint tex;       /* 0 = not uploaded yet */
    int    loading;   /* 1 = download/decode thread running */
    int    failed;    /* 1 = last attempt failed; retry after RETRY_DELAY */
    time_t last_used;
    time_t failed_at;
} ThumbEntry;

#define RETRY_DELAY 60 /* seconds before retrying a failed thumbnail */

/* ── Upload queue: produced by threads, consumed by main thread in tick() ── */

typedef struct {
    char           url[512];
    unsigned char *pixels; /* RGBA malloc'd — freed after GL upload */
    int            w, h;
} UploadJob;

static char        g_cache_dir[512];
static ThumbEntry  g_entries[THUMB_MAX];
static int         g_n_entries = 0;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

static UploadJob       g_queue[UPLOAD_QUEUE_SZ];
static int             g_q_head = 0, g_q_tail = 0;
static pthread_mutex_t g_q_mu = PTHREAD_MUTEX_INITIALIZER;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void url_to_cache_path(const char *url, char *out, size_t outsz) {
    uint8_t digest[20];
    sha1((const uint8_t *)url, strlen(url), digest);
    int n = snprintf(out, outsz, "%s/", g_cache_dir);
    for (int i = 0; i < 20 && n + 2 < (int)outsz; i++, n += 2)
        snprintf(out + n, outsz - n, "%02x", digest[i]);
    strncat(out, ".jpg", outsz - strlen(out) - 1);
}

/* ── Background download + decode thread ─────────────────────────────────── */

typedef struct { char url[512]; char path[512]; } ThreadArg;

static void entry_set_failed(const char *url) {
    pthread_mutex_lock(&g_mu);
    for (int i = 0; i < g_n_entries; i++) {
        if (!strcmp(g_entries[i].url, url)) {
            g_entries[i].loading  = 0;
            g_entries[i].failed   = 1;
            g_entries[i].failed_at = time(NULL);
            break;
        }
    }
    pthread_mutex_unlock(&g_mu);
}

static void *download_thread(void *arg) {
    ThreadArg *a = arg;

    /* Check / download disk cache */
    struct stat st;
    int need_dl = 1;
    if (stat(a->path, &st) == 0) {
        if ((time(NULL) - st.st_mtime) < THUMB_TTL)
            need_dl = 0;
    }
    if (need_dl && http_dl_file(a->url, a->path) != 0) {
        entry_set_failed(a->url);
        free(a);
        return NULL;
    }

    /* Decode with stb_image */
    int w, h, ch;
    unsigned char *pixels = stbi_load(a->path, &w, &h, &ch, 4);
    if (!pixels) {
        entry_set_failed(a->url);
        free(a);
        return NULL;
    }

    /* Enqueue for GL upload on main thread */
    pthread_mutex_lock(&g_q_mu);
    int next = (g_q_tail + 1) % UPLOAD_QUEUE_SZ;
    if (next != g_q_head) {
        UploadJob *j = &g_queue[g_q_tail];
        strncpy(j->url, a->url, sizeof(j->url) - 1);
        j->url[sizeof(j->url)-1] = '\0';
        j->pixels = pixels;
        j->w = w;
        j->h = h;
        g_q_tail = next;
    } else {
        /* queue full — discard pixels; will retry on next thumbcache_get() call */
        stbi_image_free(pixels);
        entry_set_failed(a->url);
    }
    pthread_mutex_unlock(&g_q_mu);

    free(a);
    return NULL;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void thumbcache_init(const char *data_dir) {
    snprintf(g_cache_dir, sizeof(g_cache_dir), "%s/thumbcache", data_dir);
    mkdir(g_cache_dir, 0755); /* ignore error if already exists */
    memset(g_entries, 0, sizeof(g_entries));
}

static void spawn_download(const char *url) {
    ThreadArg *a = malloc(sizeof(ThreadArg));
    if (!a) { entry_set_failed(url); return; }
    strncpy(a->url, url, sizeof(a->url) - 1);
    a->url[sizeof(a->url)-1] = '\0';
    url_to_cache_path(url, a->path, sizeof(a->path));
    pthread_t t;
    if (pthread_create(&t, NULL, download_thread, a) == 0)
        pthread_detach(t);
    else {
        entry_set_failed(url);
        free(a);
    }
}

GLuint thumbcache_get(const char *url) {
    if (!url || !url[0]) return 0;

    pthread_mutex_lock(&g_mu);
    time_t now = time(NULL);

    /* Search existing entry */
    for (int i = 0; i < g_n_entries; i++) {
        if (!strcmp(g_entries[i].url, url)) {
            g_entries[i].last_used = now;

            if (g_entries[i].tex) {          /* ready */
                GLuint t = g_entries[i].tex;
                pthread_mutex_unlock(&g_mu);
                return t;
            }
            if (g_entries[i].loading) {      /* in progress */
                pthread_mutex_unlock(&g_mu);
                return 0;
            }
            /* Failed: retry after RETRY_DELAY */
            if (g_entries[i].failed &&
                (now - g_entries[i].failed_at) >= RETRY_DELAY) {
                g_entries[i].loading = 1;
                g_entries[i].failed  = 0;
                pthread_mutex_unlock(&g_mu);
                spawn_download(url);
            } else {
                pthread_mutex_unlock(&g_mu);
            }
            return 0;
        }
    }

    /* New entry: evict LRU if full */
    int slot;
    if (g_n_entries < THUMB_MAX) {
        slot = g_n_entries++;
    } else {
        slot = 0;
        for (int i = 1; i < THUMB_MAX; i++)
            if (g_entries[i].last_used < g_entries[slot].last_used)
                slot = i;
        if (g_entries[slot].tex)
            glDeleteTextures(1, &g_entries[slot].tex);
    }

    ThumbEntry *e = &g_entries[slot];
    memset(e, 0, sizeof(*e));
    strncpy(e->url, url, sizeof(e->url) - 1);
    e->loading   = 1;
    e->last_used = now;

    pthread_mutex_unlock(&g_mu);
    spawn_download(url);
    return 0;
}

void thumbcache_tick(void) {
    /* Upload all pending decoded images to GL (must run on GL thread) */
    pthread_mutex_lock(&g_q_mu);
    while (g_q_head != g_q_tail) {
        UploadJob *j = &g_queue[g_q_head];
        g_q_head = (g_q_head + 1) % UPLOAD_QUEUE_SZ;
        pthread_mutex_unlock(&g_q_mu);

        /* Create GL texture */
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, j->w, j->h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, j->pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        stbi_image_free(j->pixels);
        j->pixels = NULL;

        /* Store tex id in entry */
        pthread_mutex_lock(&g_mu);
        for (int i = 0; i < g_n_entries; i++) {
            if (!strcmp(g_entries[i].url, j->url)) {
                g_entries[i].tex     = tex;
                g_entries[i].loading = 0;
                break;
            }
        }
        pthread_mutex_unlock(&g_mu);

        pthread_mutex_lock(&g_q_mu);
    }
    pthread_mutex_unlock(&g_q_mu);
}
