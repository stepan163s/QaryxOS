"""TV UI navigation endpoints — issue #9"""

import asyncio
import logging
from fastapi import APIRouter, Request, HTTPException
from pydantic import BaseModel
from typing import Literal

logger = logging.getLogger("qaryxos.api.ui")
router = APIRouter()

# Shared UI state — mediashell reads this via API
_ui_state = {"screen": "home", "focused": 0}

KeyType = Literal["up", "down", "left", "right", "ok", "back", "home", "menu"]


class KeyRequest(BaseModel):
    key: KeyType


@router.post("/key")
async def send_key(req: KeyRequest, request: Request):
    """
    Send a d-pad key event to the TV UI.
    mediashell listens on this endpoint via long-poll or the state endpoint.
    """
    _ui_state["last_key"] = req.key

    # Emit key event to mediashell via shared queue (set on app startup)
    queue: asyncio.Queue | None = getattr(request.app.state, "ui_key_queue", None)
    if queue:
        await queue.put(req.key)

    return {"ok": True, "key": req.key}


@router.get("/state")
async def get_state():
    """Get current UI screen state."""
    return _ui_state


@router.post("/navigate")
async def navigate(screen: str, request: Request):
    """Directly navigate to a named screen (home/youtube/iptv/settings)."""
    valid = {"home", "youtube", "iptv", "url", "settings", "moonlight"}
    if screen not in valid:
        raise HTTPException(400, f"Unknown screen: {screen}")
    _ui_state["screen"] = screen

    queue: asyncio.Queue | None = getattr(request.app.state, "ui_key_queue", None)
    if queue:
        await queue.put(f"navigate:{screen}")

    return {"ok": True, "screen": screen}
