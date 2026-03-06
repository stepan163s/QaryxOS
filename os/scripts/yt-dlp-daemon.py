#!/usr/bin/env python3
"""
QaryxOS yt-dlp daemon — persistent process to eliminate Python/yt-dlp
startup overhead (~300-500ms) on each video resolve request.

Protocol (Unix socket /tmp/qaryxos-ytdlp.sock):
  Request:  "<url>\t<format>\t<proxy>\n"
  Response: "<stream_url>\n"   (first direct URL, video stream)
            "ERROR\n"           (on failure)
"""

import sys
import os
import socket
import threading

SOCKET_PATH = "/tmp/qaryxos-ytdlp.sock"

try:
    import yt_dlp
except ImportError:
    sys.stderr.write("ytdlp-daemon: yt-dlp Python package not found.\n")
    sys.stderr.write("  Install: pip3 install yt-dlp\n")
    sys.exit(1)


def resolve(url: str, fmt: str, proxy: str) -> str | None:
    opts = {
        "format": fmt or "best",
        "quiet": True,
        "no_warnings": True,
        "extractor_args": {"youtube": {"player_client": ["android", "web"]}},
        "socket_timeout": 15,
    }
    if proxy:
        opts["proxy"] = proxy

    try:
        with yt_dlp.YoutubeDL(opts) as ydl:
            info = ydl.extract_info(url, download=False)
            if not info:
                return None
            # Single-stream URL (combined mux)
            if "url" in info:
                return info["url"]
            # Merged format: return video URL (mpv receives audio separately via --audio-file)
            rf = info.get("requested_formats")
            if rf:
                return rf[0].get("url")
    except Exception as e:
        sys.stderr.write(f"ytdlp-daemon: resolve error: {e}\n")
    return None


def handle_client(conn: socket.socket) -> None:
    try:
        data = b""
        while b"\n" not in data:
            chunk = conn.recv(4096)
            if not chunk:
                return
            data += chunk

        line = data.decode(errors="replace").strip()
        parts = line.split("\t", 2)
        if len(parts) < 1 or not parts[0]:
            conn.sendall(b"ERROR\n")
            return

        url   = parts[0]
        fmt   = parts[1] if len(parts) > 1 else "best"
        proxy = parts[2] if len(parts) > 2 else ""

        result = resolve(url, fmt, proxy)
        if result:
            conn.sendall((result + "\n").encode())
        else:
            conn.sendall(b"ERROR\n")
    except Exception as e:
        sys.stderr.write(f"ytdlp-daemon: client handler error: {e}\n")
        try:
            conn.sendall(b"ERROR\n")
        except Exception:
            pass
    finally:
        conn.close()


def main() -> None:
    try:
        os.unlink(SOCKET_PATH)
    except FileNotFoundError:
        pass

    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(SOCKET_PATH)
    os.chmod(SOCKET_PATH, 0o777)
    server.listen(10)
    sys.stderr.write(f"ytdlp-daemon: ready on {SOCKET_PATH}\n")
    sys.stderr.flush()

    while True:
        try:
            conn, _ = server.accept()
            t = threading.Thread(target=handle_client, args=(conn,), daemon=True)
            t.start()
        except KeyboardInterrupt:
            break
        except Exception as e:
            sys.stderr.write(f"ytdlp-daemon: accept error: {e}\n")


if __name__ == "__main__":
    main()
