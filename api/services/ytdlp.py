"""
QaryxOS yt-dlp service
Resolves YouTube URLs to direct stream URLs using yt-dlp binary.
"""

import asyncio
import json
import logging
import os
from typing import Optional

logger = logging.getLogger("qaryxos.ytdlp")

YTDLP_BIN = os.environ.get("YTDLP_BIN", "/usr/local/bin/yt-dlp")
RESOLVE_TIMEOUT = 30


async def resolve_url(url: str, quality: str = "1080") -> str:
    """
    Resolve a YouTube (or any yt-dlp compatible) URL to a direct stream URL.
    Falls back to 720p if 1080p unavailable.
    """
    format_spec = f"bestvideo[height<={quality}]+bestaudio/bestvideo+bestaudio/best"

    cmd = [
        YTDLP_BIN,
        "--no-warnings",
        "--no-playlist",
        "-f", format_spec,
        "--get-url",
        url,
    ]

    try:
        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        stdout, stderr = await asyncio.wait_for(proc.communicate(), timeout=RESOLVE_TIMEOUT)

        if proc.returncode != 0:
            err = stderr.decode().strip()
            logger.error("yt-dlp error for %s: %s", url, err)
            raise ValueError(f"yt-dlp failed: {err[:200]}")

        stream_url = stdout.decode().strip()
        if not stream_url:
            raise ValueError("yt-dlp returned empty URL")

        # yt-dlp may return multiple URLs (video+audio) — take first (mpv handles DASH itself)
        lines = stream_url.splitlines()
        return lines[0] if lines else stream_url

    except asyncio.TimeoutError:
        raise ValueError("yt-dlp timed out (>30s)")


async def get_video_info(url: str) -> dict:
    """Fetch video metadata (title, duration, thumbnail) without downloading."""
    cmd = [
        YTDLP_BIN,
        "--no-warnings",
        "--no-playlist",
        "--dump-json",
        "--skip-download",
        url,
    ]

    try:
        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        stdout, stderr = await asyncio.wait_for(proc.communicate(), timeout=30)

        if proc.returncode != 0:
            return {}

        data = json.loads(stdout.decode())
        return {
            "id":        data.get("id", ""),
            "title":     data.get("title", ""),
            "duration":  data.get("duration", 0),
            "thumbnail": data.get("thumbnail", ""),
            "channel":   data.get("channel", ""),
            "url":       url,
        }
    except Exception as e:
        logger.warning("get_video_info failed for %s: %s", url, e)
        return {}


async def get_channel_videos(channel_url: str, max_videos: int = 10) -> list[dict]:
    """Fetch latest N videos from a YouTube channel."""
    cmd = [
        YTDLP_BIN,
        "--no-warnings",
        "--flat-playlist",
        f"--playlist-end={max_videos}",
        "--dump-json",
        channel_url,
    ]

    try:
        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        stdout, stderr = await asyncio.wait_for(proc.communicate(), timeout=60)

        if proc.returncode != 0:
            return []

        videos = []
        for line in stdout.decode().splitlines():
            try:
                item = json.loads(line)
                videos.append({
                    "id":        item.get("id", ""),
                    "title":     item.get("title", ""),
                    "duration":  item.get("duration", 0),
                    "thumbnail": item.get("thumbnails", [{}])[-1].get("url", "") if item.get("thumbnails") else "",
                    "url":       f"https://www.youtube.com/watch?v={item.get('id', '')}",
                    "channel":   item.get("channel", ""),
                })
            except json.JSONDecodeError:
                continue

        return videos
    except asyncio.TimeoutError:
        logger.warning("get_channel_videos timed out for %s", channel_url)
        return []


async def get_yt_dlp_version() -> str:
    try:
        proc = await asyncio.create_subprocess_exec(
            YTDLP_BIN, "--version",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL,
        )
        stdout, _ = await asyncio.wait_for(proc.communicate(), timeout=5)
        return stdout.decode().strip()
    except Exception:
        return "unknown"
