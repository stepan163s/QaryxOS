"""Base screen class for mediashell screens."""

import pygame


class BaseScreen:
    def __init__(self, shell):
        self.shell = shell
        self.focused = 0

    def on_enter(self) -> None:
        """Called when navigating to this screen."""

    def draw(self, surface: pygame.Surface) -> None:
        """Render this screen."""

    def handle_key(self, key: str) -> None:
        """Handle d-pad key event."""
        if key == "home":
            self.shell.navigate("home")
        elif key == "back":
            self.shell.navigate("home")
