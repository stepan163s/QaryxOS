#include "ytdlp.h"
#include "../third_party/cjson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>

/* ── URL cache ────────────────────────────────────────────────────────────── */

typedef struct {
    char   orig[512];
    char   stream[2048];
    time_t resolved_at;
} CacheEntry;

static CacheEntry g_cache[CACHE_MAX];
static int        g_cache_n = 0;

static const char *cache_get(const char *url) {
    time_t now = time(NULL);
    for (int i = 0; i < g_cache_n; i++) {
        if (!strcmp(g_cache[i].orig, url)) {
            if (now - g_cache[i].resolved_at < CACHE_TTL)
                return g_cache[i].stream;
            /* Expired — remove */
            g_cache[i] = g_cache[--g_cache_n];
            return NULL;
        }
    }
    return NULL;
}

static void cache_set(const char *url, const char *stream) {
    if (g_cache_n >= CACHE_MAX) {
        /* Evict oldest */
        int oldest = 0;
        for (int i = 1; i < g_cache_n; i++)
            if (g_cache[i].resolved_at < g_cache[oldest].resolved_at) oldest = i;
        g_cache[oldest] = g_cache[--g_cache_n];
    }
    strncpy(g_cache[g_cache_n].orig,   url,    sizeof(g_cache[0].orig)-1);
    strncpy(g_cache[g_cache_n].stream, stream, sizeof(g_cache[0].stream)-1);
    g_cache[g_cache_n].resolved_at = time(NULL);
    g_cache_n++;
}

/* ── Pending async resolves ───────────────────────────────────────────────── */

#define MAX_PENDING 8

typedef struct {
    int     pipe_fd;  /* read end of stdout pipe */
    pid_t   pid;
    char    url[512];
    char    rbuf[4096];
    int     rlen;
    YtdlpCb cb;
    void   *userdata;
} Pending;

static Pending g_pending[MAX_PENDING];
static int     g_pending_n = 0;
static char    g_proxy[256] = "";

void ytdlp_set_proxy(const char *proxy_url) {
    if (proxy_url && proxy_url[0])
        strncpy(g_proxy, proxy_url, sizeof(g_proxy)-1);
    else
        g_proxy[0] = '\0';
    fprintf(stderr, "ytdlp: proxy=%s\n", g_proxy[0] ? g_proxy : "(none)");
}

static Pending *find_pending(int fd) {
    for (int i = 0; i < g_pending_n; i++)
        if (g_pending[i].pipe_fd == fd) return &g_pending[i];
    return NULL;
}

static void remove_pending(Pending *p) {
    ptrdiff_t idx = p - g_pending;
    if (p->pipe_fd >= 0) close(p->pipe_fd);
    g_pending[idx] = g_pending[--g_pending_n];
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void ytdlp_resolve(const char *url, const char *quality,
                   YtdlpCb cb, void *userdata) {
    const char *cached = cache_get(url);
    if (cached) { if (cb) cb(cached, userdata); return; }

    if (g_pending_n >= MAX_PENDING) {
        fprintf(stderr, "ytdlp: too many pending resolves\n");
        if (cb) cb(NULL, userdata);
        return;
    }

    char fmt[128];
    snprintf(fmt, sizeof(fmt),
             "bestvideo[height<=%s]+bestaudio/bestvideo+bestaudio/best",
             quality ? quality : "1080");

    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) < 0) { if (cb) cb(NULL, userdata); return; }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        if (cb) cb(NULL, userdata);
        return;
    }

    if (pid == 0) {
        /* Child */
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]); close(pipefd[1]);
        /* Redirect stderr to /dev/null */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, STDERR_FILENO);
        if (g_proxy[0]) {
            execlp(YTDLP_BIN, "yt-dlp",
                   "--no-warnings", "--no-playlist",
                   "--proxy", g_proxy,
                   "-f", fmt, "--get-url", url, NULL);
        } else {
            execlp(YTDLP_BIN, "yt-dlp",
                   "--no-warnings", "--no-playlist",
                   "-f", fmt, "--get-url", url, NULL);
        }
        _exit(1);
    }

    close(pipefd[1]);
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);

    Pending *p = &g_pending[g_pending_n++];
    p->pipe_fd = pipefd[0];
    p->pid     = pid;
    strncpy(p->url, url, sizeof(p->url)-1);
    p->rlen    = 0;
    p->cb      = cb;
    p->userdata= userdata;
}

void ytdlp_dispatch(int fd) {
    Pending *p = find_pending(fd);
    if (!p) return;

    int space = (int)sizeof(p->rbuf) - p->rlen - 1;
    ssize_t n = read(fd, p->rbuf + p->rlen, space);

    if (n > 0) {
        p->rlen += n;
        p->rbuf[p->rlen] = '\0';
        return;  /* Wait for EOF */
    }

    /* EOF or error — process result */
    int   status  = 0;
    waitpid(p->pid, &status, WNOHANG);

    /* Extract first non-empty line */
    char *result = NULL;
    char  tmp[2048];
    strncpy(tmp, p->rbuf, sizeof(tmp)-1);
    char *line = strtok(tmp, "\n");
    while (line) {
        /* Trim whitespace */
        while (*line == ' ' || *line == '\r') line++;
        char *end = line + strlen(line) - 1;
        while (end > line && (*end == ' ' || *end == '\r' || *end == '\n')) *end-- = '\0';
        if (*line) { result = line; break; }
        line = strtok(NULL, "\n");
    }

    if (result && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        cache_set(p->url, result);
        if (p->cb) p->cb(result, p->userdata);
    } else {
        fprintf(stderr, "ytdlp: resolve failed for %s\n", p->url);
        if (p->cb) p->cb(NULL, p->userdata);
    }

    remove_pending(p);
}

int *ytdlp_pending_fds(int *n_out) {
    static int fds[MAX_PENDING];
    *n_out = g_pending_n;
    for (int i = 0; i < g_pending_n; i++) fds[i] = g_pending[i].pipe_fd;
    return fds;
}

/* ── Synchronous channel video fetch ─────────────────────────────────────── */

int ytdlp_get_channel_videos(const char *channel_url, int max, YoutubeVideo *out) {
    char max_str[16];
    snprintf(max_str, sizeof(max_str), "%d", max);

    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) < 0) return 0;

    pid_t pid = fork();
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]); close(pipefd[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, STDERR_FILENO);
        execlp(YTDLP_BIN, "yt-dlp",
               "--no-warnings", "--flat-playlist",
               "--playlist-end", max_str,
               "--dump-json", channel_url, NULL);
        _exit(1);
    }
    close(pipefd[1]);

    /* Read all output (blocking) */
    char buf[256*1024];
    int  len = 0;
    ssize_t n;
    while ((n = read(pipefd[0], buf+len, sizeof(buf)-len-1)) > 0) len += n;
    buf[len] = '\0';
    close(pipefd[0]);
    waitpid(pid, NULL, 0);

    int count = 0;
    char *line = buf;
    while (count < max && line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        if (*line == '{') {
            cJSON *j = cJSON_Parse(line);
            if (j) {
                YoutubeVideo *v = &out[count];
                memset(v, 0, sizeof(*v));
                strncpy(v->id,           cJSON_GetString(j,"id",""),       sizeof(v->id)-1);
                strncpy(v->title,        cJSON_GetString(j,"title",""),    sizeof(v->title)-1);
                strncpy(v->channel_name, cJSON_GetString(j,"channel",""),  sizeof(v->channel_name)-1);
                v->duration = (int)cJSON_GetNumber(j,"duration",0);

                snprintf(v->url, sizeof(v->url),
                         "https://www.youtube.com/watch?v=%s", v->id);

                /* Thumbnail: last item in thumbnails array */
                cJSON *thumbs = cJSON_GetObjectItem(j, "thumbnails");
                if (thumbs && cJSON_GetArraySize(thumbs) > 0) {
                    cJSON *last = cJSON_GetArrayItem(thumbs, cJSON_GetArraySize(thumbs)-1);
                    strncpy(v->thumbnail, cJSON_GetString(last,"url",""), sizeof(v->thumbnail)-1);
                }

                cJSON_Delete(j);
                count++;
            }
        }
        line = nl ? nl+1 : NULL;
    }

    return count;
}
