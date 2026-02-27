"""
QaryxOS IPTV service
M3U/M3U8 playlist parser and channel cache manager.
"""

import asyncio
import json
import logging
import os
import re
import time
from dataclasses import dataclass, field, asdict
from typing import Optional
import httpx

logger = logging.getLogger("qaryxos.iptv")


@dataclass
class IptvChannel:
    id: str
    name: str
    url: str
    group: str = ""
    logo: str = ""
    tvg_id: str = ""
    playlist_id: str = ""


@dataclass
class IptvPlaylist:
    id: str
    name: str
    url: str
    updated_at: float = 0.0
    channel_count: int = 0


def _parse_m3u(content: str, playlist_id: str) -> list[IptvChannel]:
    channels = []
    lines = content.splitlines()
    current_meta: dict = {}

    for line in lines:
        line = line.strip()
        if not line:
            continue

        if line.startswith("#EXTINF:"):
            current_meta = {}
            # Parse attributes: tvg-id="..." tvg-logo="..." group-title="..." ,Name
            attrs_part, _, name = line.partition(",")
            current_meta["name"] = name.strip()

            for key, value in re.findall(r'([\w-]+)="([^"]*)"', attrs_part):
                current_meta[key.lower().replace("-", "_")] = value

        elif line.startswith("#"):
            continue

        elif line and current_meta:
            ch_id = current_meta.get("tvg_id") or re.sub(r"[^\w]", "_", current_meta.get("name", ""))[:32]
            ch_id = f"{playlist_id}_{ch_id}_{len(channels)}"

            channels.append(IptvChannel(
                id=ch_id,
                name=current_meta.get("name", "Unknown"),
                url=line,
                group=current_meta.get("group_title", ""),
                logo=current_meta.get("tvg_logo", ""),
                tvg_id=current_meta.get("tvg_id", ""),
                playlist_id=playlist_id,
            ))
            current_meta = {}

    return channels


class IptvService:
    def __init__(self, cache_dir: str, playlists_file: str):
        self.cache_dir = cache_dir
        self.playlists_file = playlists_file
        os.makedirs(cache_dir, exist_ok=True)

    def _load_playlists(self) -> list[IptvPlaylist]:
        try:
            with open(self.playlists_file) as f:
                data = json.load(f)
            return [IptvPlaylist(**p) for p in data.get("playlists", [])]
        except (FileNotFoundError, json.JSONDecodeError, TypeError):
            return []

    def _save_playlists(self, playlists: list[IptvPlaylist]) -> None:
        os.makedirs(os.path.dirname(self.playlists_file), exist_ok=True)
        with open(self.playlists_file, "w") as f:
            json.dump({"playlists": [asdict(p) for p in playlists]}, f, indent=2)

    def _cache_path(self, playlist_id: str) -> str:
        return os.path.join(self.cache_dir, f"{playlist_id}.json")

    def _load_channels_cache(self, playlist_id: str) -> list[IptvChannel]:
        try:
            with open(self._cache_path(playlist_id)) as f:
                return [IptvChannel(**c) for c in json.load(f)]
        except (FileNotFoundError, json.JSONDecodeError, TypeError):
            return []

    def _save_channels_cache(self, playlist_id: str, channels: list[IptvChannel]) -> None:
        with open(self._cache_path(playlist_id), "w") as f:
            json.dump([asdict(c) for c in channels], f)

    async def add_playlist(self, url: str, name: str) -> IptvPlaylist:
        playlists = self._load_playlists()
        pl_id = re.sub(r"[^\w]", "_", name)[:20] + f"_{int(time.time())}"
        playlist = IptvPlaylist(id=pl_id, name=name, url=url)
        playlists.append(playlist)
        self._save_playlists(playlists)
        # Immediately fetch
        await self.refresh_playlist(playlist)
        return playlist

    def remove_playlist(self, playlist_id: str) -> bool:
        playlists = self._load_playlists()
        new = [p for p in playlists if p.id != playlist_id]
        if len(new) == len(playlists):
            return False
        self._save_playlists(new)
        cache = self._cache_path(playlist_id)
        if os.path.exists(cache):
            os.remove(cache)
        return True

    async def refresh_playlist(self, playlist: IptvPlaylist) -> int:
        try:
            async with httpx.AsyncClient(timeout=30, follow_redirects=True) as client:
                resp = await client.get(playlist.url)
                resp.raise_for_status()
                content = resp.text

            channels = _parse_m3u(content, playlist.id)
            self._save_channels_cache(playlist.id, channels)

            # Update metadata
            playlists = self._load_playlists()
            for p in playlists:
                if p.id == playlist.id:
                    p.updated_at = time.time()
                    p.channel_count = len(channels)
            self._save_playlists(playlists)

            logger.info("Refreshed playlist %s: %d channels", playlist.name, len(channels))
            return len(channels)

        except Exception as e:
            logger.error("Failed to refresh playlist %s: %s", playlist.name, e)
            raise

    async def refresh_all(self) -> dict:
        playlists = self._load_playlists()
        results = {}
        for pl in playlists:
            try:
                count = await self.refresh_playlist(pl)
                results[pl.id] = {"ok": True, "channels": count}
            except Exception as e:
                results[pl.id] = {"ok": False, "error": str(e)}
        return results

    def get_playlists(self) -> list[IptvPlaylist]:
        return self._load_playlists()

    def get_channels(self, playlist_id: Optional[str] = None, group: Optional[str] = None) -> list[IptvChannel]:
        playlists = self._load_playlists()
        channels = []

        for pl in playlists:
            if playlist_id and pl.id != playlist_id:
                continue
            channels.extend(self._load_channels_cache(pl.id))

        if group:
            channels = [c for c in channels if c.group == group]

        return channels

    def get_channel(self, channel_id: str) -> Optional[IptvChannel]:
        for ch in self.get_channels():
            if ch.id == channel_id:
                return ch
        return None

    def get_groups(self) -> list[str]:
        groups = sorted(set(c.group for c in self.get_channels() if c.group))
        return groups
