#!/usr/bin/env python3
"""
QaryxOS OTA Updater CLI
Used by qaryxos-update.service systemd unit.
Usage: qaryxos-ota check | update | status
"""

import hashlib
import json
import os
import subprocess
import sys
import urllib.request
import urllib.error

GITHUB_REPO   = "stepan163s/QaryxOS"
VERSIONS_FILE = "/etc/qaryxos/versions.json"
API_BASE      = "http://localhost:8080"


def read_versions() -> dict:
    try:
        with open(VERSIONS_FILE) as f:
            return json.load(f)
    except Exception:
        return {"os": "0.0.0"}


def version_tuple(v: str) -> tuple:
    try:
        return tuple(int(x) for x in v.lstrip("v").split(".")[:3])
    except ValueError:
        return (0, 0, 0)


def fetch_latest_release() -> dict:
    url = f"https://api.github.com/repos/{GITHUB_REPO}/releases/latest"
    req = urllib.request.Request(url, headers={"Accept": "application/vnd.github+json"})
    with urllib.request.urlopen(req, timeout=10) as resp:
        return json.load(resp)


def cmd_check() -> None:
    current = read_versions().get("os", "0.0.0")
    try:
        release = fetch_latest_release()
        latest = release.get("tag_name", "").lstrip("v")
        available = version_tuple(latest) > version_tuple(current)
        print(json.dumps({
            "current": current,
            "latest": latest,
            "update_available": available,
        }, indent=2))
        if available:
            sys.exit(0)
        else:
            sys.exit(0)
    except Exception as e:
        print(json.dumps({"error": str(e)}))
        sys.exit(1)


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def download(url: str, dest: str) -> None:
    tmp = dest + ".tmp"
    print(f"  Downloading {os.path.basename(dest)}...")
    urllib.request.urlretrieve(url, tmp)
    os.replace(tmp, dest)


def cmd_update() -> None:
    current = read_versions().get("os", "0.0.0")
    print(f"Current version: {current}")

    try:
        release = fetch_latest_release()
    except Exception as e:
        print(f"ERROR: Cannot fetch release: {e}")
        sys.exit(1)

    latest = release.get("tag_name", "").lstrip("v")
    if not (version_tuple(latest) > version_tuple(current)):
        print(f"Already up to date: {current}")
        sys.exit(0)

    print(f"Updating {current} → {latest}")
    assets = {a["name"]: a for a in release.get("assets", [])}

    # Download checksums
    checksums: dict[str, str] = {}
    if "SHA256SUMS" in assets:
        tmp = "/tmp/qaryxos_SHA256SUMS"
        download(assets["SHA256SUMS"]["browser_download_url"], tmp)
        with open(tmp) as f:
            for line in f:
                parts = line.split(None, 1)
                if len(parts) == 2:
                    checksums[parts[1].strip()] = parts[0].strip()

    targets = {
        "qaryxos-api.tar.gz":        "/usr/local/lib/qaryxos-api",
        "qaryxos-mediashell.tar.gz": "/usr/local/lib/qaryxos-mediashell",
        "yt-dlp_linux_aarch64":      "/usr/local/bin/yt-dlp",
    }

    for asset_name, install_path in targets.items():
        if asset_name not in assets:
            continue

        tmp_path = f"/tmp/{asset_name}"
        download(assets[asset_name]["browser_download_url"], tmp_path)

        # Verify SHA256
        expected = checksums.get(asset_name)
        if expected:
            actual = sha256_file(tmp_path)
            if actual != expected:
                print(f"  ERROR: SHA256 mismatch for {asset_name}!")
                print(f"  Expected: {expected}")
                print(f"  Got:      {actual}")
                os.remove(tmp_path)
                sys.exit(1)
            print(f"  SHA256 OK: {asset_name}")

        # Install
        print(f"  Installing {asset_name}...")
        if asset_name.endswith(".tar.gz"):
            os.makedirs(install_path, exist_ok=True)
            subprocess.run(
                ["tar", "-xzf", tmp_path, "-C", install_path, "--strip-components=1"],
                check=True,
            )
        else:
            backup = install_path + ".bak"
            if os.path.exists(install_path):
                os.replace(install_path, backup)
            os.makedirs(os.path.dirname(install_path), exist_ok=True)
            os.replace(tmp_path, install_path)
            os.chmod(install_path, 0o755)

        if os.path.exists(tmp_path):
            os.remove(tmp_path)

    # Write new version
    versions = read_versions()
    versions.update({"os": latest, "api": latest, "mediashell": latest})
    with open(VERSIONS_FILE, "w") as f:
        json.dump(versions, f, indent=2)

    print(f"\nUpdate complete: {latest}")
    print("Restarting services...")
    for svc in ["qaryxos-api", "qaryxos-mediashell"]:
        subprocess.run(["systemctl", "restart", svc], check=False)
    print("Done.")


def main() -> None:
    cmd = sys.argv[1] if len(sys.argv) > 1 else "check"
    if cmd == "check":
        cmd_check()
    elif cmd == "update":
        cmd_update()
    else:
        print(f"Usage: {sys.argv[0]} check|update")
        sys.exit(1)


if __name__ == "__main__":
    main()
