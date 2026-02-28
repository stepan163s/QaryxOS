"""IPTV channel list screen with group navigation"""

import pygame
from screens import BaseScreen

ACCENT  = (100, 120, 220)
TILE_BG = (28, 28, 28)
TILE_HL = (55, 55, 80)
WHITE   = (255, 255, 255)
GRAY    = (140, 140, 140)

ITEM_H  = 56
MARGIN_X = 60
MARGIN_Y = 140
LIST_W   = 860
GROUP_W  = 280


class IptvScreen(BaseScreen):
    def __init__(self, shell):
        super().__init__(shell)
        self.groups: list[str] = []
        self.channels: list[dict] = []
        self.group_idx = 0
        self.ch_idx = 0
        self.pane = "groups"  # "groups" | "channels"
        self._font_hdr  = None
        self._font_item = None
        self._font_sub  = None

    def _ensure_fonts(self) -> None:
        if self._font_hdr:
            return
        pygame.font.init()
        self._font_hdr  = pygame.font.SysFont("DejaVu Sans", 26, bold=True)
        self._font_item = pygame.font.SysFont("DejaVu Sans", 20)
        self._font_sub  = pygame.font.SysFont("DejaVu Sans", 14)

    def on_enter(self) -> None:
        self.group_idx = 0
        self.ch_idx = 0
        self.pane = "groups"
        self._load_groups()

    def _load_groups(self) -> None:
        try:
            resp = self.shell.api.get("/iptv/groups")
            self.groups = resp.json() if resp.is_success else []
            if not self.groups:
                self.groups = ["All"]
        except Exception:
            self.groups = ["All"]
        self._load_channels()

    def _load_channels(self) -> None:
        group = self.groups[self.group_idx] if self.groups else None
        try:
            params = {} if group == "All" else {"group": group}
            resp = self.shell.api.get("/iptv/channels", params=params)
            self.channels = resp.json() if resp.is_success else []
        except Exception:
            self.channels = []
        self.ch_idx = 0

    def draw(self, surface: pygame.Surface) -> None:
        self._ensure_fonts()
        W, H = surface.get_size()

        # Header
        header = self._font_hdr.render("📡  IPTV", True, ACCENT)
        surface.blit(header, (MARGIN_X, 60))

        hint = self._font_sub.render(
            "← → switch pane  |  ↑ ↓ navigate  |  OK play  |  Back home",
            True, GRAY,
        )
        surface.blit(hint, (MARGIN_X, 100))

        # Separator
        pygame.draw.line(surface, (50, 50, 50),
                         (MARGIN_X + GROUP_W + 20, MARGIN_Y - 10),
                         (MARGIN_X + GROUP_W + 20, H - 40), 1)

        # Groups pane
        visible = (H - MARGIN_Y) // ITEM_H
        g_start = max(0, self.group_idx - visible // 2)
        for i, grp in enumerate(self.groups[g_start:g_start + visible]):
            idx = g_start + i
            y = MARGIN_Y + i * ITEM_H
            selected = (idx == self.group_idx)
            active_pane = (self.pane == "groups")

            if selected:
                bg = TILE_HL if active_pane else (40, 40, 50)
                pygame.draw.rect(surface, bg, (MARGIN_X, y, GROUP_W, ITEM_H - 4), border_radius=8)
                if active_pane:
                    pygame.draw.rect(surface, ACCENT, (MARGIN_X, y, GROUP_W, ITEM_H - 4), width=2, border_radius=8)

            color = WHITE if selected else GRAY
            surf = self._font_item.render(grp[:22], True, color)
            surface.blit(surf, (MARGIN_X + 12, y + 14))

        # Channels pane
        ch_x = MARGIN_X + GROUP_W + 40
        if not self.channels:
            msg = self._font_item.render("No channels", True, GRAY)
            surface.blit(msg, (ch_x, MARGIN_Y + 20))
        else:
            c_start = max(0, self.ch_idx - visible // 2)
            for i, ch in enumerate(self.channels[c_start:c_start + visible]):
                idx = c_start + i
                y = MARGIN_Y + i * ITEM_H
                selected = (idx == self.ch_idx)
                active_pane = (self.pane == "channels")

                if selected:
                    bg = TILE_HL if active_pane else (40, 40, 50)
                    pygame.draw.rect(surface, bg, (ch_x, y, LIST_W, ITEM_H - 4), border_radius=8)
                    if active_pane:
                        pygame.draw.rect(surface, ACCENT, (ch_x, y, LIST_W, ITEM_H - 4), width=2, border_radius=8)

                color = WHITE if selected else GRAY
                surf = self._font_item.render(ch.get("name", "")[:48], True, color)
                surface.blit(surf, (ch_x + 12, y + 14))

    def handle_key(self, key: str) -> None:
        if key == "right" and self.pane == "groups":
            self.pane = "channels"
        elif key == "left" and self.pane == "channels":
            self.pane = "groups"
        elif key == "up":
            if self.pane == "groups":
                self.group_idx = max(0, self.group_idx - 1)
                self._load_channels()
            else:
                self.ch_idx = max(0, self.ch_idx - 1)
        elif key == "down":
            if self.pane == "groups" and self.groups:
                self.group_idx = min(len(self.groups) - 1, self.group_idx + 1)
                self._load_channels()
            elif self.pane == "channels" and self.channels:
                self.ch_idx = min(len(self.channels) - 1, self.ch_idx + 1)
        elif key == "ok":
            if self.pane == "channels" and self.channels:
                ch = self.channels[self.ch_idx]
                try:
                    self.shell.api.post(f"/iptv/play/{ch['id']}")
                except Exception:
                    pass
        elif key in ("back", "home"):
            self.shell.navigate("home")
