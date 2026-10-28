#!/usr/bin/env python3
"""Real HTTP/builder integration checks; isolate HOME and navigation settings."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def request(port, path, method="GET", trusted=False):
    headers = {"X-Navalm-Book": "1"} if trusted else {}
    req = urllib.request.Request(f"http://127.0.0.1:{port}{path}",
                                 method=method, headers=headers)
    try:
        response = urllib.request.urlopen(req, timeout=10)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        return response.status, response.headers, response.read()


def api(port, path="/api/book", method="GET", trusted=False, expected=200):
    code, _, body = request(port, path, method, trusted)
    assert code == expected, (code, body)
    return json.loads(body)


def start(port, **options):
    return api(port, "/api/book?" + urllib.parse.urlencode(options), "POST", True)


def finish(port):
    deadline = time.monotonic() + 120
    progress = []
    while time.monotonic() < deadline:
        state = api(port)
        progress.append(state["percent"])
        if state["state"] != "running":
            assert progress == sorted(progress), progress
            return state
        time.sleep(0.1)
    raise AssertionError("Builder did not finish")


def server(binary, home, work):
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    env = dict(os.environ, HOME=str(home), NAVALMRC=str(home / ".navalmrc"))
    proc = subprocess.Popen([str(binary), "--ephemeris", str(ROOT / "ephem/de440s.nav"),
                             "-d", "--port", str(port)], cwd=work, env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        for _ in range(100):
            if proc.poll() is not None:
                raise AssertionError(proc.stderr.read().decode())
            try:
                api(port)
                return proc, port
            except OSError:
                time.sleep(0.05)
        raise AssertionError("Server did not start")
    except BaseException:
        proc.terminate()
        proc.wait(timeout=10)
        raise


def main():
    with tempfile.TemporaryDirectory(prefix="navalm-web-book-") as temp:
        work = Path(temp)
        home = work / "home with spaces é"
        home.mkdir()
        sentinel = home / "NAVALM-2026.pdf"
        sentinel.write_bytes(b"keep my existing book")
        proc, port = server(ROOT / "navalm", home, work)
        try:
            state = api(port)
            assert state["available"] == 1 and state["engine"] == "precise", state
            assert b"Annual book" in request(port, "/")[2]
            api(port, "/api/book?year=2026", "POST", expected=403)
            assert api(port, "/api/book?year=2026")["state"] == "idle"
            for query in ("year=2026.5", "year=2026;touch+x", "year=1599", "year=2201",
                          "year=2026&format=exe", "year=2026&shade=yes",
                          "year=2026&reduction=bad",
                          "year=2026&sun_moon=0&planets=0&stars=0&events=0&increments=0&corrections=0&reduction=none"):
                api(port, "/api/book?" + query, "POST", True, expected=400)
            assert len(list(home.iterdir())) == 1, "Rejected requests created files"

            state = start(port, year=2026, shade=1)
            assert state["state"] == "running", state
            api(port, "/api/book?year=2026", "POST", True, expected=409)
            before = time.monotonic()
            assert request(port, "/api/almanac?body=sun&y=2026&mo=10&d=27")[0] == 200
            assert time.monotonic() - before < 3, "Web calculations blocked on export"
            done = finish(port)
            assert done["state"] == "done", done
            saved = Path(done["path"])
            assert saved.parent.parent == home and saved.name == "NAVALM-2026.pdf", saved
            assert saved.read_bytes().startswith(b"%PDF-")
            assert sentinel.read_bytes() == b"keep my existing book"
            code, headers, data = request(port, "/book/download?id=" + done["id"])
            assert code == 200 and headers["Content-Type"] == "application/pdf"
            assert int(headers["Content-Length"]) == len(data)
            assert hashlib.sha256(data).digest() == hashlib.sha256(saved.read_bytes()).digest()
            assert request(port, "/book/download?id=../../etc/passwd")[0] == 404
            api(port, "/api/book?year=bad", "POST", True, expected=400)
            assert api(port)["id"] == done["id"], "Invalid request lost completed download"

            # Whole-year ephemeris errors surface politely and the next job works.
            start(port, year=1600)
            failed = finish(port)
            assert failed["state"] == "error" and "whole year" in failed["error"], failed
            assert saved.is_file()
            start(port, year=1600, format="latex", sun_moon=0, planets=0, stars=0,
                  events=0, increments=0, corrections=0, reduction="ageton", shade=1)
            latex = finish(port)
            assert latex["state"] == "done", latex
            text = Path(latex["path"]).read_text()
            assert "\\documentclass" in text and "Ageton" in text
            code, headers, data = request(port, "/book/download?id=" + latex["id"])
            assert code == 200 and headers["Content-Type"] == "application/x-tex"
            assert data == Path(latex["path"]).read_bytes()
            assert not (home / ".navalmrc").exists() and not (home / ".naval.save").exists()
            print("Full 2026 PDF, Ageton LaTeX, progress/reconnect, downloads, concurrent almanac,"
                  " input guards, failure recovery and existing-file preservation passed.")
        finally:
            proc.terminate()
            proc.wait(timeout=10)

        isolated = work / "isolated"
        isolated.mkdir()
        shutil.copy2(ROOT / "navalm", isolated / "navalm")
        proc, port = server(isolated / "navalm", home, work)
        try:
            assert api(port)["available"] == 0
            api(port, "/api/book?year=2026", "POST", True, expected=503)
            print("Missing companion builder is reported without breaking the web interface.")
        finally:
            proc.terminate()
            proc.wait(timeout=10)


if __name__ == "__main__":
    main()
