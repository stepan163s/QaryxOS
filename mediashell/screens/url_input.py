"""URL input screen — share/type a URL to play"""

import pygame
from screens import BaseScreen

ACCENT = (100, 120, 220)
WHITE  = (255, 255, 255)
GRAY   = (140, 140, 140)
BG_BOX = (30, 30, 40)


class UrlInputScreen(BaseScreen):
    def __init__(self, shell):
        super().__init__(shell)
        self.url = ""
        self._font_hdr  = None
        self._font_url  = None
        self._font_hint = None

    def _ensure_fonts(self) -> None:
        if self._font_hdr:
            return
        pygame.font.init()
        self._font_hdr  = pygame.font.SysFont("DejaVu Sans", 28, bold=True)
        self._font_url  = pygame.font.SysFont("DejaVu Mono", 22)
        self._font_hint = pygame.font.SysFont("DejaVu Sans", 16)

    def on_enter(self) -> None:
        self.url = ""
        pygame.key.start_text_input()

    def draw(self, surface: pygame.Surface) -> None:
        self._ensure_fonts()
        W, H = surface.get_size()

        hdr = self._font_hdr.render("🌐  Play URL", True, ACCENT)
        surface.blit(hdr, (100, 160))

        hint = self._font_hint.render(
            "Type URL or send via companion app  |  Enter — play  |  Esc — back",
            True, GRAY,
        )
        surface.blit(hint, (100, 210))

        # Input box
        box = pygame.Rect(100, 280, W - 200, 60)
        pygame.draw.rect(surface, BG_BOX, box, border_radius=8)
        pygame.draw.rect(surface, ACCENT, box, width=2, border_radius=8)

        url_display = self.url[-70:] if len(self.url) > 70 else self.url
        url_surf = self._font_url.render(url_display + "▌", True, WHITE)
        surface.blit(url_surf, (box.x + 12, box.y + 14))

    def handle_key(self, key: str) -> None:
        # API key events
        if key == "ok":
            self._play()
        elif key in ("back", "home"):
            pygame.key.stop_text_input()
            self.shell.navigate("home")

    def _play(self) -> None:
        if not self.url.strip():
            return
        try:
            self.shell.api.post("/play", json={"url": self.url.strip()})
        except Exception:
            pass

    # pygame text events are handled in main loop — mediashell passes them here
    def on_text_input(self, text: str) -> None:
        self.url += text

    def on_keydown(self, event: pygame.event.Event) -> None:
        if event.key == pygame.K_BACKSPACE:
            self.url = self.url[:-1]
        elif event.key in (pygame.K_RETURN, pygame.K_KP_ENTER):
            self._play()
        elif event.key == pygame.K_ESCAPE:
            pygame.key.stop_text_input()
            self.shell.navigate("home")
