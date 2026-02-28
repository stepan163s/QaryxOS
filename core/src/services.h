#pragma once

typedef struct {
    int xray_active;
    int xray_enabled;
    int tailscale_active;
    int tailscale_enabled;
} ServicesState;

/* Return cached state; force=1 re-queries systemd immediately */
const ServicesState *services_get(int force);

/* Enable or disable a service by name ("xray" or "tailscaled").
   enable=1 → systemctl enable --now
   enable=0 → systemctl disable --now
   Refreshes the cache after the change. */
void services_set(const char *name, int enable);
