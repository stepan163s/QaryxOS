#include "services.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static ServicesState g_state;
static time_t        g_last_refresh = 0;

static int svc_active(const char *name) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "systemctl is-active --quiet %s 2>/dev/null", name);
    return system(cmd) == 0;
}

static int svc_enabled(const char *name) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "systemctl is-enabled --quiet %s 2>/dev/null", name);
    return system(cmd) == 0;
}

const ServicesState *services_get(int force) {
    time_t now = time(NULL);
    if (force || now - g_last_refresh > 5) {
        g_state.xray_active       = svc_active("xray");
        g_state.xray_enabled      = svc_enabled("xray");
        g_state.tailscale_active  = svc_active("tailscaled");
        g_state.tailscale_enabled = svc_enabled("tailscaled");
        g_last_refresh = now;
    }
    return &g_state;
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
    system(cmd);
    services_get(1);
}
