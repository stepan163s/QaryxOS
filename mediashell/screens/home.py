"""
Home screen — tile grid
Tiles: Continue / YouTube / IPTV / URL / Moonlight / Settings
"""

import pygame
from screens import BaseScreen

# Layout constants
TILE_W, TILE_H = 280, 160
TILE_GAP       = 28
TILES_PER_ROW  = 3
MARGIN_X       = 120
MARGIN_Y       = 180

ACCENT  = (100, 120, 220)
TILE_BG = (28, 28, 28)
TILE_HL = (55, 55, 80)
WHITE   = (255, 255, 255)
GRAY    = (160, 160, 160)
BG      = (10, 10, 10)


TILES = [
    {"id": "continue",  "label": "Continue",  "icon": "▶",  "screen": None},
    {"id": "youtube",   "label": "YouTube",   "icon": "📺", "screen": "youtube"},
    {"id": "iptv",      "label": "IPTV",      "icon": "📡", "screen": "iptv"},
    {"id": "url",       "label": "URL",       "icon": "🌐", "screen": "url"},
    {"id": "moonlight", "label": "Moonlight", "icon": "🎮", "screen": None},
    {"id": "settings",  "label": "Settings",  "icon": "⚙",  "screen": None},
]


class HomeScreen(BaseScreen):
    def __init__(self, shell):
        super().__init__(shell)
        self.focused = 0
        self._font_large = None
        self._font_small = None
        self._font_icon  = None

    def on_enter(self) -> None:
        self.focused = 0
        self._ensure_fonts()

    def _ensure_fonts(self) -> None:
        if self._font_large:
            return
        pygame.font.init()
        self._font_large = pygame.font.SysFont("DejaVu Sans", 22, bold=True)
        self._font_small = pygame.font.SysFont("DejaVu Sans", 16)
        self._font_icon  = pygame.font.SysFont("DejaVu Sans", 48)

    def draw(self, surface: pygame.Surface) -> None:
        self._ensure_fonts()
        W, H = surface.get_size()

        # Title
        title = self._font_large.render("QaryxOS", True, ACCENT)
        surface.blit(title, (MARGIN_X, 60))

        # Tiles
        for i, tile in enumerate(TILES):
            row = i // TILES_PER_ROW
            col = i % TILES_PER_ROW
            x = MARGIN_X + col * (TILE_W + TILE_GAP)
            y = MARGIN_Y + row * (TILE_H + TILE_GAP)

            selected = (i == self.focused)
            color = TILE_HL if selected else TILE_BG
            rect = pygame.Rect(x, y, TILE_W, TILE_H)

            # Tile background
            pygame.draw.rect(surface, color, rect, border_radius=12)
            if selected:
                pygame.draw.rect(surface, ACCENT, rect, width=2, border_radius=12)

            # Icon
            icon_surf = self._font_icon.render(tile["icon"], True, WHITE)
            surface.blit(icon_surf, (x + 20, y + 20))

            # Label
            label_surf = self._font_large.render(tile["label"], True, WHITE if selected else GRAY)
            surface.blit(label_surf, (x + 20, y + TILE_H - 44))

    def handle_key(self, key: str) -> None:
        n = len(TILES)
        if key == "right":
            self.focused = (self.focused + 1) % n
        elif key == "left":
            self.focused = (self.focused - 1) % n
        elif key == "down":
            self.focused = min(self.focused + TILES_PER_ROW, n - 1)
        elif key == "up":
            self.focused = max(self.focused - TILES_PER_ROW, 0)
        elif key == "ok":
            tile = TILES[self.focused]
            if tile["screen"]:
                self.shell.navigate(tile["screen"])
            elif tile["id"] == "continue":
                self._play_last()

    def _play_last(self) -> None:
        try:
            self.shell.api.post("/play", json={"url": "__resume__"})
        except Exception:
            pass
