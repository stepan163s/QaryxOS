"""
QaryxOS mpv IPC service
Communicates with mpv via Unix socket using JSON IPC protocol.
"""

import asyncio
import json
import logging
import time
from typing import Any

logger = logging.getLogger("qaryxos.mpv")

CONNECT_TIMEOUT = 5.0
COMMAND_TIMEOUT = 3.0


class MpvError(Exception):
    pass


class MpvService:
    def __init__(self, socket_path: str = "/tmp/mpv.sock"):
        self.socket_path = socket_path
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._request_id = 0
        self._lock = asyncio.Lock()

    async def connect(self) -> None:
        deadline = time.monotonic() + CONNECT_TIMEOUT
        while time.monotonic() < deadline:
            try:
                self._reader, self._writer = await asyncio.open_unix_connection(self.socket_path)
                logger.info("Connected to mpv IPC at %s", self.socket_path)
                return
            except (FileNotFoundError, ConnectionRefusedError):
                await asyncio.sleep(0.5)
        logger.warning("mpv socket not available — will retry on each request")

    async def disconnect(self) -> None:
        if self._writer:
            self._writer.close()
            try:
                await self._writer.wait_closed()
            except Exception:
                pass

    async def _ensure_connected(self) -> bool:
        if self._writer and not self._writer.is_closing():
            return True
        try:
            self._reader, self._writer = await asyncio.wait_for(
                asyncio.open_unix_connection(self.socket_path),
                timeout=1.0,
            )
            return True
        except Exception:
            return False

    async def command(self, *args: Any) -> Any:
        async with self._lock:
            if not await self._ensure_connected():
                raise MpvError("mpv not connected")

            self._request_id += 1
            req_id = self._request_id
            payload = json.dumps({"command": list(args), "request_id": req_id}) + "\n"

            try:
                self._writer.write(payload.encode())
                await self._writer.drain()

                async with asyncio.timeout(COMMAND_TIMEOUT):
                    while True:
                        line = await self._reader.readline()
                        if not line:
                            raise MpvError("mpv socket closed")
                        try:
                            data = json.loads(line)
                        except json.JSONDecodeError:
                            continue  # skip malformed mpv event lines
                        if data.get("request_id") == req_id:
                            if data.get("error") != "success":
                                raise MpvError(data.get("error", "unknown"))
                            return data.get("data")
            except asyncio.TimeoutError:
                raise MpvError("mpv command timed out")

    async def is_connected(self) -> bool:
        """Public health-check: True if mpv socket is responsive."""
        return await self._ensure_connected()

    async def get_property(self, prop: str) -> Any:
        return await self.command("get_property", prop)

    async def set_property(self, prop: str, value: Any) -> None:
        await self.command("set_property", prop, value)

    # --- High-level controls ---

    async def load(self, url: str, profile: str | None = None) -> None:
        if profile:
            await self.set_property("profile", profile)
        await self.command("loadfile", url, "replace")

    async def pause(self) -> bool:
        """Toggle pause. Returns new pause state."""
        current = await self.get_property("pause")
        new_state = not current
        await self.set_property("pause", new_state)
        return new_state

    async def stop(self) -> None:
        await self.command("stop")

    async def seek(self, seconds: float) -> None:
        await self.command("seek", seconds, "relative")

    async def set_volume(self, level: int) -> None:
        level = max(0, min(100, level))
        await self.set_property("volume", level)

    async def get_status(self) -> dict:
        try:
            idle = await self.get_property("idle-active")
            if idle:
                return {"state": "idle", "url": None, "position": 0, "duration": 0, "volume": 80, "paused": False}

            return {
                "state": "playing" if not await self.get_property("pause") else "paused",
                "url":      await self.get_property("path"),
                "position": await self.get_property("time-pos") or 0,
                "duration": await self.get_property("duration") or 0,
                "volume":   await self.get_property("volume") or 80,
                "paused":   await self.get_property("pause"),
            }
        except MpvError:
            return {"state": "error", "url": None, "position": 0, "duration": 0, "volume": 0, "paused": False}
