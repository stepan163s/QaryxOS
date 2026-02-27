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
    ffmpeg \
    libdrm2 \
    libgbm1

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

# --- Copy overlay files ---
info "Applying OS overlay..."
if [[ -d /opt/qaryxos-src/os/overlay ]]; then
    cp -r /opt/qaryxos-src/os/overlay/* /
fi

# --- Enable services ---
info "Enabling systemd services..."
systemctl daemon-reload
systemctl enable qaryxos-api.service
systemctl enable qaryxos-mediashell.service
systemctl enable qaryxos-mpv.service
systemctl enable qaryxos-update.timer

info "=== Installation complete ==="
info "Reboot to start QaryxOS"
info "API will be available at http://$(hostname -I | awk '{print $1}'):8080"
