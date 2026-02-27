# QaryxOS Architecture

## Hardware
- **Board**: Radxa Zero 3W (RK3566, 2GB RAM)
- **Output**: HDMI
- **Network**: Wi-Fi / Ethernet (USB adapter)

## OS Decision: Armbian Bookworm Minimal

**Chosen**: Armbian 24.x Debian Bookworm CLI (minimal)
**Image**: `Armbian_24.x_Radxa-zero3_bookworm_current_6.6.x_minimal.img`

### Why Armbian over alternatives
| | Armbian | Radxa Official | Buildroot |
|---|---|---|---|
| Kernel patches for RK3566 VPU | ✅ | ✅ | manual |
| rkmpp (hardware decode) | ✅ | ✅ | manual |
| APT ecosystem | ✅ | ✅ | ❌ |
| Minimal footprint | ✅ | ❌ (bloat) | ✅ |
| Community/docs | ✅ | medium | low |
| Boot time tunable | ✅ | hard | ✅ |

### Key kernel requirements
- `CONFIG_VIDEO_HANTRO` — Hantro VPU (H.264/H.265 hwdec)
- `CONFIG_DRM_ROCKCHIP` — DRM/KMS display
- `CONFIG_DRM_PANFROST` — Mali GPU (optional, not needed for mpv drm)

## Component Map

```
Radxa Zero 3W (Armbian Bookworm)
├── systemd
│   ├── qaryxos-api.service       ← REST API on :8080
│   ├── qaryxos-mediashell.service ← TV UI on HDMI via DRM/fbdev
│   ├── qaryxos-mpv.service       ← mpv IPC server
│   └── qaryxos-update.timer      ← OTA check timer
│
├── /usr/local/bin/
│   ├── qaryxos-api               ← Python FastAPI daemon
│   ├── qaryxos-mediashell        ← Python pygame TV UI
│   ├── qaryxos-ota               ← OTA updater script
│   └── yt-dlp                    ← YouTube resolver (standalone binary)
│
├── /etc/qaryxos/
│   ├── config.json               ← main config
│   ├── channels.json             ← YouTube channels
│   ├── playlists.json            ← IPTV playlists
│   └── history.json              ← playback history (v1.5)
│
├── /var/lib/qaryxos/
│   ├── iptv_cache/               ← M3U cache
│   └── youtube_cache/            ← video metadata cache
│
└── /tmp/mpv.sock                 ← mpv IPC socket (runtime)
```

## Network API

Base URL: `http://<device-ip>:8080`

### Playback
```
POST /play          {url, type?}   → start playback
POST /pause                         → toggle pause
POST /stop                          → stop
POST /seek          {seconds}       → seek relative
POST /volume        {level: 0-100}  → set volume
GET  /status                        → mpv state
```

### Navigation (TV UI)
```
POST /ui/key        {key: up|down|left|right|ok|back|home}
GET  /ui/state                      → current screen
```

### YouTube
```
GET  /youtube/feed                  → cached video list
GET  /youtube/channels              → channel list
POST /youtube/channels  {url}       → add channel
DELETE /youtube/channels/{id}       → remove
POST /youtube/refresh               → force refresh
```

### IPTV
```
GET  /iptv/channels                 → channel list
GET  /iptv/channels?group=X         → filtered
POST /iptv/playlists   {url, name}  → add playlist
DELETE /iptv/playlists/{id}         → remove
POST /iptv/play/{channel_id}        → play channel
POST /iptv/refresh                  → refresh M3U
```

### OTA
```
GET  /ota/check                     → {current, latest, available}
POST /ota/update                    → start update
GET  /ota/status                    → update progress
```

### System
```
GET  /health                        → {status: ok, version}
POST /system/reboot                 → reboot device
```

## mpv Configuration

```
hwdec=rkmpp          # Rockchip MPP hardware decode
hwdec-codecs=all
vo=drm               # Direct Rendering Manager output (no X11)
drm-connector=HDMI-A-1
cache=yes
cache-secs=30
demuxer-max-bytes=50MiB
input-ipc-server=/tmp/mpv.sock
```

## Versioning

Each component has independent version in `/etc/qaryxos/versions.json`:
```json
{
  "os": "1.0.0",
  "api": "1.0.0",
  "mediashell": "1.0.0",
  "yt-dlp": "2024.x.x"
}
```

OTA updates components independently via GitHub Releases.
