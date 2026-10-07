"""B4: the screen walk, with the simulated MCB, graded against the baseline.

    python walk_check.py --ip 192.168.137.180 --out C:\\b\\bench\\results\\x\\walk

Runs rtps_mcb_sim.py (through sim_child.py) as a child, waits until the board
streams XYTwist to it, then `hmi_ui.py --host <ip> walk <out>`.
Verdict (exit 0 PASS, 1 FAIL):
- the walk exits 0 and reports 13 screens;
- the 13 screen names equal the baseline names, in order. The baseline PNGs
  were saved without their names; the names now live beside them in
  walk/names.json (if neither that nor BENCH_HOME/baseline/walk-names.json
  exists, this run records the latter and the names are not graded);
- the static screens (STATIC below) equal the baseline PNGs pixel for pixel
  (Night theme since 2026-10-06, owner-approved; the Day originals are kept as
  walk/<stem>-day.png)
  (TS-DET-05: exact), with the top bar (y < 60: clock, link) masked, and on
  the two Settings sections the scrolling row names (MASKS below).
The other screens (diagnostics, joystick, log, skunk works, internet, firmware
update, about) show live values; they are reported, not graded.
A connection refused after the preflight passed is a FAIL (plan §6).
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import struct
import subprocess
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402
import peers  # noqa: E402

STATIC = ["00-home", "01-drive", "02-seat-functions", "03-bench",
          "08-settings-display-sound", "09-settings-joystick-driving"]
MASK_Y = 60  # rows 0..59: clock and link indicator
# Per-screen masks (x0, y0, x1, y1), exclusive ends. The two Settings sections
# show their row names (J1.., "Stick sensitivity"...) as scrolling marquees in
# x 18..147, y 399..841: four shots 0.7 s apart on e2047a4-identical firmware
# differed only there (02:11, probe in C:\b\bench\probe-scroll). Masked so the
# rest of each screen is still graded exactly. Decided unattended (revisit).
MASKS = {
    "08-settings-display-sound": [(0, 340, 150, 1010)],
    "09-settings-joystick-driving": [(0, 340, 150, 1010)],
}
EXPECTED_SCREENS = 13
# The names, committed beside the baseline PNGs; the run-time copy under
# C:enchaseline is used only when the repo has none.
NAMES_FILE = (common.BASELINE_DIR / "walk" / "names.json"
              if (common.BASELINE_DIR / "walk" / "names.json").exists()
              else common.LOCAL_BASELINE_DIR / "walk-names.json")


def read_png(path: pathlib.Path) -> tuple[int, int, bytes]:
    """(width, height, RGB bytes) of an 8-bit RGB, non-interlaced PNG."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")
    pos, idat, w, h = 8, b"", 0, 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if (depth, ctype, interlace) != (8, 2, 0):
                raise ValueError(f"{path}: only 8-bit RGB non-interlaced is supported")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride, bpp = w * 3, 3
    out = bytearray(stride * h)
    prev = bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in (range(stride) if f else ()):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 0xFF
            elif f == 2:
                line[x] = (line[x] + b) & 0xFF
            elif f == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 0xFF
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, bytes(out)


def compare_png(got: pathlib.Path, want: pathlib.Path, mask_y: int = MASK_Y,
                rects: list[tuple[int, int, int, int]] | None = None) -> dict:
    gw, gh, g = read_png(got)
    ww, wh, w = read_png(want)
    if (gw, gh) != (ww, wh):
        return {"equal": False, "detail": f"size {gw}x{gh} != {ww}x{wh}"}
    if rects:
        g, w = bytearray(g), bytearray(w)
        for x0, y0, x1, y1 in rects:
            blank = bytes(3 * (min(x1, gw) - x0))
            for y in range(y0, min(y1, gh)):
                a = (y * gw + x0) * 3
                g[a:a + len(blank)] = blank
                w[a:a + len(blank)] = blank
    start = mask_y * gw * 3
    if g[start:] == w[start:]:
        return {"equal": True, "detail": f"identical below y={mask_y}" + (" outside masks" if rects else "")}
    diff, x0, y0, x1, y1 = 0, gw, gh, -1, -1
    for i in range(start, len(g), 3):
        if g[i:i + 3] != w[i:i + 3]:
            diff += 1
            px = i // 3
            x, y = px % gw, px // gw
            x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
    return {"equal": False, "detail": f"{diff} pixels differ in box ({x0},{y0})-({x1},{y1})"}


def run_walk(ip: str, out: pathlib.Path, tree: pathlib.Path) -> dict:
    cmd = [common.python(), str(tree / "scripts" / "hmi_ui.py"), "--host", ip, "walk", str(out)]
    common.log("walk: " + " ".join(cmd))
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=900, cwd=str(tree),
                       env=common.child_env(HMI_HOST=ip))
    screens = []
    for line in r.stdout.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[1].endswith(".png"):
            screens.append({"name": parts[0], "png": parts[1], "stem": pathlib.Path(parts[1]).stem})
    return {"exit": r.returncode, "screens": screens, "stderr_tail": r.stderr[-800:]}


def grade(walk: dict) -> dict:
    problems, notes = [], []
    screens = walk["screens"]
    if walk["exit"] != 0:
        problems.append(f"hmi_ui.py walk exit {walk['exit']}: {walk['stderr_tail'][-300:]}")
    if len(screens) != EXPECTED_SCREENS:
        problems.append(f"{len(screens)} screens, expected {EXPECTED_SCREENS}")
    names = [s["name"] for s in screens]
    names_state = "COMPARED"
    if NAMES_FILE.exists():
        want = json.loads(NAMES_FILE.read_text(encoding="utf-8"))["names"]
        if names != want:
            problems.append(f"screen names {names} != baseline {want}")
    elif len(screens) == EXPECTED_SCREENS and walk["exit"] == 0:
        common.write_json(NAMES_FILE, {"names": names, "recorded": common.now_iso(),
                                       "note": "recorded by walk_check.py: baseline PNGs had no names"})
        names_state = "RECORDED"
        notes.append(f"no names baseline: recorded {NAMES_FILE}; names not graded this run")
    else:
        names_state = "NOT_GRADED"
    pixels, ungraded = {}, []
    by_stem = {s["stem"]: s for s in screens}
    for stem in STATIC:
        s = by_stem.get(stem)
        base = common.BASELINE_DIR / "walk" / f"{stem}.png"
        if s is None:
            problems.append(f"static screen {stem} not captured")
            continue
        res = compare_png(pathlib.Path(s["png"]), base, rects=MASKS.get(stem))
        pixels[stem] = res
        if not res["equal"]:
            problems.append(f"{stem}: {res['detail']}")
    for s in screens:
        if s["stem"] not in STATIC:
            ungraded.append(f"{s['stem']} ({s['name']})")
    return {"verdict": "PASS" if not problems else "FAIL", "names": names,
            "names_check": names_state, "pixels": pixels, "ungraded": ungraded,
            "problems": problems, "notes": notes}


def walk_check(ip: str, out: pathlib.Path, tree: pathlib.Path) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    with peers.SimChild(ip, tree, out / "sim.log") as sim:
        if not sim.wait_ready():
            return {"verdict": "FAIL", "problems": ["the simulated MCB got no XYTwist from the board in 45 s"]}
        walk = run_walk(ip, out, tree)
    report = grade(walk)
    report["walk_exit"] = walk["exit"]
    return report


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--ip", required=True)
    p.add_argument("--out", type=pathlib.Path, default=common.BENCH_HOME / f"walk-{common.stamp()}")
    p.add_argument("--tree", type=pathlib.Path, default=common.REPO)
    a = p.parse_args()
    strays = common.stray_peers()
    if strays:
        print(json.dumps({"verdict": "INVALID", "processes": strays}, indent=2))
        return 2
    import lease
    with lease.held():
        report = walk_check(a.ip, a.out, a.tree)
    print(json.dumps(report, indent=2))
    return 0 if report["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
