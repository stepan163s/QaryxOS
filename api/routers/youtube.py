"""YouTube channel management endpoints — issues #14, #15"""

import asyncio
from fastapi import APIRouter, Request, HTTPException, BackgroundTasks
from pydantic import BaseModel

from services.youtube import YoutubeService
from services.mpv import MpvError
from services import ytdlp

router = APIRouter()


def _svc(request: Request) -> YoutubeService:
    cfg = request.app.state.config
    return YoutubeService(cfg.youtube_cache_dir, cfg.channels_file)


class AddChannelRequest(BaseModel):
    url: str


class PlayVideoRequest(BaseModel):
    url: str


@router.get("/channels")
async def list_channels(request: Request):
    return [vars(c) for c in _svc(request).get_channels()]


@router.post("/channels")
async def add_channel(req: AddChannelRequest, request: Request, bg: BackgroundTasks):
    try:
        channel = await _svc(request).add_channel(req.url)
        return vars(channel)
    except Exception as e:
        raise HTTPException(422, str(e))


@router.delete("/channels/{channel_id}")
async def remove_channel(channel_id: str, request: Request):
    if not _svc(request).remove_channel(channel_id):
        raise HTTPException(404, "Channel not found")
    return {"ok": True}


@router.get("/feed")
async def get_feed(request: Request, limit: int = 50):
    return [vars(v) for v in _svc(request).get_feed(limit)]


@router.post("/refresh")
async def refresh_all(request: Request, bg: BackgroundTasks):
    """Trigger async refresh of all channel feeds."""
    svc = _svc(request)
    bg.add_task(svc.refresh_all)
    return {"ok": True, "message": "Refresh started in background"}


@router.post("/play")
async def play_video(req: PlayVideoRequest, request: Request):
    """Resolve YouTube URL and play on device."""
    mpv = request.app.state.mpv
    try:
        stream_url = await ytdlp.resolve_url(req.url)
        await mpv.load(stream_url)
        return {"ok": True}
    except ValueError as e:
        raise HTTPException(422, str(e))
    except MpvError as e:
        raise HTTPException(503, str(e))
