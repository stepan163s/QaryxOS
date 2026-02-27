"""
QaryxOS mediashell — TV UI
Runs on HDMI via DRM/KMS using pygame (SDL_VIDEODRIVER=kmsdrm).
Renders tile-based home screen, navigated via d-pad.
"""

import asyncio
import json
import logging
import os
import sys
import threading
import time

import pygame
import httpx

from screens.home import HomeScreen
from screens.youtube import YoutubeScreen
from screens.iptv import IptvScreen
from screens.url_input import UrlInputScreen

logging.basicConfig(level=logging.WARNING, format="%(asctime)s %(levelname)s %(message)s")
logger = logging.getLogger("mediashell")

API_BASE = os.environ.get("QARYXOS_API", "http://localhost:8080")
FPS = 30

# ── Palette ─────────────────────────────────────────────────────────────────
BG      = (10,  10,  10)
TILE_BG = (28,  28,  28)
TILE_HL = (48,  48,  60)
ACCENT  = (100, 120, 220)
WHITE   = (255, 255, 255)
GRAY    = (160, 160, 160)

# ── Resolution ───────────────────────────────────────────────────────────────
W, H = 1920, 1080


class MediaShell:
    def __init__(self):
        self.screen: pygame.Surface | None = None
        self.clock = pygame.time.Clock()
        self.current_screen = "home"
        self.api = httpx.Client(base_url=API_BASE, timeout=5)
        self.key_queue: list[str] = []

        self.screens = {
            "home":     HomeScreen(self),
            "youtube":  YoutubeScreen(self),
            "iptv":     IptvScreen(self),
            "url":      UrlInputScreen(self),
        }

        # Background thread: poll API for key events
        self._poll_thread = threading.Thread(target=self._poll_keys, daemon=True)

    def _poll_keys(self) -> None:
        """Poll /ui/state for key events sent from companion app."""
        last_key = None
        while True:
            try:
                resp = self.api.get("/ui/state")
                data = resp.json()
                key = data.get("last_key")
                if key and key != last_key:
                    last_key = key
                    self.key_queue.append(key)
            except Exception:
                pass
            time.sleep(0.1)

    def navigate(self, screen_name: str) -> None:
        self.current_screen = screen_name
        if screen_name in self.screens:
            self.screens[screen_name].on_enter()

    def run(self) -> None:
        pygame.init()

        # Try KMS first (real device), fall back to windowed (dev)
        try:
            self.screen = pygame.display.set_mode((W, H), pygame.FULLSCREEN | pygame.NOFRAME)
        except Exception:
            self.screen = pygame.display.set_mode((W, H))

        pygame.display.set_caption("QaryxOS")
        pygame.mouse.set_visible(False)

        self._poll_thread.start()
        self.screens["home"].on_enter()

        while True:
            events = pygame.event.get()
            for event in events:
                if event.type == pygame.QUIT:
                    self._quit()
                elif event.type == pygame.KEYDOWN:
                    self._handle_key_event(event.key)

            # Process API key events
            while self.key_queue:
                key = self.key_queue.pop(0)
                if key.startswith("navigate:"):
                    self.navigate(key.split(":", 1)[1])
                else:
                    self._handle_key(key)

            current = self.screens.get(self.current_screen)
            if current:
                self.screen.fill(BG)
                current.draw(self.screen)
                pygame.display.flip()

            self.clock.tick(FPS)

    def _handle_key_event(self, pygame_key: int) -> None:
        KEY_MAP = {
            pygame.K_UP:     "up",
            pygame.K_DOWN:   "down",
            pygame.K_LEFT:   "left",
            pygame.K_RIGHT:  "right",
            pygame.K_RETURN: "ok",
            pygame.K_KP_ENTER: "ok",
            pygame.K_ESCAPE: "back",
            pygame.K_BACKSPACE: "back",
            pygame.K_h:      "home",
        }
        key = KEY_MAP.get(pygame_key)
        if key:
            self._handle_key(key)

    def _handle_key(self, key: str) -> None:
        current = self.screens.get(self.current_screen)
        if current:
            current.handle_key(key)

    def _quit(self) -> None:
        self.api.close()
        pygame.quit()
        sys.exit(0)


if __name__ == "__main__":
    shell = MediaShell()
    shell.run()
