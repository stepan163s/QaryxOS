# Установка QaryxOS на Radxa Zero 3W

## Что нужно

- Radxa Zero 3W (2GB)
- SD карта 16GB+ (Class 10 / A1)
- HDMI кабель + телевизор
- Доступ к Wi-Fi или Ethernet (USB-C адаптер)

---

## Шаг 1 — Скачать и прошить Armbian

**Скачать образ:**
```
https://www.armbian.com/radxa-zero3/
→ Armbian 24.x Bookworm → Minimal (CLI)
Файл: Armbian_24.x_Radxa-zero3_bookworm_current_6.6.x_minimal.img.xz
```

**Прошить на SD карту:**

На macOS/Linux:
```bash
# Разархивировать и прошить (замени /dev/sdX на твою карту)
xz -d Armbian_24.x_*.img.xz
sudo dd if=Armbian_24.x_*.img of=/dev/sdX bs=4M status=progress conv=fsync
```

Или через **Balena Etcher** (GUI, проще):
1. Открыть Etcher
2. Выбрать .img.xz файл (Etcher сам разархивирует)
3. Выбрать SD карту → Flash

---

## Шаг 2 — Первый запуск Armbian

1. Вставить SD карту в Radxa Zero 3W
2. Подключить HDMI к телевизору
3. Подать питание (USB-C 5V/3A)

**При первом запуске Armbian попросит:**
- Придумать пароль для root
- Создать пользователя (можно пропустить — нажать Ctrl+C)
- Настроить Wi-Fi

**Подключить к Wi-Fi:**
```bash
nmtui
# → Activate a connection → выбрать сеть → ввести пароль
```

**Узнать IP устройства:**
```bash
ip addr show
# или
hostname -I
```

---

## Шаг 3 — Установить QaryxOS

Подключиться по SSH с компьютера:
```bash
ssh root@<IP_УСТРОЙСТВА>
```

Запустить установщик:
```bash
curl -sL https://raw.githubusercontent.com/stepan163s/QaryxOS/main/os/scripts/install.sh | bash
```

Скрипт автоматически:
- Установит mpv, Python, зависимости
- Скачает yt-dlp для aarch64
- Создаст конфиги в `/etc/qaryxos/`
- Зарегистрирует systemd сервисы
- Настроит автозапуск TV UI

Затем настроить лаунчер:
```bash
bash /usr/local/lib/qaryxos/os/scripts/setup-launcher.sh
```

Перезагрузить:
```bash
reboot
```

**После перезагрузки на HDMI сразу появится QaryxOS.**

---

## Что происходит при старте

```
Питание →
  U-Boot (2s) →
  Kernel (2s) →
  systemd (3s):
    qaryxos-mpv.service     (mpv IPC server, /tmp/mpv.sock)
    qaryxos-api.service     (REST API, порт 8080)
    qaryxos-mediashell.service → TV UI на HDMI
  Итого: ~10-11 секунд
```

---

## Управление

**С телефона:**
- Установить QaryxOS Companion App (Android APK из Releases)
- Открыть → ввести IP устройства → Connect
- Использовать как пульт

**Прямое воспроизведение:**
```bash
curl -X POST http://<IP>:8080/play -d '{"url":"https://youtu.be/..."}'
```
