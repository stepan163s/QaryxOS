"""IPTV endpoints — issues #16, #17"""

from fastapi import APIRouter, Request, HTTPException, BackgroundTasks
from pydantic import BaseModel
from typing import Optional

from services.iptv import IptvService
from services.mpv import MpvError

router = APIRouter()


def _svc(request: Request) -> IptvService:
    cfg = request.app.state.config
    return IptvService(cfg.iptv_cache_dir, cfg.playlists_file)


class AddPlaylistRequest(BaseModel):
    url: str
    name: str


@router.get("/playlists")
async def list_playlists(request: Request):
    return [vars(p) for p in _svc(request).get_playlists()]


@router.post("/playlists")
async def add_playlist(req: AddPlaylistRequest, request: Request):
    try:
        pl = await _svc(request).add_playlist(req.url, req.name)
        return vars(pl)
    except Exception as e:
        raise HTTPException(422, str(e))


@router.delete("/playlists/{playlist_id}")
async def remove_playlist(playlist_id: str, request: Request):
    if not _svc(request).remove_playlist(playlist_id):
        raise HTTPException(404, "Playlist not found")
    return {"ok": True}


@router.get("/channels")
async def list_channels(
    request: Request,
    playlist_id: Optional[str] = None,
    group: Optional[str] = None,
):
    channels = _svc(request).get_channels(playlist_id=playlist_id, group=group)
    return [vars(c) for c in channels]


@router.get("/groups")
async def list_groups(request: Request):
    return _svc(request).get_groups()


@router.post("/play/{channel_id}")
async def play_channel(channel_id: str, request: Request):
    """Start playing an IPTV channel directly via mpv loadfile."""
    svc = _svc(request)
    channel = svc.get_channel(channel_id)
    if not channel:
        raise HTTPException(404, "Channel not found")

    mpv = request.app.state.mpv
    try:
        await mpv.load(channel.url, profile="live")   # low-latency profile for live streams
        return {"ok": True, "channel": channel.name, "url": channel.url}
    except MpvError as e:
        raise HTTPException(503, str(e))


@router.post("/refresh")
async def refresh(request: Request, bg: BackgroundTasks):
    """Refresh all IPTV playlists in background."""
    bg.add_task(_svc(request).refresh_all)
    return {"ok": True, "message": "Refresh started"}
