#!/usr/bin/env python3
"""Builds the shareable source ZIP of RETROTV from a commit: dist/retrotv-<version>-<commit>.zip.

    tools/make_dist.py [ref]        # default HEAD
    tools/make_dist.py --self-test

It comes from `git archive`, so only files Git tracks go in: never include/secrets.h, include/title_tags.h,
.pio, virtual environments, caches, server/media, the server's local channels.json or .git. Then the ZIP is
checked by name and path (video, audio, indexes, case models, temporary files) and, when include/secrets.h
exists, no string from it may appear in any file (the strings are never printed). On any problem the ZIP is
deleted and the script fails. Uncommitted changes are not in the ZIP.
"""

import re
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Paths that must never be shared: private, built, cached or someone else's content.
FORBIDDEN = re.compile(
    r"(^|/)(secrets\.h|title_tags\.h|\.pio/|\.venv/|venv/|__pycache__/|\.pytest_cache/|\.DS_Store|\._[^/]*|\.git/|dist/)"
    r"|(^|/)server/media/|(^|/)server/config/channels\.json$"
    r"|\.(mjpeg|aac|idx|wav|mp3|mp4|mkv|avi|mov|m4v|webm|stl|3mf|blend\d?|tmp|part|pyc)$",
    re.IGNORECASE,
)
REQUIRED = ["include/secrets.example.h", "platformio.ini", "README.md", "LICENSE"]
MIN_SECRET_LEN = 4  # shorter strings ("1", "on") would match by chance


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, check=True, capture_output=True, text=True).stdout.strip()


def secret_strings(path: Path) -> list[str]:
    """The string literals of a secrets.h (Wi-Fi names and passwords), or [] if there is none."""
    if not path.exists():
        return []
    return [s for s in re.findall(r'"([^"\\]*)"', path.read_text(encoding="utf-8", errors="replace")) if len(s) >= MIN_SECRET_LEN]


def problems(zip_path: Path, prefix: str, secrets: list[str]) -> list[str]:
    found = []
    with zipfile.ZipFile(zip_path) as z:
        names = [n for n in z.namelist() if not n.endswith("/")]
        rel = [n[len(prefix):] if n.startswith(prefix) else n for n in names]
        found += [f"forbidden path: {r}" for r in rel if FORBIDDEN.search(r)]
        found += [f"missing: {r}" for r in REQUIRED if r not in rel]
        for name, r in zip(names, rel):
            data = z.read(name)
            for i, s in enumerate(secrets, 1):
                if s.encode() in data:
                    found.append(f"string #{i} of include/secrets.h ({len(s)} characters) found in {r}")
    return found


def build(ref: str) -> int:
    if git("status", "--porcelain", "--untracked-files=no"):
        print(f"note: there are uncommitted changes; the ZIP has {ref}, not what is on disk", file=sys.stderr)
    version = re.search(r'PAUTV_VERSION = "([^"]+)"', git("show", f"{ref}:include/config.h")).group(1)
    name = f"retrotv-{version}-{git('rev-parse', '--short', ref)}"
    out = ROOT / "dist" / f"{name}.zip"
    out.parent.mkdir(exist_ok=True)
    git("archive", "--format=zip", f"--prefix={name}/", "-o", str(out), ref)
    found = problems(out, f"{name}/", secret_strings(ROOT / "include" / "secrets.h"))
    if found:
        out.unlink()
        print("ERROR, ZIP deleted:\n  " + "\n  ".join(found), file=sys.stderr)
        return 1
    with zipfile.ZipFile(out) as z:
        files = [n for n in z.namelist() if not n.endswith("/")]
    print(f"{out.relative_to(ROOT)}: {len(files)} files, {out.stat().st_size // 1024} KB, checked")
    return 0


def self_test() -> int:
    import tempfile

    with tempfile.TemporaryDirectory() as tmp:
        bad = Path(tmp) / "bad.zip"
        with zipfile.ZipFile(bad, "w") as z:
            for n in ["p/README.md", "p/LICENSE", "p/platformio.ini", "p/include/secrets.example.h",
                      "p/include/secrets.h", "p/server/media/a.mjpeg", "p/docs/x.wav", "p/.pio/build/f.bin"]:
                z.writestr(n, "x")
            z.writestr("p/docs/notes.md", "wifi: my-home-net")
        found = problems(bad, "p/", ["my-home-net"])
        assert any("secrets.h" in f and "forbidden" in f for f in found), found
        assert any("server/media/a.mjpeg" in f for f in found), found
        assert any("docs/x.wav" in f for f in found), found
        assert any(".pio/" in f for f in found), found
        assert any("string #1" in f and "docs/notes.md" in f for f in found), found
        assert not any("my-home-net" in f for f in found), "a secret was printed"
        good = Path(tmp) / "good.zip"
        with zipfile.ZipFile(good, "w") as z:
            for n in REQUIRED + ["src/main.cpp", "docs/img/intro.gif"]:
                z.writestr("p/" + n, "x")
        assert problems(good, "p/", ["my-home-net"]) == [], problems(good, "p/", [])
    print("make_dist self-test: OK")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test())
    sys.exit(build(sys.argv[1] if len(sys.argv) > 1 else "HEAD"))
