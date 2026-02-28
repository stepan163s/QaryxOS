#!/bin/bash
# QaryxOS Installation Script (C-stack)
# Run on fresh Armbian Bookworm Minimal on Radxa Zero 3W
# Usage: curl -sL https://raw.githubusercontent.com/stepan163s/QaryxOS/main/os/scripts/install.sh | bash

set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

QARYXOS_VERSION="2.0.0"
REPO="stepan163s/QaryxOS"
SRC_DIR="/opt/qaryxos-src"
CONFIG_DIR="/etc/qaryxos"
DATA_DIR="/var/lib/qaryxos"

[[ $EUID -ne 0 ]] && error "Run as root (sudo bash install.sh)"

info "=== QaryxOS v${QARYXOS_VERSION} Installer (C-stack) ==="
info "Board: Radxa Zero 3W (RK3566)"
echo

# ── 1. System packages ────────────────────────────────────────────────────────

info "Installing system packages..."
apt-get update -qq
apt-get install -y --no-install-recommends \
    git cmake build-essential pkg-config \
    libdrm-dev libgbm-dev libegl-dev libgles2-mesa-dev \
    libmpv-dev libinput-dev libudev-dev \
    libcurl4-openssl-dev \
    fonts-dejavu-core \
    avahi-daemon libnss-mdns \
    python3 \
    curl ca-certificates

# ── 2. yt-dlp ─────────────────────────────────────────────────────────────────

info "Installing yt-dlp (aarch64 binary)..."
curl -sL "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_linux_aarch64" \
    -o /usr/local/bin/yt-dlp
chmod +x /usr/local/bin/yt-dlp
info "  yt-dlp $(yt-dlp --version)"

# ── 3. Clone / update source ──────────────────────────────────────────────────

info "Getting QaryxOS source code..."
if [[ -d "$SRC_DIR/.git" ]]; then
    git -C "$SRC_DIR" pull --quiet
    info "  Updated existing clone"
else
    git clone --depth=1 "https://github.com/$REPO.git" "$SRC_DIR"
    info "  Cloned to $SRC_DIR"
fi

# ── 4. Build qaryx binary ─────────────────────────────────────────────────────

info "Building qaryx binary (this takes a few minutes)..."
cmake -B "$SRC_DIR/core/build" -S "$SRC_DIR/core" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="-march=armv8-a+crc" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    2>&1 | tail -5
cmake --build "$SRC_DIR/core/build" -j4 2>&1 | tail -5
install -m 755 "$SRC_DIR/core/build/qaryx" /usr/bin/qaryx
info "  /usr/bin/qaryx installed"

# ── 5. Directories + config ───────────────────────────────────────────────────

info "Creating data directories..."
mkdir -p "$CONFIG_DIR"
mkdir -p "$DATA_DIR/iptv"
mkdir -p "$DATA_DIR/history"

if [[ ! -f "$CONFIG_DIR/config.json" ]]; then
    cat > "$CONFIG_DIR/config.json" << 'EOF'
{
  "ws_port": 8080,
  "font_path": "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
  "data_dir": "/var/lib/qaryxos",
  "volume": 80,
  "screen_w": 1920,
  "screen_h": 1080
}
EOF
    info "  Config written to $CONFIG_DIR/config.json"
else
    info "  Config already exists — skipping"
fi

# ── 6. mpv.conf ───────────────────────────────────────────────────────────────

info "Installing mpv config (hwdec rkmpp)..."
mkdir -p /etc/mpv
cp "$SRC_DIR/os/overlay/etc/mpv/mpv.conf" /etc/mpv/

# ── 7. systemd units ──────────────────────────────────────────────────────────

info "Installing systemd units..."
cp "$SRC_DIR/os/overlay/etc/systemd/system/qaryxos.service"             /etc/systemd/system/
cp "$SRC_DIR/os/overlay/etc/systemd/system/qaryxos-cpu-governor.service" /etc/systemd/system/
cp "$SRC_DIR/os/overlay/etc/systemd/system/qaryxos-zram.service"         /etc/systemd/system/

# ── 8. mDNS (Android auto-discovery) ─────────────────────────────────────────

info "Setting up mDNS (Avahi)..."
mkdir -p /etc/avahi/services
cp "$SRC_DIR/os/overlay/etc/avahi/services/qaryxos.service" /etc/avahi/services/

# ── 9. DRM / console setup ────────────────────────────────────────────────────

info "Configuring DRM console (quiet boot, HDMI clean)..."

# udev rules for DRM and input access
cat > /etc/udev/rules.d/99-qaryx.rules << 'EOF'
SUBSYSTEM=="drm", GROUP="video", MODE="0660"
SUBSYSTEM=="dri", GROUP="video", MODE="0660"
KERNEL=="event*", SUBSYSTEM=="input", GROUP="input", MODE="0660"
EOF
udevadm control --reload-rules

# Quiet boot params (suppress kernel messages on HDMI)
ARMBIAN_ENV="/boot/armbianEnv.txt"
if [[ -f "$ARMBIAN_ENV" ]]; then
    if ! grep -q "vt.global_cursor_default=0" "$ARMBIAN_ENV"; then
        echo "extraargs=quiet loglevel=0 consoleblank=0 logo.nologo vt.global_cursor_default=0" >> "$ARMBIAN_ENV"
        info "  Added quiet boot params"
    fi
    if ! grep -q "console=ttyS2" "$ARMBIAN_ENV"; then
        sed -i '/^console=/d' "$ARMBIAN_ENV" 2>/dev/null || true
        echo "console=ttyS2,1500000n8" >> "$ARMBIAN_ENV"
        info "  Kernel console → UART ttyS2 (HDMI stays clean)"
    fi
fi

# Hide login prompt on tty1 (autologin root)
mkdir -p /etc/systemd/system/getty@tty1.service.d
cat > /etc/systemd/system/getty@tty1.service.d/override.conf << 'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --noclear %I $TERM
EOF

# Suppress systemd status messages
mkdir -p /etc/systemd/system.conf.d
cat > /etc/systemd/system.conf.d/99-qaryx-console.conf << 'EOF'
[Manager]
ShowStatus=no
EOF

# ── 10. Install OTA tool ──────────────────────────────────────────────────────

info "Installing OTA updater CLI..."
install -m 755 "$SRC_DIR/ota/updater.py" /usr/local/bin/qaryxos-ota

# ── 11. Enable services ───────────────────────────────────────────────────────

info "Enabling services..."
systemctl daemon-reload
systemctl enable qaryxos-zram.service
systemctl enable qaryxos-cpu-governor.service
systemctl enable qaryxos.service
systemctl enable avahi-daemon

# ── Done ──────────────────────────────────────────────────────────────────────

DEVICE_IP=$(hostname -I | awk '{print $1}')

echo
info "╔══════════════════════════════════════════════════╗"
info "║        QaryxOS v${QARYXOS_VERSION} installed!              ║"
info "╠══════════════════════════════════════════════════╣"
info "║  Device IP:  ${DEVICE_IP}"
info "║  Binary:     /usr/bin/qaryx"
info "║  Config:     $CONFIG_DIR/config.json"
info "╠══════════════════════════════════════════════════╣"
info "║  Next step:  reboot                              ║"
info "║  After reboot → HDMI shows QaryxOS UI (~6s)     ║"
info "╠══════════════════════════════════════════════════╣"
info "║  Android app:                                    ║"
info "║  github.com/$REPO/releases/latest  ║"
info "║  → Enter IP: ${DEVICE_IP}                    ║"
info "╚══════════════════════════════════════════════════╝"
echo
warn "Run 'reboot' to start QaryxOS"
