# Установка и развёртывание QaryxOS

> **Архитектура (C-стек):**
> Один бинарник `/usr/bin/qaryx` — DRM/KMS + OpenGL ES + libmpv + libinput + WebSocket.
> Единственный внешний процесс — `yt-dlp` (subprocess для резолва YouTube URL).

---

## Что понадобится

| | |
|---|---|
| **Устройство** | Radxa Zero 3W (RK3566, 2GB RAM) |
| **SD карта** | 16 GB+, Class 10 / A1 |
| **Питание** | USB-C, 5V/3A |
| **Дисплей** | Телевизор с HDMI |
| **Сеть** | Wi-Fi или USB-C Ethernet адаптер |
| **Android** | Телефон Android 8.0+ для компаньон-приложения |
| **ПК** | Для прошивки SD и SSH (macOS / Linux / Windows) |

---

## Шаг 1 — Прошивка Armbian

### 1.1 Скачать образ

```
https://www.armbian.com/radxa-zero3/
→ Armbian 24.x Bookworm → Minimal (CLI)
Файл: Armbian_24.x_Radxa-zero3_bookworm_current_6.6.x_minimal.img.xz
```

### 1.2 Записать на SD карту

**macOS / Linux:**
```bash
xz -d Armbian_24.x_*.img.xz
sudo dd if=Armbian_24.x_*.img of=/dev/sdX bs=4M status=progress conv=fsync
# /dev/sdX — ваша SD карта (проверьте lsblk / diskutil list)
```

**Windows:** использовать [Balena Etcher](https://etcher.balena.io) — выбрать .img.xz → SD карта → Flash.

---

## Шаг 2 — Первый запуск Armbian

1. Вставить SD карту в Radxa Zero 3W
2. Подключить HDMI к телевизору
3. Подать питание (USB-C 5V/3A)
4. При первом старте Armbian попросит задать пароль root и настроить сеть

**Подключиться к Wi-Fi:**
```bash
nmtui
# → Activate a connection → выбрать сеть → ввести пароль
```

**Узнать IP устройства:**
```bash
hostname -I
# Пример: 192.168.1.42
```

**Подключиться по SSH с компьютера:**
```bash
ssh root@192.168.1.42
```

---

## Шаг 3 — Установка зависимостей

```bash
apt-get update && apt-get upgrade -y

# Зависимости для сборки qaryx (C binary)
apt-get install -y \
    git cmake build-essential pkg-config \
    libdrm-dev libgbm-dev libegl-dev libgles2-mesa-dev \
    libmpv-dev libinput-dev libudev-dev \
    libcurl4-openssl-dev \
    fonts-dejavu-core \
    avahi-daemon libnss-mdns \
    python3

# yt-dlp — единственный Python-компонент (запускается как subprocess)
curl -L "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_linux_aarch64" \
    -o /usr/local/bin/yt-dlp
chmod +x /usr/local/bin/yt-dlp

# Проверка
yt-dlp --version
```

> **Примечание:** `python3` нужен только для `yt-dlp`. Никакого FastAPI, uvicorn, pygame — не устанавливать.

---

## Шаг 4 — Получение исходного кода

```bash
git clone --depth=1 https://github.com/stepan163s/QaryxOS.git /opt/qaryxos-src
cd /opt/qaryxos-src
```

---

## Шаг 5 — Сборка бинарника `qaryx`

### 5.1 Нативная сборка (прямо на плате, ~4–8 минут)

```bash
cd /opt/qaryxos-src
cmake -B core/build -S core \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="-march=armv8-a+crc"
cmake --build core/build -j4

# Установить бинарник
install -m 755 core/build/qaryx /usr/bin/qaryx
```

### 5.2 Кросс-компиляция (опционально, с x86-64 машины)

```bash
# На машине разработки (Ubuntu/Debian):
sudo apt-get install gcc-aarch64-linux-gnu \
    libdrm-dev:arm64 libgbm-dev:arm64 libegl-dev:arm64 \
    libgles2-mesa-dev:arm64 libmpv-dev:arm64 \
    libinput-dev:arm64 libudev-dev:arm64 libcurl4-openssl-dev:arm64

cmake -B core/build-cross -S core \
    -DCMAKE_TOOLCHAIN_FILE=core/cmake/aarch64-linux-gnu.cmake \
    -DCMAKE_BUILD_TYPE=Release
cmake --build core/build-cross -j$(nproc)

# Скопировать на плату
scp core/build-cross/qaryx root@<IP_ПЛАТЫ>:/usr/bin/qaryx
```

---

## Шаг 6 — Конфигурация

```bash
# Создать директории
mkdir -p /etc/qaryxos
mkdir -p /var/lib/qaryxos/{iptv,history}

# Создать config.json
cat > /etc/qaryxos/config.json << 'EOF'
{
  "ws_port": 8080,
  "font_path": "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
  "data_dir": "/var/lib/qaryxos",
  "volume": 80,
  "screen_w": 1920,
  "screen_h": 1080
}
EOF

# Установить mpv.conf (для libmpv — hwdec rkmpp, ALSA audio)
mkdir -p /etc/mpv
cp /opt/qaryxos-src/os/overlay/etc/mpv/mpv.conf /etc/mpv/
```

---

## Шаг 7 — Установка системных файлов

```bash
cd /opt/qaryxos-src

# systemd сервисы
cp os/overlay/etc/systemd/system/qaryxos.service          /etc/systemd/system/
cp os/overlay/etc/systemd/system/qaryxos-cpu-governor.service /etc/systemd/system/
cp os/overlay/etc/systemd/system/qaryxos-zram.service     /etc/systemd/system/

# mDNS (автообнаружение устройства в Android-приложении)
mkdir -p /etc/avahi/services
cp os/overlay/etc/avahi/services/qaryxos.service /etc/avahi/services/

# Применить и включить
systemctl daemon-reload
systemctl enable qaryxos.service
systemctl enable qaryxos-cpu-governor.service
systemctl enable qaryxos-zram.service
systemctl enable avahi-daemon
```

---

## Шаг 8 — Настройка DRM-консоли

Бинарник `qaryx` захватывает DRM напрямую, без X11/Wayland. Нужно:

```bash
# 1. Убрать курсор и kernel messages с HDMI-дисплея
cat >> /boot/armbianEnv.txt << 'EOF'
extraargs=quiet loglevel=0 consoleblank=0 logo.nologo vt.global_cursor_default=0
console=ttyS2,1500000n8
EOF
# (ttyS2 = UART Radxa Zero 3W — kernel log уходит туда, HDMI чистый)

# 2. Права на DRM/input устройства
cat > /etc/udev/rules.d/99-qaryx.rules << 'EOF'
SUBSYSTEM=="drm", GROUP="video", MODE="0660"
SUBSYSTEM=="dri", GROUP="video", MODE="0660"
KERNEL=="event*", SUBSYSTEM=="input", GROUP="input", MODE="0660"
EOF

# 3. Скрыть login prompt на tty1
mkdir -p /etc/systemd/system/getty@tty1.service.d
cat > /etc/systemd/system/getty@tty1.service.d/override.conf << 'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --noclear %I $TERM
EOF

# qaryx запускается через systemd (не из .bashrc) — autologin не обязателен,
# но убирает prompt, если кто-то подключится по HDMI + клавиатуре.

# 4. Применить udev
udevadm control --reload-rules
systemctl daemon-reload
```

---

## Шаг 9 — Перезагрузка и первый старт

```bash
reboot
```

**Boot timeline:**
```
Питание →
  U-Boot         (~2s)
  Kernel         (~2s)
  systemd:
    qaryxos-zram          (compressed swap, ~0.3s)
    qaryxos-cpu-governor  (performance governor, ~0.1s)
    qaryxos               (qaryx binary → DRM init → OpenGL → UI)
  ──────────────────────────
  HDMI загорается UI      (~5–6s после подачи питания)
```

**Проверить статус:**
```bash
systemctl status qaryxos
journalctl -u qaryxos -f
```

---

## Шаг 10 — Android-приложение

### 10.1 Сборка APK из исходников

На машине разработчика (требуется Android Studio / JDK 17):

```bash
cd /opt/qaryxos-src/android
./gradlew assembleRelease
# APK: android/app/build/outputs/apk/release/app-release.apk
```

### 10.2 Установить APK на телефон

```bash
adb install android/app/build/outputs/apk/release/app-release.apk
# или скопировать APK на телефон и открыть файловым менеджером
```

### 10.3 Подключение

1. Открыть **QaryxOS Remote** на телефоне
2. Автопоиск нажать **"Search on network"** (mDNS) — устройство появится в списке
3. Или ввести IP вручную (из `hostname -I` на плате)
4. Нажать **Connect** → зелёная точка = соединение установлено

---

## Шаг 11 — Добавление контента

### YouTube

В приложении → **Channels → YouTube** → вставить URL видео → **Play on TV**.
Устройство само резолвит URL через yt-dlp и передаёт в libmpv.

### IPTV (M3U плейлист)

В приложении → **Channels → IPTV → (+)** → ввести название и M3U URL → **Add Playlist**.
Плейлист скачивается и парсится на устройстве (~10–30 сек для больших списков).

### Прямое воспроизведение (WebSocket)

```bash
# Подключиться к WebSocket и отправить команду (wscat / websocat)
websocat ws://192.168.1.42:8080/
{"cmd":"play","url":"https://youtu.be/dQw4w9WgXcQ","type":"youtube"}
```

---

## Шаг 12 — OTA обновление прошивки

OTA — отдельный CLI-инструмент (не фоновый сервис):

```bash
ssh root@192.168.1.42

# Проверить наличие обновления
python3 /opt/qaryxos-src/ota/updater.py check

# Обновить
python3 /opt/qaryxos-src/ota/updater.py update
```

Или обновить вручную:

```bash
cd /opt/qaryxos-src
git pull
cmake --build core/build -j4
install -m 755 core/build/qaryx /usr/bin/qaryx
systemctl restart qaryxos
```

---

## Команды управления сервисом

```bash
systemctl start   qaryxos    # запустить
systemctl stop    qaryxos    # остановить
systemctl restart qaryxos    # перезапустить
systemctl status  qaryxos    # статус

journalctl -u qaryxos -f     # live-логи
journalctl -u qaryxos -n 100 # последние 100 строк
```

---

## Troubleshooting

### Нет изображения на HDMI

```bash
# Проверить что DRM устройство есть
ls /dev/dri/
# card0 и renderD128 должны присутствовать

# Убедиться что HDMI подключён ДО запуска qaryx
# (DRM connector обнаруживается при init)

# Проверить лог
journalctl -u qaryxos -n 50
# Ищем: "drm: opened /dev/dri/card0" и "drm: found connector HDMI-A-1"
```

### WebSocket не подключается (Android)

```bash
# Проверить что порт открыт
ss -tlnp | grep 8080

# Проверить firewall (Armbian обычно без него, но на всякий случай)
iptables -L INPUT -n | grep 8080
```

### yt-dlp не работает

```bash
# Обновить yt-dlp (нужен свежий — YouTube часто меняет API)
yt-dlp -U
# или принудительно
curl -L "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_linux_aarch64" \
    -o /usr/local/bin/yt-dlp
chmod +x /usr/local/bin/yt-dlp
yt-dlp --version
```

### Нет звука

```bash
# Список ALSA устройств
aplay -l

# Проверить что audio идёт через HDMI
# В /etc/mpv/mpv.conf:
#   ao=alsa
#   audio-device=auto   ← или явно: alsa/plughw:0,3 для HDMI

# Тест
mpv --ao=alsa --audio-device=auto /dev/urandom --length=2
```

### Мало памяти (OOM)

```bash
# Проверить использование памяти
free -h
# qaryx idle должен быть < 50MB RSS
# libmpv при воспроизведении: +30–80MB

# zram должен быть активен
swapon -s
# /dev/zram0 ... 512M
```

### Пересборка после изменений кода

```bash
cd /opt/qaryxos-src
git pull
cmake --build core/build -j4
systemctl restart qaryxos
```

---

## Файловая структура после установки

```
/usr/bin/qaryx                      ← основной бинарник
/usr/local/bin/yt-dlp               ← YouTube resolver

/etc/qaryxos/config.json            ← конфиг
/etc/mpv/mpv.conf                   ← mpv настройки (hwdec, audio)
/etc/avahi/services/qaryxos.service ← mDNS для Android autodiscovery

/etc/systemd/system/
  qaryxos.service                   ← главный сервис
  qaryxos-cpu-governor.service      ← performance governor
  qaryxos-zram.service              ← 512M compressed swap

/var/lib/qaryxos/
  iptv/                             ← IPTV плейлисты + каналы (JSON)
  history/                          ← история воспроизведения (JSON)

/opt/qaryxos-src/                   ← исходный код (для обновлений)
  core/build/                       ← build artifacts
  ota/updater.py                    ← CLI OTA updater
```
