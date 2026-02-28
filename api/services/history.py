"""
QaryxOS playback history service — v1.5
Tracks what was played and last position for resume.
"""

import json
import logging
import os
import time
from dataclasses import dataclass, asdict
from typing import Optional

logger = logging.getLogger("qaryxos.history")

HISTORY_FILE = "/etc/qaryxos/history.json"
MAX_ENTRIES  = 50


@dataclass
class HistoryEntry:
    url: str
    title: str
    content_type: str       # youtube | iptv | direct
    channel_name: str = ""  # for IPTV
    thumbnail: str = ""
    duration: float = 0
    position: float = 0     # last known playback position (seconds)
    played_at: float = 0    # unix timestamp


class HistoryService:
    def __init__(self, history_file: str = HISTORY_FILE):
        self.history_file = history_file

    def _load(self) -> list[HistoryEntry]:
        try:
            with open(self.history_file) as f:
                return [HistoryEntry(**e) for e in json.load(f)]
        except (FileNotFoundError, json.JSONDecodeError, TypeError):
            return []

    def _save(self, entries: list[HistoryEntry]) -> None:
        os.makedirs(os.path.dirname(self.history_file), exist_ok=True)
        tmp = self.history_file + ".tmp"
        with open(tmp, "w") as f:
            json.dump([asdict(e) for e in entries[:MAX_ENTRIES]], f, indent=2)
        os.replace(tmp, self.history_file)  # atomic: no partial reads

    def record(
        self,
        url: str,
        title: str,
        content_type: str = "direct",
        channel_name: str = "",
        thumbnail: str = "",
        duration: float = 0,
    ) -> None:
        entries = self._load()
        # Remove existing entry for same URL to avoid duplicates
        entries = [e for e in entries if e.url != url]
        entry = HistoryEntry(
            url=url,
            title=title,
            content_type=content_type,
            channel_name=channel_name,
            thumbnail=thumbnail,
            duration=duration,
            position=0,
            played_at=time.time(),
        )
        entries.insert(0, entry)
        self._save(entries)

    def update_position(self, url: str, position: float) -> None:
        """Update the last known playback position for a URL."""
        entries = self._load()
        for entry in entries:
            if entry.url == url:
                entry.position = position
                break
        self._save(entries)

    def get_last(self) -> Optional[HistoryEntry]:
        entries = self._load()
        return entries[0] if entries else None

    def get_all(self, limit: int = 20) -> list[HistoryEntry]:
        return self._load()[:limit]

    def clear(self) -> None:
        self._save([])
