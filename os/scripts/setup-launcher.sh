#!/bin/bash
# QaryxOS Launcher Setup
# Configures the device so mediashell starts automatically on HDMI
# and the system behaves as a dedicated TV appliance (no login prompt).
#
# Run once after install.sh:
#   bash /path/to/setup-launcher.sh

set -euo pipefail
info()  { echo -e "\e[32m[INFO]\e[0m  $*"; }
warn()  { echo -e "\e[33m[WARN]\e[0m  $*"; }

[[ $EUID -ne 0 ]] && echo "Run as root" && exit 1

info "=== QaryxOS Launcher Setup ==="

# ── 1. Autologin on tty1 ───────────────────────────────────────────────────
# mediashell needs to run as a session that owns the DRM device.
# The cleanest way on Armbian: autologin root on tty1 → start mediashell.
# (We don't use X11 or Wayland — direct DRM via SDL/pygame kmsdrm driver)

info "Configuring autologin on tty1..."
mkdir -p /etc/systemd/system/getty@tty1.service.d
cat > /etc/systemd/system/getty@tty1.service.d/autologin.conf << 'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --noclear %I $TERM
EOF

# ── 2. bash profile: start mediashell on tty1 ─────────────────────────────
# When root logs in on tty1, immediately start the mediashell service
# (or launch it directly via systemd if not already running).
info "Configuring root profile for tty1..."

PROFILE_SNIPPET='
# QaryxOS: start mediashell on tty1
if [[ "$(tty)" == "/dev/tty1" ]]; then
    # Disable cursor blinking on framebuffer
    echo -e "\e[?25l" > /dev/tty1
    setterm --cursor off --blank 0 --powerdown 0 > /dev/tty1 2>/dev/null || true
    # Start all QaryxOS services (idempotent)
    systemctl start qaryxos-mpv.service
    systemctl start qaryxos-api.service
    systemctl start qaryxos-mediashell.service
    # Keep tty1 alive (mediashell runs as its own process)
    while true; do sleep 3600; done
fi
'

if ! grep -q "QaryxOS: start mediashell" /root/.bashrc 2>/dev/null; then
    echo "$PROFILE_SNIPPET" >> /root/.bashrc
    info "  Added mediashell autostart to /root/.bashrc"
fi

# ── 3. Hide console cursor and messages on HDMI ───────────────────────────
info "Hiding kernel console on HDMI..."

# Suppress kernel messages on tty (they appear over our UI)
mkdir -p /etc/systemd/system.conf.d
cat > /etc/systemd/system.conf.d/99-qaryxos-console.conf << 'EOF'
[Manager]
ShowStatus=no
EOF

# Boot params: suppress console messages
ARMBIAN_ENV="/boot/armbianEnv.txt"
if [[ -f "$ARMBIAN_ENV" ]]; then
    # Add console blank + quiet if not already set
    if ! grep -q "consoleblank" "$ARMBIAN_ENV"; then
        echo "extraargs=quiet loglevel=0 consoleblank=0 logo.nologo vt.global_cursor_default=0" >> "$ARMBIAN_ENV"
        info "  Added quiet boot params to armbianEnv.txt"
    fi
fi

# Disable console blanking via udev rule
cat > /etc/udev/rules.d/99-qaryxos-console.rules << 'EOF'
# Disable DPMS / console blanking — QaryxOS manages display
SUBSYSTEM=="tty", KERNEL=="tty1", ACTION=="add", RUN+="/bin/sh -c 'echo -e \\\\033[9;0] > /dev/tty1'"
EOF

# ── 4. Kernel console → only on serial, not HDMI ─────────────────────────
# Keep kernel log on UART (for debug) but remove it from HDMI (tty1)
# This prevents kernel messages from appearing on TV screen
info "Redirecting kernel console to UART only..."
if [[ -f "$ARMBIAN_ENV" ]] && ! grep -q "console=ttyS2" "$ARMBIAN_ENV"; then
    # RK3566 UART: ttyS2 on Radxa Zero 3W
    # Keep UART, remove HDMI from kernel console list
    sed -i '/^console=/d' "$ARMBIAN_ENV" 2>/dev/null || true
    echo "console=ttyS2,1500000n8" >> "$ARMBIAN_ENV"
    info "  Kernel console → UART ttyS2 only"
fi

# ── 5. DRM/KMS permissions ────────────────────────────────────────────────
info "Setting DRM permissions for mediashell..."
# Ensure root (or video group) can access DRM devices
cat > /etc/udev/rules.d/99-qaryxos-drm.rules << 'EOF'
SUBSYSTEM=="drm", GROUP="video", MODE="0660"
SUBSYSTEM=="dri", GROUP="video", MODE="0660"
EOF

# ── 6. SDL/pygame KMS/DRM environment ─────────────────────────────────────
info "Setting SDL environment for KMS/DRM..."
cat > /etc/environment.d/99-qaryxos.conf << 'EOF'
SDL_VIDEODRIVER=kmsdrm
SDL_AUDIODRIVER=alsa
SDL_FBDEV=/dev/fb0
PYTHONUNBUFFERED=1
EOF

# ── 7. Disable login prompt on tty1-tty6 (keep only tty1 for autologin) ──
info "Disabling unused TTYs..."
for tty in tty2 tty3 tty4 tty5 tty6; do
    systemctl disable getty@$tty 2>/dev/null || true
    systemctl stop getty@$tty 2>/dev/null || true
done

# ── 8. Watchdog for mediashell (auto-restart if it crashes) ───────────────
# Already handled by Restart=always in mediashell.service

# ── 9. Reload and enable ─────────────────────────────────────────────────
info "Enabling QaryxOS services..."
systemctl daemon-reload
systemctl enable qaryxos-mpv.service
systemctl enable qaryxos-api.service
systemctl enable qaryxos-mediashell.service
systemctl enable qaryxos-update.timer
systemctl enable qaryxos-youtube-refresh.timer
systemctl enable getty@tty1.service

info ""
info "=== Setup complete! ==="
info ""
info "Boot sequence:"
info "  Power ON → Armbian kernel → autologin root → mediashell on HDMI"
info ""
info "After reboot you will see QaryxOS UI on your TV."
info "Control from your phone: install companion app, enter device IP."
info ""
info "To reboot now:  reboot"
info "To test now:    systemctl start qaryxos-mediashell"
