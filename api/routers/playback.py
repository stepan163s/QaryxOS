"""Playback control endpoints — issues #6, #7, #10, #24, #25"""

import asyncio
from fastapi import APIRouter, Request, HTTPException, BackgroundTasks
from pydantic import BaseModel
from typing import Optional

from services.mpv import MpvError
from services import ytdlp
from services.history import HistoryService

router = APIRouter()

# Background position-saver task handle
_position_task: asyncio.Task | None = None


class PlayRequest(BaseModel):
    url: str
    type: Optional[str] = None   # "youtube" | "direct" | "iptv" | auto-detect
    title: Optional[str] = None
    thumbnail: Optional[str] = None
    resume: bool = False         # v1.5: resume from last position


class SeekRequest(BaseModel):
    seconds: float


class VolumeRequest(BaseModel):
    level: int  # 0–100


def _is_youtube(url: str) -> bool:
    return "youtube.com" in url or "youtu.be" in url


async def _save_position_loop(request: Request) -> None:
    """Background task: save mpv position to history every 30s."""
    while True:
        await asyncio.sleep(30)
        try:
            status = await request.app.state.mpv.get_status()
            if status["state"] == "playing" and status.get("url"):
                cfg = request.app.state.config
                svc = HistoryService(cfg.config_dir + "/history.json")
                svc.update_position(status["url"], status["position"])
        except Exception:
            pass


@router.post("/play")
async def play(req: PlayRequest, request: Request, bg: BackgroundTasks):
    """Load and play a URL. Resolves YouTube via yt-dlp. Records to history."""
    global _position_task
    mpv = request.app.state.mpv
    cfg = request.app.state.config
    history = HistoryService(cfg.config_dir + "/history.json")

    # Special __resume__ token — play last from history
    if req.url == "__resume__":
        last = history.get_last()
        if not last:
            raise HTTPException(404, "Nothing to resume")
        req = PlayRequest(url=last.url, type=last.content_type,
                          title=last.title, resume=True)

    original_url = req.url
    url = req.url
    content_type = req.type or ("youtube" if _is_youtube(url) else "direct")

    try:
        if content_type == "youtube":
            url = await ytdlp.resolve_url(original_url)

        # Resume: seek to last position after load
        if req.resume:
            entry = next((e for e in history.get_all() if e.url == original_url), None)
            resume_pos = entry.position if entry and entry.position > 5 else 0
        else:
            resume_pos = 0

        await mpv.load(url)

        if resume_pos > 0:
            await asyncio.sleep(1.5)  # wait for mpv to start
            await mpv.seek(resume_pos - (await mpv.get_property("time-pos") or 0))

        # Record to history
        history.record(
            url=original_url,
            title=req.title or original_url,
            content_type=content_type,
            thumbnail=req.thumbnail or "",
        )

        # Start position-saving background task
        if _position_task is None or _position_task.done():
            _position_task = asyncio.create_task(_save_position_loop(request))

        return {"ok": True, "url": original_url, "type": content_type,
                "resumed_from": resume_pos if resume_pos > 0 else None}
    except ValueError as e:
        raise HTTPException(422, str(e))
    except MpvError as e:
        raise HTTPException(503, f"mpv error: {e}")


@router.get("/history")
async def get_history(request: Request, limit: int = 20):
    """Get playback history (v1.5)."""
    cfg = request.app.state.config
    history = HistoryService(cfg.config_dir + "/history.json")
    return [vars(e) for e in history.get_all(limit)]


@router.delete("/history")
async def clear_history(request: Request):
    """Clear playback history."""
    cfg = request.app.state.config
    HistoryService(cfg.config_dir + "/history.json").clear()
    return {"ok": True}


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
