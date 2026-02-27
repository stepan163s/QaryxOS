# QaryxOS

Minimal Local Stream Box — автономный HDMI-медиаклиент на базе Radxa Zero 3W.

```
Телефон (Android)  ──REST──▶  Radxa Zero 3W  ──HDMI──▶  TV
   Companion App                  QaryxOS
```

## Компоненты

| Компонент | Описание |
|---|---|
| `os/` | Armbian overlay, systemd units, скрипты установки |
| `api/` | REST API daemon (Python FastAPI, порт 8080) |
| `mediashell/` | TV UI на pygame/DRM (home, YouTube, IPTV) |
| `ota/` | OTA updater CLI |
| `android/` | Kotlin Compose companion app |

## Быстрый старт

```bash
# На Radxa Zero 3W (Armbian Bookworm Minimal):
curl -sL https://raw.githubusercontent.com/stepan163s/QaryxOS/main/os/scripts/install.sh | bash
reboot
```

API будет доступен по `http://<device-ip>:8080`.

## API

```
POST /play          {url}         → воспроизвести URL
POST /pause                       → play/pause
POST /seek          {seconds}     → перемотка
POST /volume        {level}       → громкость 0-100
GET  /status                      → состояние mpv
POST /ui/key        {key}         → d-pad навигация
GET  /youtube/feed                → лента YouTube
POST /iptv/play/{id}              → включить IPTV канал
GET  /ota/check                   → проверить обновления
```

## Roadmap

- **v1.0** — API, mpv, Android remote, YouTube, IPTV
- **v1.5** — История просмотра, resume, веб-интерфейс
- **v2.0** — A/B OTA, EPG, HTTPS API

[→ GitHub Issues / Project Board](https://github.com/stepan163s/QaryxOS/projects/1)