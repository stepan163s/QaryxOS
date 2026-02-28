"""YouTube feed screen — tile grid of videos per channel"""

import pygame
from screens import BaseScreen

ACCENT  = (100, 120, 220)
TILE_BG = (28, 28, 28)
TILE_HL = (55, 55, 80)
WHITE   = (255, 255, 255)
GRAY    = (140, 140, 140)
BG      = (10, 10, 10)

TILE_W, TILE_H = 320, 190
TILE_GAP = 24
TILES_PER_ROW = 4
MARGIN_X = 60
MARGIN_Y = 140


class YoutubeScreen(BaseScreen):
    def __init__(self, shell):
        super().__init__(shell)
        self.videos = []
        self.focused = 0
        self._font_title = None
        self._font_sub   = None
        self._font_hdr   = None

    def _ensure_fonts(self) -> None:
        if self._font_title:
            return
        pygame.font.init()
        self._font_hdr   = pygame.font.SysFont("DejaVu Sans", 26, bold=True)
        self._font_title = pygame.font.SysFont("DejaVu Sans", 17, bold=True)
        self._font_sub   = pygame.font.SysFont("DejaVu Sans", 14)

    def on_enter(self) -> None:
        self.focused = 0
        self._load_feed()

    def _load_feed(self) -> None:
        try:
            resp = self.shell.api.get("/youtube/feed", params={"limit": 40})
            self.videos = resp.json() if resp.is_success else []
        except Exception:
            self.videos = []
        self.focused = min(self.focused, len(self.videos) - 1) if self.videos else 0

    def draw(self, surface: pygame.Surface) -> None:
        self._ensure_fonts()

        # Header
        header = self._font_hdr.render("📺  YouTube", True, ACCENT)
        surface.blit(header, (MARGIN_X, 60))

        hint = self._font_sub.render("OK — play  |  Back — home  |  ← → ↑ ↓ — navigate", True, GRAY)
        surface.blit(hint, (MARGIN_X, 100))

        if not self.videos:
            msg = self._font_title.render("No videos — add channels in companion app", True, GRAY)
            surface.blit(msg, (MARGIN_X, 420))
            return

        for i, video in enumerate(self.videos):
            row = i // TILES_PER_ROW
            col = i % TILES_PER_ROW
            x = MARGIN_X + col * (TILE_W + TILE_GAP)
            y = MARGIN_Y + row * (TILE_H + TILE_GAP)

            if y + TILE_H > surface.get_height() - 20:
                break

            selected = (i == self.focused)
            color = TILE_HL if selected else TILE_BG
            rect = pygame.Rect(x, y, TILE_W, TILE_H)
            pygame.draw.rect(surface, color, rect, border_radius=10)
            if selected:
                pygame.draw.rect(surface, ACCENT, rect, width=2, border_radius=10)

            # Thumbnail placeholder
            thumb_rect = pygame.Rect(x + 8, y + 8, TILE_W - 16, 110)
            pygame.draw.rect(surface, (40, 40, 40), thumb_rect, border_radius=6)

            # Title (wrapped to 2 lines)
            title = video.get("title", "")[:60]
            title_surf = self._font_title.render(title[:42], True, WHITE if selected else (210, 210, 210))
            surface.blit(title_surf, (x + 8, y + 124))

            ch = video.get("channel_name", "")[:30]
            ch_surf = self._font_sub.render(ch, True, GRAY)
            surface.blit(ch_surf, (x + 8, y + 148))

            dur = video.get("duration", 0)
            if dur:
                dur_str = f"{int(dur // 60)}:{int(dur % 60):02d}"
                dur_surf = self._font_sub.render(dur_str, True, GRAY)
                surface.blit(dur_surf, (x + TILE_W - 50, y + 148))

    def handle_key(self, key: str) -> None:
        n = max(len(self.videos), 1)
        if key == "right":
            self.focused = min(self.focused + 1, n - 1)
        elif key == "left":
            self.focused = max(self.focused - 1, 0)
        elif key == "down":
            self.focused = min(self.focused + TILES_PER_ROW, n - 1)
        elif key == "up":
            self.focused = max(self.focused - TILES_PER_ROW, 0)
        elif key == "ok" and self.videos:
            video = self.videos[self.focused]
            try:
                self.shell.api.post("/youtube/play", json={"url": video["url"]})
            except Exception:
                pass
        elif key in ("back", "home"):
            self.shell.navigate("home")
