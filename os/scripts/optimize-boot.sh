#!/bin/bash
# QaryxOS Boot Optimization Script
# Target: < 12 seconds from power-on to TV UI
# Run once after initial setup

set -euo pipefail

info() { echo "[INFO] $*"; }

[[ $EUID -ne 0 ]] && echo "Run as root" && exit 1

info "=== QaryxOS Boot Optimizer ==="

# --- Disable unused services ---
info "Disabling unused systemd services..."

DISABLE_SERVICES=(
    apt-daily.service
    apt-daily-upgrade.service
    apt-daily.timer
    apt-daily-upgrade.timer
    man-db.timer
    man-db.service
    fstrim.timer
    motd-news.timer
    motd-news.service
    bluetooth.service
    # avahi-daemon.service  ← keep: needed for mDNS device discovery
    ModemManager.service
    wpa_supplicant.service     # if using NetworkManager instead
    networkd-dispatcher.service
    systemd-timesyncd.service  # optional, re-enable if NTP needed
    e2scrub_reap.service
    e2scrub_all.timer
    logrotate.timer
    sysstat.service
    sysstat-collect.timer
    sysstat-summary.timer
)

for svc in "${DISABLE_SERVICES[@]}"; do
    if systemctl is-enabled "$svc" &>/dev/null; then
        systemctl disable --now "$svc" 2>/dev/null || true
        info "  Disabled: $svc"
    fi
done

# --- Mask heavy units that auto-activate ---
info "Masking heavy units..."
MASK_UNITS=(
    apt-daily.service
    apt-daily-upgrade.service
    man-db.timer
    e2scrub_all.timer
)
for unit in "${MASK_UNITS[@]}"; do
    systemctl mask "$unit" 2>/dev/null || true
done

# --- zram: compressed swap (512 MB, zstd) ---
info "Setting up zram swap..."
if modprobe zram 2>/dev/null; then
    echo zstd > /sys/block/zram0/comp_algorithm 2>/dev/null || \
        echo lz4  > /sys/block/zram0/comp_algorithm 2>/dev/null || true
    echo 512M > /sys/block/zram0/disksize
    mkswap /dev/zram0 -L zram0 >/dev/null
    swapon -p 100 /dev/zram0
    # Persist via service
    systemctl enable qaryxos-zram.service 2>/dev/null || true
    info "  zram0: 512M zstd swap enabled"
else
    info "  zram module not available, skipping"
fi

# --- NetworkManager: reduce connection wait ---
info "Optimizing NetworkManager..."
mkdir -p /etc/NetworkManager/conf.d
cat > /etc/NetworkManager/conf.d/99-qaryxos.conf << 'EOF'
[main]
dhcp=internal

[connection]
connection.stable-id=${CONNECTION}/${BOOT}
EOF

# --- systemd timeout reductions ---
info "Tuning systemd timeouts..."
mkdir -p /etc/systemd/system.conf.d
cat > /etc/systemd/system.conf.d/99-qaryxos.conf << 'EOF'
[Manager]
DefaultTimeoutStartSec=10s
DefaultTimeoutStopSec=5s
DefaultDeviceTimeoutSec=8s
EOF

# --- Journal: RAM only, small size ---
info "Configuring journald..."
mkdir -p /etc/systemd/journald.conf.d
cat > /etc/systemd/journald.conf.d/99-qaryxos.conf << 'EOF'
[Journal]
Storage=volatile
RuntimeMaxUse=32M
MaxRetentionSec=1day
EOF

# --- Remove fsck on boot (SD card, read-only root later) ---
info "Disabling fsck on boot..."
# Add 'fastboot' to kernel cmdline or set fsck pass=0 in fstab
sed -i 's/ errors=remount-ro/ errors=remount-ro,nodiscard/' /etc/fstab 2>/dev/null || true

# Set fsck passno to 0 for all partitions (SD card doesn't benefit)
awk 'NR==1 || $6 != 0 { $6 = 0 } 1' /etc/fstab > /tmp/fstab.new && mv /tmp/fstab.new /etc/fstab

# --- Kernel boot parameters (edit in /boot/armbianEnv.txt or /boot/cmdline.txt) ---
info "Applying kernel cmdline tweaks..."
BOOT_FILE=""
[[ -f /boot/armbianEnv.txt ]] && BOOT_FILE="/boot/armbianEnv.txt"
[[ -f /boot/cmdline.txt ]] && BOOT_FILE="/boot/cmdline.txt"

if [[ -n "$BOOT_FILE" ]]; then
    # Add quiet, remove verbose boot messages
    if ! grep -q "quiet" "$BOOT_FILE"; then
        if [[ "$BOOT_FILE" == *"armbianEnv.txt" ]]; then
            echo 'extraargs=quiet loglevel=3 rd.systemd.show_status=false rd.udev.log_level=3' >> "$BOOT_FILE"
        else
            sed -i 's/$/ quiet loglevel=3/' "$BOOT_FILE"
        fi
        info "  Added quiet boot to $BOOT_FILE"
    fi
fi

# --- Preload mpv socket service at boot ---
info "Configuring mpv early start..."
# mpv service uses socket activation - starts before UI

# --- Reload systemd ---
systemctl daemon-reload

info "=== Boot optimization complete ==="
info "Run 'systemd-analyze blame' after reboot to verify"
info ""
info "Expected boot breakdown:"
info "  Kernel + initrd:      ~3s"
info "  systemd init:         ~2s"
info "  Network (background): ~3s"
info "  API + mpv start:      ~2s"
info "  mediashell start:     ~1s"
info "  ─────────────────────────"
info "  Total target:         < 12s"
