#include "services.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

static ServicesState   g_state;
static time_t          g_last_refresh = 0;
static pthread_mutex_t g_mu   = PTHREAD_MUTEX_INITIALIZER;
static int             g_busy = 0; /* 1 = background refresh in flight */

/* Single systemctl query — does not block the calling thread for long. */
static int svc_query(const char *subcmd, const char *name) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "systemctl %s --quiet %s 2>/dev/null; echo $?", subcmd, name);
    FILE *f = popen(cmd, "r");
    if (!f) return 0;
    int rc = 1;
    fscanf(f, "%d", &rc);
    pclose(f);
    return rc == 0;
}

static void *refresh_thread(void *arg) {
    (void)arg;
    ServicesState s;
    s.xray_active       = svc_query("is-active",  "xray");
    s.xray_enabled      = svc_query("is-enabled", "xray");
    s.tailscale_active  = svc_query("is-active",  "tailscaled");
    s.tailscale_enabled = svc_query("is-enabled", "tailscaled");

    pthread_mutex_lock(&g_mu);
    g_state        = s;
    g_last_refresh = time(NULL);
    g_busy         = 0;
    pthread_mutex_unlock(&g_mu);
    return NULL;
}

/* Must be called with g_mu held. */
static void spawn_refresh(void) {
    if (g_busy) return;
    g_busy = 1;
    pthread_t t;
    if (pthread_create(&t, NULL, refresh_thread, NULL) == 0)
        pthread_detach(t);
    else
        g_busy = 0;
}

const ServicesState *services_get(int force) {
    pthread_mutex_lock(&g_mu);
    time_t now = time(NULL);
    if (force || now - g_last_refresh > 5)
        spawn_refresh();   /* non-blocking — returns immediately */
    const ServicesState *p = &g_state;
    pthread_mutex_unlock(&g_mu);
    return p;
}

void services_set(const char *name, int enable) {
    if (strcmp(name, "xray") != 0 && strcmp(name, "tailscaled") != 0)
        return;
    char cmd[128];
    if (enable)
        snprintf(cmd, sizeof(cmd),
                 "systemctl enable --now %s 2>/dev/null", name);
    else
        snprintf(cmd, sizeof(cmd),
                 "systemctl disable --now %s 2>/dev/null", name);
    system(cmd);   /* intentional: user toggled a service, short wait OK */

    pthread_mutex_lock(&g_mu);
    g_last_refresh = 0;   /* invalidate cache so next get() re-queries */
    spawn_refresh();
    pthread_mutex_unlock(&g_mu);
}
