"""
QaryxOS REST API Daemon
FastAPI application — runs on Radxa Zero 3W, port 8080
"""

import asyncio
import os
import json
import logging
from contextlib import asynccontextmanager
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from routers import playback, ui, youtube, iptv, ota, system
from services.mpv import MpvService
from services.config import Config

logging.basicConfig(
    level=logging.WARNING,
    format="%(asctime)s [%(name)s] %(levelname)s: %(message)s",
)
logger = logging.getLogger("qaryxos.api")


@asynccontextmanager
async def lifespan(app: FastAPI):
    logger.info("QaryxOS API starting...")
    config = Config.load()
    mpv = MpvService(config.mpv_socket)
    await mpv.connect()
    app.state.mpv = mpv
    app.state.config = config
    app.state.ui_key_queue = asyncio.Queue()
    logger.info("API ready on :%d", config.api_port)
    yield
    logger.info("API shutting down...")
    await mpv.disconnect()


app = FastAPI(
    title="QaryxOS API",
    version="1.0.0",
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(playback.router, tags=["playback"])
app.include_router(ui.router,       prefix="/ui",       tags=["ui"])
app.include_router(youtube.router,  prefix="/youtube",  tags=["youtube"])
app.include_router(iptv.router,     prefix="/iptv",     tags=["iptv"])
app.include_router(ota.router,      prefix="/ota",      tags=["ota"])
app.include_router(system.router,   tags=["system"])

# ── Web UI (v1.5) ─────────────────────────────────────────────────────────
_static_dir = os.path.join(os.path.dirname(__file__), "static")
if os.path.isdir(_static_dir):
    app.mount("/static", StaticFiles(directory=_static_dir), name="static")

    @app.get("/")
    async def web_ui():
        return FileResponse(os.path.join(_static_dir, "index.html"))
