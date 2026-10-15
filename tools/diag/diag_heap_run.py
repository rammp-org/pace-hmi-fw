"""DIAGNOSTIC (dev_ai_diag_heaptrace, owner-approved 2026-10-13; never merged).

With the heaptrace image on the board (already flashed and booted): starts the MCB sim so
Read ADC publishes XYTwist, captures the serial console (no reset) for --seconds, counts the
XYTwist the sim received, stops the sim, runs the self test (mem.alloc_adc), then resolves
every AGDIAG address with riscv32-esp-elf-addr2line against the image's ELF.

  python diag_heap_run.py --tree C:/w/diag --ip 192.168.137.x --port COM9 \
      --elf C:/b/diag_heaptrace_bench/rammp-hmi-p4.elf --seconds 120
  python diag_heap_run.py --resolve-only <out dir> --elf ...
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys
import time

import os
import shutil

# Windows: C:\Espressif\tools; Linux: ~/.espressif/tools (or the PATH of an activated IDF).
ADDR2LINE = (r"C:\Espressif\tools\riscv32-esp-elf" if os.name == "nt"
             else str(pathlib.Path.home() / ".espressif" / "tools" / "riscv32-esp-elf"))
EXE = "riscv32-esp-elf-addr2line" + (".exe" if os.name == "nt" else "")


def find_addr2line() -> str:
    on_path = shutil.which(EXE)
    if on_path:
        return on_path
    hits = sorted(pathlib.Path(ADDR2LINE).rglob(EXE))
    if not hits:
        raise SystemExit("no riscv32-esp-elf-addr2line under " + ADDR2LINE)
    return str(hits[-1])


def resolve(out: pathlib.Path, elf: pathlib.Path) -> None:
    text = (out / "serial.log").read_text(encoding="utf-8", errors="replace")
    lines = [ln for ln in text.splitlines() if "AGDIAG" in ln]
    addrs = sorted({int(a, 16) for ln in lines for a in re.findall(r"0x([0-9a-f]{8})", ln)
                    if not ln.split("AGDIAG", 1)[1].lstrip().startswith("t_")})
    addrs = [a for a in addrs if 0x40000000 <= a < 0x44000000 or 0x4ff00000 <= a < 0x4ffc0000]
    tool = find_addr2line()
    names: dict[int, str] = {}
    for i in range(0, len(addrs), 200):
        chunk = addrs[i:i + 200]
        # a return address points after the call: look up ra - 2 (the call, RVC or not)
        r = subprocess.run([tool, "-pfiaC", "-e", str(elf)] + [f"0x{a - 2:08x}" for a in chunk],
                           capture_output=True, text=True, timeout=120)
        blocks = re.split(r"(?m)^(?=0x[0-9a-f]{8}: )", r.stdout)
        blocks = [b.strip() for b in blocks if b.strip()]
        for a, b in zip(chunk, blocks):
            names[a] = b.split(": ", 1)[1] if ": " in b else b
    report = []
    for ln in lines:
        body = ln.split("AGDIAG", 1)[1].strip()
        report.append("AGDIAG " + body)
        if body.startswith("t_"):
            continue
        for part in ("fp", "scan"):
            m = re.search(part + r"=([0-9a-fx,]*)", body)
            if not m or not m.group(1):
                continue
            report.append(f"  {part}:")
            for a in m.group(1).split(","):
                v = int(a, 16)
                report.append(f"    {a}  {names.get(v, '?')}")
    (out / "resolved.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    print(f"RESOLVED {out / 'resolved.txt'} ({len(addrs)} addresses)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", type=pathlib.Path)
    ap.add_argument("--ip")
    ap.add_argument("--port")
    ap.add_argument("--elf", type=pathlib.Path, required=True)
    ap.add_argument("--seconds", type=float, default=120.0)
    ap.add_argument("--out", type=pathlib.Path,
                    default=pathlib.Path(r"C:\b\bench\diag_heap") if os.name == "nt"
                    else pathlib.Path.home() / "bench" / "diag_heap")
    ap.add_argument("--resolve-only", type=pathlib.Path)
    a = ap.parse_args()
    if a.resolve_only:
        resolve(a.resolve_only, a.elf)
        return 0
    sys.path.insert(0, str(a.tree / "tools" / "bench"))
    import common
    import compare_selftest
    import hazard_rig
    import lease
    import peers
    out = a.out / f"heap-{common.stamp()}"
    out.mkdir(parents=True, exist_ok=True)
    ok, proof = hazard_rig.sole_sim_proof(a.ip, lambda: hazard_rig.rtps_sweep(a.tree))
    if not ok:
        print(json.dumps({"verdict": "INVALID", "reason": proof}))
        return 2
    info: dict = {"sole_sim_proof": proof}
    with lease.held("diag_heaptrace"):
        watch = hazard_rig.SerialWatch(a.port, a.seconds + 60.0)
        t0 = time.monotonic()
        sim = peers.SimChild(a.ip, a.tree, out / "sim.log", event_log=out / "sim-events.jsonl")
        try:
            info["sim_ready"] = sim.wait_ready(45.0)
            info["sim_ready_s"] = round(time.monotonic() - t0, 1)
            info["xytwist_at_ready"] = sim.xy_count()
            time.sleep(max(0.0, a.seconds - (time.monotonic() - t0)))
            info["xytwist_at_end"] = sim.xy_count()
            info["matched_s"] = round(time.monotonic() - t0, 1)
        finally:
            sim.stop()
            text = watch.stop()
        (out / "serial.log").write_text(text, encoding="utf-8")
        verdict, _ = compare_selftest.run_selftest(a.ip, out / "selftest.json", a.tree)
        info["selftest_verdict"] = verdict
        try:
            res = json.loads((out / "selftest.json").read_text(encoding="utf-8"))["results"]
            info["selftest_alloc"] = [r for r in res if r["name"].startswith("mem.")]
        except (OSError, ValueError, KeyError) as e:
            info["selftest_error"] = str(e)
    common.write_json(out / "info.json", info)
    print(json.dumps(info, indent=1))
    resolve(out, a.elf)
    print(f"OUT {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
