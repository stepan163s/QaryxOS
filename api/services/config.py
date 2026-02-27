"""QaryxOS configuration loader"""

import os
import json
from dataclasses import dataclass, field


@dataclass
class Config:
    api_port: int = 8080
    api_host: str = "0.0.0.0"
    mpv_socket: str = "/tmp/mpv.sock"
    youtube_refresh_hours: int = 6
    iptv_refresh_hours: int = 24
    default_volume: int = 80
    data_dir: str = "/var/lib/qaryxos"
    config_dir: str = "/etc/qaryxos"

    @classmethod
    def load(cls) -> "Config":
        config_file = os.environ.get("QARYXOS_CONFIG", "/etc/qaryxos/config.json")
        data_dir = os.environ.get("QARYXOS_DATA", "/var/lib/qaryxos")
        config_dir = os.path.dirname(config_file)

        try:
            with open(config_file) as f:
                data = json.load(f)
        except (FileNotFoundError, json.JSONDecodeError):
            data = {}

        return cls(
            api_port=data.get("api_port", 8080),
            api_host=data.get("api_host", "0.0.0.0"),
            mpv_socket=data.get("mpv_socket", "/tmp/mpv.sock"),
            youtube_refresh_hours=data.get("youtube_refresh_hours", 6),
            iptv_refresh_hours=data.get("iptv_refresh_hours", 24),
            default_volume=data.get("default_volume", 80),
            data_dir=data_dir,
            config_dir=config_dir,
        )

    @property
    def channels_file(self) -> str:
        return os.path.join(self.config_dir, "channels.json")

    @property
    def playlists_file(self) -> str:
        return os.path.join(self.config_dir, "playlists.json")

    @property
    def versions_file(self) -> str:
        return os.path.join(self.config_dir, "versions.json")

    @property
    def youtube_cache_dir(self) -> str:
        return os.path.join(self.data_dir, "youtube_cache")

    @property
    def iptv_cache_dir(self) -> str:
        return os.path.join(self.data_dir, "iptv_cache")
