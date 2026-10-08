"""Check a firmware .bin against GitHub's releases, and tell the Tab5.

    python scripts/fw_verify.py                       # build/rammp-hmi-p4.bin, board found on its own
    python scripts/fw_verify.py --bin precompiled/rammp-hmi-p4.bin --port COM6
    python scripts/fw_verify.py --no-write            # only say what it is

The Tab5 hashes its own firmware at boot (components/ota/src/fw_info.cpp), but it cannot ask
GitHub whether that hash is a published release. This does, on the PC:

1. SHA-256 of the .bin -- the same number `sha256sum` prints, and the digest
   GitHub shows beside rammp-hmi-p4.bin on a release page.
2. Looks for that digest among the repo's releases (public API, no login).
3. If a release (or pre-release) has it, adds a line to /storage/fwinfo.txt on
   the Tab5:  <sha256>  <tag>  <release|prerelease>  <checked, UTC>
   The About screen turns green when the board finds its OWN hash in that
   file; anything else is red. Nothing is written for a .bin that matches no
   release -- there is nothing true to record.

Writing the file does not touch the rest of the board's storage (settings,
the joined WiFi network, the stick calibration): the storage partition is read
back over USB, the file is added with littlefs-python -- the library ESP-IDF's
LittleFS component builds its own images with -- every other file is checked
to be byte-for-byte unchanged, and only the 4 KB sectors that differ are
written. The read-back is kept under build/fw_verify/ as a backup.

Needs: pip install esptool littlefs-python
Exit status: 0 = a published release, 1 = not, 2 = could not tell (GitHub or
the board unreachable, storage unreadable).
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import pathlib
import struct
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request

REPO = "rammp-org/pace-hmi-fw"
ASSET = "rammp-hmi-p4.bin"
RECORD = "/fwinfo.txt"  # /storage/fwinfo.txt on the board; see components/ota/include/fw_info.hpp
KEEP_LINES = 8          # one per release image vouched for; the oldest go first
SECTOR = 4096
ROOT = pathlib.Path(__file__).resolve().parent.parent

# The board's storage as esp_littlefs formats it (read off the superblock of a
# real board: disk version 2.1, 4096-byte blocks, 255-byte names; sdkconfig's
# CONFIG_LITTLEFS_* for the rest). Mounting with a smaller name_max than the
# superblock's is refused, so these must not drift below it.
LFS_CONFIG = dict(block_size=SECTOR, name_max=255, read_size=128, prog_size=128,
                  lookahead_size=128, cache_size=512, block_cycles=512,
                  disk_version=0x00020001)


# ------------------------------------------------------------------- the .bin

def image_info(path: pathlib.Path) -> tuple[str, str]:
    """(sha256 hex, version) of an ESP-IDF app image.

    The version is esp_app_desc_t.version, which sits right after the image
    header and the first segment's header (offset 32 + 16).
    """
    data = path.read_bytes()
    magic, = struct.unpack_from("<I", data, 32)
    if data[0] != 0xE9 or magic != 0xABCD5432:
        raise SystemExit(f"{path} is not an ESP-IDF app image")
    version = data[48:80].split(b"\0", 1)[0].decode(errors="replace")
    return hashlib.sha256(data).hexdigest(), version


# ------------------------------------------------------------------- GitHub

def find_release(sha256: str) -> tuple[str, bool] | None:
    """(tag, prerelease) of the release whose rammp-hmi-p4.bin has this digest."""
    page = 1
    while True:
        url = f"https://api.github.com/repos/{REPO}/releases?per_page=100&page={page}"
        request = urllib.request.Request(url, headers={"Accept": "application/vnd.github+json",
                                                       "User-Agent": "pace-hmi-fw fw_verify"})
        with urllib.request.urlopen(request, timeout=20) as response:
            releases = json.load(response)
        for release in releases:
            for asset in release.get("assets", []):
                if asset.get("name") == ASSET and asset.get("digest") == f"sha256:{sha256}":
                    return release["tag_name"], bool(release.get("prerelease"))
        if len(releases) < 100:
            return None
        page += 1


# -------------------------------------------------------------------- the board

def find_port() -> str:
    from serial.tools import list_ports  # pyserial, installed with esptool

    tab5s = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x303A, 0x1001)]
    if len(tab5s) != 1:
        raise SystemExit(f"found {len(tab5s)} ESP USB ports ({', '.join(tab5s) or 'none'}): "
                         "pass --port")
    return tab5s[0]


def esptool(port: str, *args: str, first: bool = False, last: bool = False) -> str:
    """One esptool run. The first resets the board into its bootloader, the
    rest find it there, and the last resets it back into the firmware."""
    cmd = [sys.executable, "-m", "esptool", "--chip", "esp32p4", "-p", port, "-b", "921600",
           "--before", "default-reset" if first else "no-reset",
           "--after", "hard-reset" if last else "no-reset", *args]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"esptool {args[0]} failed:\n{result.stdout}{result.stderr}")
    return result.stdout


def storage_partition(port: str) -> tuple[int, int, str]:
    """(offset, size) of the `storage` partition, from the board's own table,
    and the board's MAC. Read rather than assumed: the OTA layout moves it."""
    with tempfile.TemporaryDirectory() as tmp:
        table = pathlib.Path(tmp) / "pt.bin"
        out = esptool(port, "read-flash", "0x8000", "0xc00", str(table), first=True)
        data = table.read_bytes()
    mac = next((line.split(":", 1)[1].strip() for line in out.splitlines()
                if line.startswith("MAC:")), "unknown")
    for i in range(0, len(data), 32):
        entry = data[i:i + 32]
        if entry[:2] != b"\xaa\x50":
            break
        offset, size = struct.unpack_from("<II", entry, 4)
        if entry[12:28].rstrip(b"\0") == b"storage":
            return offset, size, mac
    raise SystemExit("the board's partition table has no 'storage' partition")


def add_line(image: bytearray, line: str, sha256: str) -> dict[str, bytes]:
    """Adds `line` to the record inside a storage image, in place. Returns
    every other file's contents, as they were, for the caller to check."""
    from littlefs import LittleFS

    fs = LittleFS(block_count=len(image) // SECTOR, mount=False, **LFS_CONFIG)
    fs.context.buffer = image
    fs.mount()

    def files() -> dict[str, bytes]:
        found = {}
        for root, _dirs, names in fs.walk("/"):
            for name in names:
                path = root.rstrip("/") + "/" + name
                with fs.open(path, "rb") as f:
                    found[path] = f.read()
        return found

    before = files()
    old = before.get(RECORD, b"").decode(errors="replace").splitlines()
    kept = [l for l in old if l.strip() and not l.startswith("#") and l.split()[0] != sha256]
    lines = (kept + [line])[-KEEP_LINES:]
    header = "# written by scripts/fw_verify.py: <sha256> <tag> <release|prerelease> <checked>"
    with fs.open(RECORD, "w") as f:
        f.write("\n".join([header] + lines) + "\n")

    after = files()
    others = {p: c for p, c in before.items() if p != RECORD}
    changed = [p for p, c in others.items() if after.get(p) != c]
    if changed:
        raise SystemExit(f"refusing to write: {', '.join(changed)} would change")
    fs.unmount()
    return others


def record(port: str, line: str, sha256: str) -> None:
    offset, size, mac = storage_partition(port)
    print(f"board {mac}: storage at {offset:#x}, {size // 1024} KB; reading it back (~20 s)...")
    backups = ROOT / "build" / "fw_verify"
    backups.mkdir(parents=True, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    backup = backups / f"storage-{mac.replace(':', '')}-{stamp}.bin"
    esptool(port, "read-flash", hex(offset), hex(size), str(backup))

    original = backup.read_bytes()
    image = bytearray(original)
    others = add_line(image, line, sha256)
    print(f"  kept as they were: {', '.join(sorted(others)) or '(no other files)'}")

    # Only the sectors that differ, as runs of neighbours.
    dirty = [i for i in range(0, size, SECTOR) if image[i:i + SECTOR] != original[i:i + SECTOR]]
    runs: list[list[int]] = []
    for i in dirty:
        if runs and runs[-1][1] == i:
            runs[-1][1] = i + SECTOR
        else:
            runs.append([i, i + SECTOR])
    with tempfile.TemporaryDirectory() as tmp:
        pairs = []
        for start, end in runs:
            part = pathlib.Path(tmp) / f"{start:x}.bin"
            part.write_bytes(image[start:end])
            pairs += [hex(offset + start), str(part)]
        print(f"  writing {len(dirty)} sector(s) of {size // SECTOR}; backup {backup}")
        if pairs:
            esptool(port, "write-flash", *pairs, last=True)
        else:
            esptool(port, "chip-id", last=True)
    print("  done: the board restarts, and About shows the result")


# ------------------------------------------------------------------------- main

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--bin", type=pathlib.Path, default=ROOT / "build" / ASSET,
                        help=f"the image that was flashed (default build/{ASSET})")
    parser.add_argument("--port", help="the Tab5's serial port (default: the only one)")
    parser.add_argument("--no-write", action="store_true",
                        help="only report; leave the board alone")
    args = parser.parse_args()

    sha256, version = image_info(args.bin)
    print(f"{args.bin}\n  version {version}\n  SHA-256 {sha256}")
    try:
        found = find_release(sha256)
    except (urllib.error.URLError, TimeoutError, OSError) as err:
        print(f"could not reach GitHub ({err}): nothing checked, nothing written")
        return 2
    if found is None:
        print(f"RED: not a published release -- no release of {REPO} has this {ASSET}")
        return 1

    tag, prerelease = found
    kind = "prerelease" if prerelease else "release"
    print(f"GREEN: this is the {kind.replace('pre', 'pre-')} {tag}")
    if args.no_write:
        return 0
    checked = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%MZ")
    record(args.port or find_port(), f"{sha256} {tag} {kind} {checked}", sha256)
    return 0


if __name__ == "__main__":
    sys.exit(main())
