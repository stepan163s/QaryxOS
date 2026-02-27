"""Playback control endpoints — issues #6, #7, #10"""

from fastapi import APIRouter, Request, HTTPException
from pydantic import BaseModel
from typing import Optional

from services.mpv import MpvError
from services import ytdlp

router = APIRouter()


class PlayRequest(BaseModel):
    url: str
    type: Optional[str] = None  # "youtube" | "direct" | "iptv" | auto-detect


class SeekRequest(BaseModel):
    seconds: float


class VolumeRequest(BaseModel):
    level: int  # 0–100


def _is_youtube(url: str) -> bool:
    return "youtube.com" in url or "youtu.be" in url


@router.post("/play")
async def play(req: PlayRequest, request: Request):
    """Load and play a URL. Resolves YouTube via yt-dlp."""
    mpv = request.app.state.mpv
    url = req.url
    content_type = req.type or ("youtube" if _is_youtube(url) else "direct")

    try:
        if content_type == "youtube":
            url = await ytdlp.resolve_url(url)
        await mpv.load(url)
        return {"ok": True, "url": url, "type": content_type}
    except ValueError as e:
        raise HTTPException(422, str(e))
    except MpvError as e:
        raise HTTPException(503, f"mpv error: {e}")


@router.post("/pause")
async def pause(request: Request):
    """Toggle play/pause."""
    try:
        paused = await request.app.state.mpv.pause()
        return {"ok": True, "paused": paused}
    except MpvError as e:
        raise HTTPException(503, str(e))


@router.post("/stop")
async def stop(request: Request):
    """Stop playback."""
    try:
        await request.app.state.mpv.stop()
        return {"ok": True}
    except MpvError as e:
        raise HTTPException(503, str(e))


@router.post("/seek")
async def seek(req: SeekRequest, request: Request):
    """Seek relative (positive = forward, negative = back)."""
    try:
        await request.app.state.mpv.seek(req.seconds)
        return {"ok": True, "seconds": req.seconds}
    except MpvError as e:
        raise HTTPException(503, str(e))


@router.post("/volume")
async def volume(req: VolumeRequest, request: Request):
    """Set volume 0–100."""
    try:
        await request.app.state.mpv.set_volume(req.level)
        return {"ok": True, "volume": req.level}
    except MpvError as e:
        raise HTTPException(503, str(e))


@router.get("/status")
async def status(request: Request):
    """Get current mpv playback status."""
    return await request.app.state.mpv.get_status()
