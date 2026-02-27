"""OTA update endpoints — issue #18"""

import asyncio
import hashlib
import json
import logging
import os
import subprocess
import tempfile
from fastapi import APIRouter, Request, HTTPException, BackgroundTasks
import httpx

logger = logging.getLogger("qaryxos.ota")
router = APIRouter()

GITHUB_REPO = "stepan163s/QaryxOS"
VERSIONS_FILE = "/etc/qaryxos/versions.json"

# Global update status
_update_status = {"running": False, "progress": "", "error": None, "last_updated": None}


def _read_versions() -> dict:
    try:
        with open(VERSIONS_FILE) as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {"os": "0.0.0", "api": "0.0.0", "mediashell": "0.0.0", "yt-dlp": "unknown"}


def _version_tuple(v: str) -> tuple:
    try:
        return tuple(int(x) for x in v.split(".")[:3])
    except ValueError:
        return (0, 0, 0)


async def _fetch_latest_release() -> dict:
    url = f"https://api.github.com/repos/{GITHUB_REPO}/releases/latest"
    async with httpx.AsyncClient(timeout=10) as client:
        resp = await client.get(url, headers={"Accept": "application/vnd.github+json"})
        resp.raise_for_status()
        return resp.json()


async def _download_and_verify(url: str, expected_sha256: str, dest: str) -> None:
    async with httpx.AsyncClient(timeout=120, follow_redirects=True) as client:
        resp = await client.get(url)
        resp.raise_for_status()
        content = resp.content

    actual = hashlib.sha256(content).hexdigest()
    if actual != expected_sha256:
        raise ValueError(f"SHA256 mismatch: expected {expected_sha256}, got {actual}")

    # Write atomically via temp file
    tmp = dest + ".tmp"
    with open(tmp, "wb") as f:
        f.write(content)
    os.replace(tmp, dest)
    os.chmod(dest, 0o755)


async def _run_update(release: dict) -> None:
    global _update_status
    _update_status = {"running": True, "progress": "Starting", "error": None, "last_updated": None}

    try:
        assets = {a["name"]: a for a in release.get("assets", [])}
        tag = release.get("tag_name", "unknown")

        # Expected assets and their install paths
        update_targets = {
            "qaryxos-api.tar.gz":        "/usr/local/lib/qaryxos-api",
            "qaryxos-mediashell.tar.gz": "/usr/local/lib/qaryxos-mediashell",
            "yt-dlp_linux_aarch64":      "/usr/local/bin/yt-dlp",
        }

        # Checksums asset
        checksums: dict[str, str] = {}
        if "SHA256SUMS" in assets:
            _update_status["progress"] = "Fetching checksums"
            async with httpx.AsyncClient(timeout=10) as client:
                resp = await client.get(assets["SHA256SUMS"]["browser_download_url"])
                for line in resp.text.splitlines():
                    parts = line.split(None, 1)
                    if len(parts) == 2:
                        checksums[parts[1].strip()] = parts[0].strip()

        for asset_name, install_path in update_targets.items():
            if asset_name not in assets:
                continue

            _update_status["progress"] = f"Downloading {asset_name}"
            asset = assets[asset_name]
            sha256 = checksums.get(asset_name, "")

            with tempfile.NamedTemporaryFile(delete=False, suffix=asset_name) as tmp:
                tmp_path = tmp.name

            await _download_and_verify(asset["browser_download_url"], sha256, tmp_path)

            # Install
            _update_status["progress"] = f"Installing {asset_name}"
            if asset_name.endswith(".tar.gz"):
                os.makedirs(install_path, exist_ok=True)
                subprocess.run(["tar", "-xzf", tmp_path, "-C", install_path, "--strip-components=1"],
                               check=True)
            else:
                os.makedirs(os.path.dirname(install_path), exist_ok=True)
                os.replace(tmp_path, install_path)
                os.chmod(install_path, 0o755)

            if os.path.exists(tmp_path):
                os.remove(tmp_path)

        # Update version file
        versions = _read_versions()
        versions["os"] = tag
        versions["api"] = tag
        versions["mediashell"] = tag
        with open(VERSIONS_FILE, "w") as f:
            json.dump(versions, f, indent=2)

        # Restart services
        _update_status["progress"] = "Restarting services"
        for svc in ["qaryxos-api", "qaryxos-mediashell"]:
            subprocess.run(["systemctl", "restart", svc], check=False)

        _update_status.update({"running": False, "progress": "Done", "last_updated": tag})
        logger.info("OTA update complete: %s", tag)

    except Exception as e:
        _update_status.update({"running": False, "progress": "Failed", "error": str(e)})
        logger.error("OTA update failed: %s", e)


@router.get("/check")
async def check_update():
    """Check if an update is available."""
    current = _read_versions()
    try:
        release = await _fetch_latest_release()
        latest = release.get("tag_name", "").lstrip("v")
        available = _version_tuple(latest) > _version_tuple(current.get("os", "0.0.0"))
        return {
            "current": current.get("os", "0.0.0"),
            "latest": latest,
            "update_available": available,
            "release_notes": release.get("body", "")[:500],
        }
    except Exception as e:
        raise HTTPException(503, f"Cannot reach GitHub: {e}")


@router.post("/update")
async def start_update(request: Request, bg: BackgroundTasks):
    """Start OTA update in background."""
    if _update_status["running"]:
        raise HTTPException(409, "Update already in progress")
    try:
        release = await _fetch_latest_release()
    except Exception as e:
        raise HTTPException(503, f"Cannot fetch release: {e}")

    bg.add_task(_run_update, release)
    return {"ok": True, "message": "Update started"}


@router.get("/status")
async def get_status():
    return _update_status
