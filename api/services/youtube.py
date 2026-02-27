"""
QaryxOS YouTube channel manager
Stores channel list and caches video metadata.
"""

import asyncio
import json
import logging
import os
import re
import time
from dataclasses import dataclass, asdict
from typing import Optional

from services import ytdlp

logger = logging.getLogger("qaryxos.youtube")


@dataclass
class YoutubeChannel:
    id: str
    name: str
    url: str
    added_at: float = 0.0
    updated_at: float = 0.0


@dataclass
class YoutubeVideo:
    id: str
    title: str
    url: str
    channel_id: str
    channel_name: str
    duration: int = 0
    thumbnail: str = ""


class YoutubeService:
    def __init__(self, cache_dir: str, channels_file: str):
        self.cache_dir = cache_dir
        self.channels_file = channels_file
        os.makedirs(cache_dir, exist_ok=True)

    def _load_channels(self) -> list[YoutubeChannel]:
        try:
            with open(self.channels_file) as f:
                data = json.load(f)
            return [YoutubeChannel(**c) for c in data.get("channels", [])]
        except (FileNotFoundError, json.JSONDecodeError, TypeError):
            return []

    def _save_channels(self, channels: list[YoutubeChannel]) -> None:
        os.makedirs(os.path.dirname(self.channels_file), exist_ok=True)
        with open(self.channels_file, "w") as f:
            json.dump({"channels": [asdict(c) for c in channels]}, f, indent=2)

    def _video_cache_path(self, channel_id: str) -> str:
        return os.path.join(self.cache_dir, f"{channel_id}.json")

    def _load_video_cache(self, channel_id: str) -> list[YoutubeVideo]:
        try:
            with open(self._video_cache_path(channel_id)) as f:
                return [YoutubeVideo(**v) for v in json.load(f)]
        except (FileNotFoundError, json.JSONDecodeError, TypeError):
            return []

    def _save_video_cache(self, channel_id: str, videos: list[YoutubeVideo]) -> None:
        with open(self._video_cache_path(channel_id), "w") as f:
            json.dump([asdict(v) for v in videos], f, indent=2)

    def _channel_id_from_url(self, url: str) -> str:
        # Extract a stable ID from URL for storage
        for pattern in [r"@([\w-]+)", r"/channel/([\w-]+)", r"/c/([\w-]+)", r"/user/([\w-]+)"]:
            m = re.search(pattern, url)
            if m:
                return m.group(1)
        return re.sub(r"[^\w]", "_", url)[-24:]

    async def add_channel(self, url: str) -> YoutubeChannel:
        channels = self._load_channels()
        ch_id = self._channel_id_from_url(url)

        # Check duplicate
        for ch in channels:
            if ch.id == ch_id or ch.url == url:
                return ch

        # Fetch channel name from yt-dlp
        name = ch_id
        try:
            videos = await ytdlp.get_channel_videos(url, max_videos=1)
            if videos and videos[0].get("channel"):
                name = videos[0]["channel"]
        except Exception:
            pass

        channel = YoutubeChannel(
            id=ch_id,
            name=name,
            url=url,
            added_at=time.time(),
        )
        channels.append(channel)
        self._save_channels(channels)

        # Fetch initial feed
        await self.refresh_channel(channel)
        return channel

    def remove_channel(self, channel_id: str) -> bool:
        channels = self._load_channels()
        new = [c for c in channels if c.id != channel_id]
        if len(new) == len(channels):
            return False
        self._save_channels(new)
        cache = self._video_cache_path(channel_id)
        if os.path.exists(cache):
            os.remove(cache)
        return True

    async def refresh_channel(self, channel: YoutubeChannel, max_videos: int = 10) -> int:
        try:
            raw_videos = await ytdlp.get_channel_videos(channel.url, max_videos)
            videos = [
                YoutubeVideo(
                    id=v["id"],
                    title=v["title"],
                    url=v["url"],
                    channel_id=channel.id,
                    channel_name=channel.name,
                    duration=v.get("duration", 0),
                    thumbnail=v.get("thumbnail", ""),
                )
                for v in raw_videos
            ]
            self._save_video_cache(channel.id, videos)

            # Update last refreshed timestamp
            channels = self._load_channels()
            for ch in channels:
                if ch.id == channel.id:
                    ch.updated_at = time.time()
            self._save_channels(channels)

            logger.info("Refreshed channel %s: %d videos", channel.name, len(videos))
            return len(videos)
        except Exception as e:
            logger.error("Failed to refresh channel %s: %s", channel.name, e)
            raise

    async def refresh_all(self) -> dict:
        channels = self._load_channels()
        results = {}
        for ch in channels:
            try:
                count = await self.refresh_channel(ch)
                results[ch.id] = {"ok": True, "videos": count}
            except Exception as e:
                results[ch.id] = {"ok": False, "error": str(e)}
        return results

    def get_channels(self) -> list[YoutubeChannel]:
        return self._load_channels()

    def get_feed(self, limit: int = 50) -> list[YoutubeVideo]:
        """Return merged feed from all channels, sorted by channel order."""
        channels = self._load_channels()
        all_videos = []
        for ch in channels:
            all_videos.extend(self._load_video_cache(ch.id))
        return all_videos[:limit]

    def get_channel(self, channel_id: str) -> Optional[YoutubeChannel]:
        for ch in self._load_channels():
            if ch.id == channel_id:
                return ch
        return None
