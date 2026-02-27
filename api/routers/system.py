"""System endpoints — health, reboot"""

import subprocess
from fastapi import APIRouter, Request
from services.ota import _read_versions

router = APIRouter()


@router.get("/health")
async def health(request: Request):
    cfg = request.app.state.config
    mpv = request.app.state.mpv
    mpv_ok = await mpv._ensure_connected()
    versions = _read_versions()
    return {
        "status": "ok",
        "version": versions.get("os", "unknown"),
        "mpv": "connected" if mpv_ok else "disconnected",
        "api_port": cfg.api_port,
    }


@router.post("/system/reboot")
async def reboot():
    """Reboot the device. Requires root."""
    subprocess.Popen(["systemctl", "reboot"])
    return {"ok": True, "message": "Rebooting..."}
