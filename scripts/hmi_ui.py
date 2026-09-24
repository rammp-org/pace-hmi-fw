#!/usr/bin/env python3
"""See the Tab5's screen from a PC, and drive it.

The other end is main/remote_ui.cpp, behind CONFIG_HMI_REMOTE_UI. It hands out
the frame the panel is showing and injects touch, joystick and button input, so
every screen, menu row and button can be exercised and captured from here
instead of by someone looking at the display.

    python hmi_ui.py shot out.png              # the active screen
    python hmi_ui.py screen                    # what is loaded
    python hmi_ui.py tap 360 1200              # the burger key
    python hmi_ui.py key DOWN                  # hold the stick down
    python hmi_ui.py key NONE                  # let it go
    python hmi_ui.py walk renders              # every screen, captured
    python hmi_ui.py watch --fps 2             # crude video into ./watch
    python hmi_ui.py raw "SWIPE 360 900 360 300 400"

--host takes the board's address; without it the script asks the network (an
RTPS participant announces itself, so rtps_net.py's sweep finds the board) and
falls back to HMI_HOST from the environment.

Stdlib only, like its neighbours: zlib and a short struct header are enough to
write a PNG, so nothing here needs Pillow.

It cannot drive the chair -- injected stick directions only move the UI's
focus, never the stick values the MCB receives -- but it can press anything on
screen, the seat and actuator jogs included. Bench use only.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import socket
import struct
import sys
import time
import zlib

PORT = 3333
TIMEOUT_S = 20.0

# The burger key, centred in the bottom 162 px of a 720x1280 panel, and the
# seven menu rows: 131 px each, starting at the top of the 921 px body.
MENU_KEY = (360, 1198)
BODY_TOP = 195
ROW_HEIGHT = 921 // 8  # eight rows divide the 921 px body
MENU_ROWS = [
    "Drive",
    "Seat Functions",
    "Bench",
    "Diagnostics",
    "Joystick",
    "Log",
    "Skunk Works",
    "UI Settings",
]


def row_point(index: int) -> tuple[int, int]:
    """The middle of menu row `index` (0-based)."""
    return (360, BODY_TOP + index * ROW_HEIGHT + ROW_HEIGHT // 2)


class Hmi:
    """One connection. Commands are line in, line out; SHOT answers with a
    FRAME header and then that many raw bytes."""

    def __init__(self, host: str, port: int = PORT):
        self.sock = socket.create_connection((host, port), timeout=TIMEOUT_S)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buf = b""

    def close(self) -> None:
        self.sock.close()

    def __enter__(self) -> "Hmi":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def _line(self) -> str:
        while b"\n" not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("the board closed the connection")
            self.buf += chunk
        line, _, self.buf = self.buf.partition(b"\n")
        return line.decode("utf-8", "replace").rstrip("\r")

    def _exact(self, count: int) -> bytes:
        while len(self.buf) < count:
            chunk = self.sock.recv(min(65536, count - len(self.buf)))
            if not chunk:
                raise ConnectionError("the board closed the connection mid-frame")
            self.buf += chunk
        out, self.buf = self.buf[:count], self.buf[count:]
        return out

    def command(self, text: str) -> str:
        self.sock.sendall((text + "\n").encode("ascii"))
        return self._line()

    def shot(self, half: bool = False) -> tuple[int, int, bytes]:
        """(width, height, RGB565 little-endian) of the frame on the panel."""
        reply = self.command("SHOT 2" if half else "SHOT")
        if not reply.startswith("FRAME "):
            raise RuntimeError(reply)
        _, w, h, count = reply.split()
        return int(w), int(h), self._exact(int(count))

    def screen(self) -> str:
        return self.command("SCREEN").removeprefix("OK ")

    def tap(self, x: int, y: int) -> str:
        return self.command(f"TAP {x} {y}")

    def key(self, name: str) -> str:
        return self.command(f"KEY {name}")

    def theme(self, index: int) -> str:
        return self.command(f"THEME {index}")

    # --- the joystick, as a person would use it -------------------------------

    def nudge(self, direction: str, ms: int = 120) -> str:
        """One flick of the stick: held long enough for one keypad read, then
        let go. LVGL repeats a held key after 500 ms, so this stays well short."""
        self.key(direction.upper())
        time.sleep(ms / 1000)
        return self.key("NONE")

    def press(self, ms: int = 120) -> str:
        """A short press of the stick button: a select, on release."""
        self.command("BTN 1")
        time.sleep(ms / 1000)
        return self.command("BTN 0")

    def hold(self, ms: int = 2000) -> str:
        """A long press of the stick button: a hold, never a select."""
        return self.press(ms)

    # --- the burger menu, which is how everything is reached ----------------

    def open_menu(self) -> str:
        return self.tap(*MENU_KEY)

    def go(self, row: str) -> str:
        """Open the menu and take the row whose label starts with `row`."""
        matches = [i for i, name in enumerate(MENU_ROWS)
                   if name.lower().startswith(row.lower())]
        if not matches:
            raise SystemExit(f"no menu row starts with {row!r}; rows are {MENU_ROWS}")
        self.open_menu()
        time.sleep(0.4)  # the 280 ms slide, plus a little
        out = self.tap(*row_point(matches[0]))
        time.sleep(0.4)
        return out

    def home(self) -> str:
        """DRIVE in the band, which goes home from anywhere."""
        out = self.tap(180, 150)
        time.sleep(0.4)
        return out


# --- PNG ---------------------------------------------------------------------


def write_png(path: pathlib.Path, width: int, height: int, rgb565: bytes) -> None:
    """RGB565 little-endian -> 8-bit RGB PNG. No Pillow, so this is the whole
    encoder: an IHDR, one zlib-compressed IDAT and an IEND."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # filter type 0 (None) for this scanline
        base = y * width * 2
        for x in range(width):
            value = rgb565[base + x * 2] | (rgb565[base + x * 2 + 1] << 8)
            r, g, b = (value >> 11) & 0x1F, (value >> 5) & 0x3F, value & 0x1F
            # Replicate the high bits into the low ones so full-scale stays full
            # scale (31 -> 255, not 248).
            rows += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
                     chunk(b"IDAT", zlib.compress(bytes(rows), 6)) + chunk(b"IEND", b""))


# --- finding the board -------------------------------------------------------


def find_host() -> str:
    """HMI_HOST, or whatever on this subnet answers on the remote-UI port."""
    from_env = os.environ.get("HMI_HOST")
    if from_env:
        return from_env
    try:
        import rtps_net  # scripts/rtps_net.py, beside this file
        prefix = rtps_net.local_subnet().rsplit(".", 1)[0]
    except Exception:
        raise SystemExit("could not guess the board's address: pass --host, or set HMI_HOST")
    print(f"scanning {prefix}.0/24 for the remote UI ...", file=sys.stderr)
    for last in range(1, 255):
        candidate = f"{prefix}.{last}"
        try:
            with socket.create_connection((candidate, PORT), timeout=0.08):
                print(f"found {candidate}", file=sys.stderr)
                return candidate
        except OSError:
            continue
    raise SystemExit("nothing answered on port %d: pass --host" % PORT)


# --- commands ----------------------------------------------------------------


def capture(hmi: Hmi, path: pathlib.Path, half: bool) -> pathlib.Path:
    width, height, pixels = hmi.shot(half)
    path.parent.mkdir(parents=True, exist_ok=True)
    write_png(path, width, height, pixels)
    return path


def cmd_walk(hmi: Hmi, out: pathlib.Path, half: bool) -> int:
    """Open every destination in turn, capture it, and come back.

    This is the regression test for "every screen, button and menu still
    works": a row that opens the wrong screen, or none, shows up as the wrong
    name beside the wrong picture.
    """
    out.mkdir(parents=True, exist_ok=True)
    hmi.home()
    results = [(hmi.screen(), capture(hmi, out / "00-home.png", half))]
    for index, row in enumerate(MENU_ROWS, start=1):
        hmi.go(row)
        name = hmi.screen()
        png = capture(hmi, out / f"{index:02d}-{row.replace(' ', '-').lower()}.png", half)
        results.append((name, png))
        hmi.home()
    width = max(len(name) for name, _ in results)
    for name, png in results:
        print(f"  {name:<{width}}  {png}")
    return 0


def cmd_watch(hmi: Hmi, out: pathlib.Path, fps: float, half: bool) -> int:
    out.mkdir(parents=True, exist_ok=True)
    period = 1.0 / max(fps, 0.1)
    frame = 0
    print(f"writing into {out} at ~{fps} fps; ctrl-c to stop", file=sys.stderr)
    try:
        while True:
            started = time.time()
            capture(hmi, out / f"{frame:05d}.png", half)
            frame += 1
            time.sleep(max(0.0, period - (time.time() - started)))
    except KeyboardInterrupt:
        print(f"\n{frame} frames in {out}", file=sys.stderr)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", help="the board's address (default: HMI_HOST, else a scan)")
    parser.add_argument("--half", action="store_true",
                        help="capture at half resolution: a quarter of the bytes")
    sub = parser.add_subparsers(dest="command", required=True)

    one = sub.add_parser("shot", help="capture the active screen")
    one.add_argument("path", nargs="?", default="shot.png", type=pathlib.Path)

    sub.add_parser("screen", help="print the active screen's name")

    tap = sub.add_parser("tap", help="tap a point")
    tap.add_argument("x", type=int)
    tap.add_argument("y", type=int)

    key = sub.add_parser("key", help="hold a joystick direction (UP/DOWN/LEFT/RIGHT/NONE/ENTER)")
    key.add_argument("name")

    go = sub.add_parser("go", help="open a burger-menu destination by name")
    go.add_argument("row")

    sub.add_parser("home", help="DRIVE in the band: back to the drive screen")
    sub.add_parser("menu", help="open (or close) the burger menu")

    nudge = sub.add_parser("nudge", help="one flick of the stick (UP/DOWN/LEFT/RIGHT)")
    nudge.add_argument("direction")
    nudge.add_argument("--times", type=int, default=1)

    sub.add_parser("press", help="a short press of the stick button (select)")
    hold = sub.add_parser("hold", help="hold the stick button")
    hold.add_argument("ms", type=int, nargs="?", default=2000)

    theme = sub.add_parser("theme", help="0 = night, 1 = day")
    theme.add_argument("index", type=int)

    walk = sub.add_parser("walk", help="capture every screen the menu reaches")
    walk.add_argument("out", nargs="?", default="renders", type=pathlib.Path)

    watch = sub.add_parser("watch", help="capture continuously into a directory")
    watch.add_argument("out", nargs="?", default="watch", type=pathlib.Path)
    watch.add_argument("--fps", type=float, default=2.0)

    raw = sub.add_parser("raw", help="send one command verbatim")
    raw.add_argument("text")

    args = parser.parse_args()
    host = args.host or find_host()

    with Hmi(host) as hmi:
        if args.command == "shot":
            print(capture(hmi, args.path, args.half))
        elif args.command == "screen":
            print(hmi.screen())
        elif args.command == "tap":
            print(hmi.tap(args.x, args.y))
        elif args.command == "key":
            print(hmi.key(args.name.upper()))
        elif args.command == "go":
            print(hmi.go(args.row), hmi.screen())
        elif args.command == "home":
            print(hmi.home(), hmi.screen())
        elif args.command == "menu":
            print(hmi.open_menu())
        elif args.command == "nudge":
            for _ in range(args.times):
                print(hmi.nudge(args.direction))
                time.sleep(0.15)
        elif args.command == "press":
            print(hmi.press())
        elif args.command == "hold":
            print(hmi.hold(args.ms))
        elif args.command == "theme":
            print(hmi.theme(args.index))
        elif args.command == "walk":
            return cmd_walk(hmi, args.out, args.half)
        elif args.command == "watch":
            return cmd_watch(hmi, args.out, args.fps, args.half)
        elif args.command == "raw":
            print(hmi.command(args.text))
    return 0


if __name__ == "__main__":
    sys.exit(main())
