#include "iptv.h"
#include "http_dl.h"
#include "../third_party/cjson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

/* ── In-memory state ──────────────────────────────────────────────────────── */

static IptvPlaylist g_playlists[IPTV_MAX_PLAYLISTS];
static int          g_pl_count = 0;

static IptvChannel  g_channels[IPTV_MAX_CHANNELS];
static int          g_ch_count = 0;

/* ── M3U parser ───────────────────────────────────────────────────────────── */

static int parse_m3u(const char *content, const char *pl_id,
                      IptvChannel *out, int max) {
    int count = 0;
    const char *p = content;
    IptvChannel cur;
    int has_meta = 0;

    while (*p && count < max) {
        /* Find next line */
        const char *line_start = p;
        const char *line_end   = strchr(p, '\n');
        if (!line_end) line_end = p + strlen(p);

        /* Copy line */
        char line[1024];
        int  llen = (int)(line_end - line_start);
        if (llen >= (int)sizeof(line)) llen = sizeof(line)-1;
        memcpy(line, line_start, llen);
        /* Trim \r */
        while (llen > 0 && (line[llen-1] == '\r' || line[llen-1] == '\n')) llen--;
        line[llen] = '\0';
        p = *line_end ? line_end+1 : line_end;

        if (!*line) continue;

        if (!strncmp(line, "#EXTINF:", 8)) {
            memset(&cur, 0, sizeof(cur));
            strncpy(cur.playlist_id, pl_id, sizeof(cur.playlist_id)-1);
            has_meta = 1;

            /* Extract attributes: tvg-id="..." group-title="..." tvg-logo="..." ,Name */
            char *comma = strrchr(line, ',');
            if (comma) {
                strncpy(cur.name, comma+1, sizeof(cur.name)-1);
                /* Trim leading space */
                char *n = cur.name;
                while (*n == ' ') memmove(n, n+1, strlen(n));
            }

            /* Parse key="value" pairs */
            const char *attr = line + 8;
            while ((attr = strstr(attr, "="))) {
                /* Find key start */
                const char *key_end = attr;
                const char *key_start = key_end - 1;
                while (key_start > line && *key_start != ' ' &&
                       *key_start != '\t') key_start--;
                if (*key_start == ' ' || *key_start == '\t') key_start++;

                char key[64] = {0};
                int  klen = (int)(key_end - key_start);
                if (klen >= (int)sizeof(key)) klen = sizeof(key)-1;
                memcpy(key, key_start, klen);

                attr++; /* skip '=' */
                if (*attr == '"') {
                    attr++;
                    const char *val_end = strchr(attr, '"');
                    if (!val_end) break;
                    char val[256] = {0};
                    int vlen = (int)(val_end - attr);
                    if (vlen >= (int)sizeof(val)) vlen = sizeof(val)-1;
                    memcpy(val, attr, vlen);

                    if      (!strcasecmp(key,"tvg-id"))      strncpy(cur.id, val, sizeof(cur.id)-1);
                    else if (!strcasecmp(key,"group-title")) strncpy(cur.group, val, sizeof(cur.group)-1);
                    else if (!strcasecmp(key,"tvg-logo"))    strncpy(cur.logo,  val, sizeof(cur.logo)-1);

                    attr = val_end + 1;
                } else {
                    attr++;
                }
            }
        } else if (line[0] != '#' && has_meta) {
            strncpy(cur.url, line, sizeof(cur.url)-1);

            /* Generate stable id */
            char id_src[128];
            snprintf(id_src, sizeof(id_src), "%s_%s_%d", pl_id, cur.name, count);
            /* Simple hash to 8 hex chars */
            unsigned h = 5381;
            for (const char *s = id_src; *s; s++) h = ((h << 5) + h) ^ (unsigned char)*s;
            snprintf(cur.id, sizeof(cur.id), "%s_%08x", pl_id, h);

            out[count++] = cur;
            has_meta = 0;
        }
    }
    return count;
}

/* ── Disk persistence ─────────────────────────────────────────────────────── */

static void mkdirs(const char *path) {
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp)-1);
    char *p = tmp+1;
    while ((p = strchr(p,'/'))) { *p='\0'; mkdir(tmp,0755); *p='/'; p++; }
    mkdir(tmp, 0755);
}

static char *channel_cache_path(const char *pl_id) {
    static char buf[512];
    snprintf(buf, sizeof(buf), "%s/%s.json", IPTV_CACHE_DIR, pl_id);
    return buf;
}

static void save_channel_cache(const char *pl_id, IptvChannel *ch, int n) {
    mkdirs(IPTV_CACHE_DIR);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id",          ch[i].id);
        cJSON_AddStringToObject(o, "name",        ch[i].name);
        cJSON_AddStringToObject(o, "url",         ch[i].url);
        cJSON_AddStringToObject(o, "group",       ch[i].group);
        cJSON_AddStringToObject(o, "logo",        ch[i].logo);
        cJSON_AddStringToObject(o, "playlist_id", ch[i].playlist_id);
        cJSON_AddItemToArray(arr, o);
    }
    char *s = cJSON_Print(arr);
    cJSON_Delete(arr);

    char tmp[512]; snprintf(tmp, sizeof(tmp), "%s.tmp", channel_cache_path(pl_id));
    FILE *f = fopen(tmp, "w");
    if (f) { fputs(s, f); fclose(f); rename(tmp, channel_cache_path(pl_id)); }
    free(s);
}

static int load_channel_cache(const char *pl_id) {
    FILE *f = fopen(channel_cache_path(pl_id), "r");
    if (!f) return 0;
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char *buf = malloc(sz+1); fread(buf,1,sz,f); buf[sz]='\0'; fclose(f);

    cJSON *arr = cJSON_Parse(buf); free(buf);
    if (!arr) return 0;

    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n && g_ch_count < IPTV_MAX_CHANNELS; i++) {
        cJSON *o = cJSON_GetArrayItem(arr, i);
        IptvChannel *c = &g_channels[g_ch_count++];
        strncpy(c->id,          cJSON_GetString(o,"id",""),          sizeof(c->id)-1);
        strncpy(c->name,        cJSON_GetString(o,"name",""),        sizeof(c->name)-1);
        strncpy(c->url,         cJSON_GetString(o,"url",""),         sizeof(c->url)-1);
        strncpy(c->group,       cJSON_GetString(o,"group",""),       sizeof(c->group)-1);
        strncpy(c->logo,        cJSON_GetString(o,"logo",""),        sizeof(c->logo)-1);
        strncpy(c->playlist_id, cJSON_GetString(o,"playlist_id",""),sizeof(c->playlist_id)-1);
    }
    cJSON_Delete(arr);
    return n;
}

void iptv_save_playlists(void) {
    mkdirs("/var/lib/qaryxos");
    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_CreateArray();
    for (int i = 0; i < g_pl_count; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id",            g_playlists[i].id);
        cJSON_AddStringToObject(o, "name",          g_playlists[i].name);
        cJSON_AddStringToObject(o, "url",           g_playlists[i].url);
        cJSON_AddNumberToObject(o, "updated_at",    (double)g_playlists[i].updated_at);
        cJSON_AddNumberToObject(o, "channel_count", g_playlists[i].channel_count);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON_AddItemToObject(root, "playlists", arr);
    char *s = cJSON_Print(root); cJSON_Delete(root);

    char tmp[512]; snprintf(tmp, sizeof(tmp), "%s.tmp", IPTV_PLAYLISTS_FILE);
    FILE *f = fopen(tmp, "w");
    if (f) { fputs(s, f); fclose(f); rename(tmp, IPTV_PLAYLISTS_FILE); }
    free(s);
}

void iptv_load(void) {
    g_pl_count = 0;
    g_ch_count = 0;

    FILE *f = fopen(IPTV_PLAYLISTS_FILE, "r");
    if (!f) return;
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char *buf = malloc(sz+1); fread(buf,1,sz,f); buf[sz]='\0'; fclose(f);

    cJSON *root = cJSON_Parse(buf); free(buf);
    if (!root) return;

    cJSON *arr = cJSON_GetObjectItem(root, "playlists");
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n && g_pl_count < IPTV_MAX_PLAYLISTS; i++) {
        cJSON *o = cJSON_GetArrayItem(arr, i);
        IptvPlaylist *p = &g_playlists[g_pl_count++];
        strncpy(p->id,   cJSON_GetString(o,"id",""),   sizeof(p->id)-1);
        strncpy(p->name, cJSON_GetString(o,"name",""), sizeof(p->name)-1);
        strncpy(p->url,  cJSON_GetString(o,"url",""),  sizeof(p->url)-1);
        p->updated_at    = (time_t)cJSON_GetNumber(o,"updated_at",0);
        p->channel_count = (int)  cJSON_GetNumber(o,"channel_count",0);
        load_channel_cache(p->id);
    }
    cJSON_Delete(root);
}

int iptv_add_playlist(const char *url, const char *name) {
    if (g_pl_count >= IPTV_MAX_PLAYLISTS) return -1;

    IptvPlaylist *pl = &g_playlists[g_pl_count];
    /* Generate id from name + timestamp */
    unsigned h = 5381;
    for (const char *s = name; *s; s++) h = ((h<<5)+h)^(unsigned char)*s;
    snprintf(pl->id, sizeof(pl->id), "pl_%08x_%lx", h, (unsigned long)time(NULL));
    strncpy(pl->name, name, sizeof(pl->name)-1);
    strncpy(pl->url,  url,  sizeof(pl->url)-1);
    pl->updated_at    = 0;
    pl->channel_count = 0;
    g_pl_count++;

    int count = iptv_refresh_playlist(pl->id);
    iptv_save_playlists();
    return count;
}

int iptv_remove_playlist(const char *id) {
    for (int i = 0; i < g_pl_count; i++) {
        if (!strcmp(g_playlists[i].id, id)) {
            /* Remove channels */
            int w = 0;
            for (int j = 0; j < g_ch_count; j++)
                if (strcmp(g_channels[j].playlist_id, id))
                    g_channels[w++] = g_channels[j];
            g_ch_count = w;

            /* Remove playlist */
            g_playlists[i] = g_playlists[--g_pl_count];
            remove(channel_cache_path(id));
            iptv_save_playlists();
            return 1;
        }
    }
    return 0;
}

int iptv_refresh_playlist(const char *id) {
    IptvPlaylist *pl = NULL;
    for (int i = 0; i < g_pl_count; i++)
        if (!strcmp(g_playlists[i].id, id)) { pl = &g_playlists[i]; break; }
    if (!pl) return -1;

    char *content = http_dl_string(pl->url);
    if (!content) return -1;

    /* Allocate temp channel buffer */
    IptvChannel *tmp = malloc(sizeof(IptvChannel) * IPTV_MAX_CHANNELS);
    if (!tmp) { free(content); return -1; }

    int count = parse_m3u(content, pl->id, tmp, IPTV_MAX_CHANNELS);
    free(content);

    /* Remove old channels for this playlist */
    int w = 0;
    for (int i = 0; i < g_ch_count; i++)
        if (strcmp(g_channels[i].playlist_id, id))
            g_channels[w++] = g_channels[i];
    g_ch_count = w;

    /* Add new channels */
    int add = count;
    if (g_ch_count + add > IPTV_MAX_CHANNELS) add = IPTV_MAX_CHANNELS - g_ch_count;
    memcpy(g_channels + g_ch_count, tmp, add * sizeof(IptvChannel));
    g_ch_count += add;
    free(tmp);

    pl->channel_count = count;
    pl->updated_at    = time(NULL);
    save_channel_cache(id, g_channels + g_ch_count - add, add);
    iptv_save_playlists();
    return count;
}

IptvPlaylist *iptv_get_playlists(int *n) { *n = g_pl_count; return g_playlists; }

IptvChannel *iptv_get_channels(const char *pl_id, const char *group, int *n) {
    static IptvChannel result[IPTV_MAX_CHANNELS];
    int count = 0;
    for (int i = 0; i < g_ch_count && count < IPTV_MAX_CHANNELS; i++) {
        if (pl_id && strcmp(g_channels[i].playlist_id, pl_id)) continue;
        if (group  && strcmp(g_channels[i].group, group))       continue;
        result[count++] = g_channels[i];
    }
    *n = count;
    return result;
}

IptvChannel *iptv_get_channel(const char *id) {
    for (int i = 0; i < g_ch_count; i++)
        if (!strcmp(g_channels[i].id, id)) return &g_channels[i];
    return NULL;
}

const char **iptv_get_groups(int *n) {
    static const char *groups[1024];
    static char        group_bufs[1024][64];
    int count = 0;
    for (int i = 0; i < g_ch_count; i++) {
        if (!g_channels[i].group[0]) continue;
        int found = 0;
        for (int j = 0; j < count; j++)
            if (!strcmp(groups[j], g_channels[i].group)) { found=1; break; }
        if (!found && count < 1024) {
            strncpy(group_bufs[count], g_channels[i].group, 63);
            groups[count] = group_bufs[count];
            count++;
        }
    }
    *n = count;
    return groups;
}
