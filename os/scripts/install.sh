#!/bin/bash
# QaryxOS Installation Script
# Run on fresh Armbian Bookworm Minimal on Radxa Zero 3W
# Usage: curl -sL https://raw.githubusercontent.com/stepan163s/QaryxOS/main/os/scripts/install.sh | bash

set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()    { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $*"; }
error()   { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

QARYXOS_VERSION="1.0.0"
INSTALL_DIR="/usr/local/bin"
CONFIG_DIR="/etc/qaryxos"
DATA_DIR="/var/lib/qaryxos"
SRC_DIR="/usr/local/lib/qaryxos"
REPO="stepan163s/QaryxOS"

[[ $EUID -ne 0 ]] && error "Run as root"

info "=== QaryxOS v${QARYXOS_VERSION} Installer ==="
info "Board: Radxa Zero 3W (RK3566)"

# --- System packages ---
info "Installing system packages..."
apt-get update -qq
apt-get install -y --no-install-recommends \
    mpv \
    python3 \
    python3-pip \
    python3-venv \
    python3-pygame \
    sqlite3 \
    curl \
    jq \
    git \
    ffmpeg \
    libdrm2 \
    libgbm1 \
    kbd \
    util-linux

# --- Python venv for API ---
info "Setting up Python environment..."
python3 -m venv /opt/qaryxos-venv
/opt/qaryxos-venv/bin/pip install --quiet \
    fastapi \
    uvicorn[standard] \
    aiofiles \
    httpx \
    python-multipart

# --- yt-dlp standalone binary ---
info "Installing yt-dlp..."
curl -sL "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_linux_aarch64" \
    -o /usr/local/bin/yt-dlp
chmod +x /usr/local/bin/yt-dlp

# --- Directories ---
info "Creating directories..."
mkdir -p "$CONFIG_DIR"
mkdir -p "$DATA_DIR/iptv_cache"
mkdir -p "$DATA_DIR/youtube_cache"
mkdir -p "$DATA_DIR/thumbnails"

# --- Default config ---
if [[ ! -f "$CONFIG_DIR/config.json" ]]; then
    cat > "$CONFIG_DIR/config.json" << 'EOF'
{
  "version": "1.0.0",
  "api_port": 8080,
  "api_host": "0.0.0.0",
  "mpv_socket": "/tmp/mpv.sock",
  "youtube_refresh_hours": 6,
  "iptv_refresh_hours": 24,
  "default_volume": 80,
  "ui_resolution": "auto"
}
EOF
fi

if [[ ! -f "$CONFIG_DIR/channels.json" ]]; then
    echo '{"channels": []}' > "$CONFIG_DIR/channels.json"
fi

if [[ ! -f "$CONFIG_DIR/playlists.json" ]]; then
    echo '{"playlists": []}' > "$CONFIG_DIR/playlists.json"
fi

# --- Versions file ---
cat > "$CONFIG_DIR/versions.json" << EOF
{
  "os": "${QARYXOS_VERSION}",
  "api": "${QARYXOS_VERSION}",
  "mediashell": "${QARYXOS_VERSION}",
  "yt-dlp": "$(yt-dlp --version 2>/dev/null || echo unknown)"
}
EOF

# --- Download source code ---
info "Downloading QaryxOS source..."
mkdir -p "$SRC_DIR"
if [[ -d "$SRC_DIR/.git" ]]; then
    git -C "$SRC_DIR" pull --quiet
else
    git clone --depth=1 "https://github.com/$REPO.git" "$SRC_DIR"
fi

# --- Install app components ---
info "Installing API daemon..."
mkdir -p /usr/local/lib/qaryxos-api
cp -r "$SRC_DIR/api/"* /usr/local/lib/qaryxos-api/
/opt/qaryxos-venv/bin/pip install --quiet -r /usr/local/lib/qaryxos-api/requirements.txt

info "Installing mediashell..."
mkdir -p /usr/local/lib/qaryxos-mediashell
cp -r "$SRC_DIR/mediashell/"* /usr/local/lib/qaryxos-mediashell/

info "Installing OTA updater..."
cp "$SRC_DIR/ota/updater.py" /usr/local/bin/qaryxos-ota
chmod +x /usr/local/bin/qaryxos-ota

# --- Apply OS overlay (systemd units, mpv config, etc.) ---
info "Applying OS overlay..."
cp -r "$SRC_DIR/os/overlay/"* /

# --- Enable services ---
info "Enabling systemd services..."
systemctl daemon-reload
systemctl enable qaryxos-mpv.service
systemctl enable qaryxos-api.service
systemctl enable qaryxos-mediashell.service
systemctl enable qaryxos-update.timer
systemctl enable qaryxos-youtube-refresh.timer

# --- Setup launcher (autologin + DRM console) ---
info "Setting up TV launcher mode..."
bash "$SRC_DIR/os/scripts/setup-launcher.sh"

DEVICE_IP=$(hostname -I | awk '{print $1}')
info ""
info "=== QaryxOS installation complete! ==="
info ""
info "  Device IP:  $DEVICE_IP"
info "  API URL:    http://$DEVICE_IP:8080/health"
info ""
info "Next: reboot the device"
info "      → QaryxOS UI will appear on HDMI automatically"
info ""
info "Install companion app on your Android phone:"
info "  https://github.com/$REPO/releases/latest"
info "  → Enter IP: $DEVICE_IP"
