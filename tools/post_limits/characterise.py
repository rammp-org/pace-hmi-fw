#!/usr/bin/env python3
"""Characterise the POST "D4" limits from existing bench results (read only).

Reads every <results>/*/selftest.json (one board, many images) and the boot logs next to
them, drops runs that cannot speak for a production image (no self test, B2 failed, debug
or known-broken heap builds), and prints, for each quantity behind a D4 constant in
components/post/include/post/checks.hpp: n, min, p1, median, p99, max and which runs hold
the extremes. It also writes the same data as JSON (--json). Standard library only; it
never talks to a board.
"""
import argparse
import glob
import json
import os
import re
import statistics
import sys

# Boot-log-only directories that come from debug or known-broken heap builds.
EXCLUDE_BOOT_DIRS = (
    ("heapdbg", "heap-debug build (boots with guru meditation, no I2C scan line)"),
    ("heapmap", "heap-map debug build"),
    ("hp5-dbg", "heap-debug build"),
    ("hp1-flood", "build 58646ef with the heap bug, flood repro"),
    ("hp3-flood", "build 58646ef with the heap bug, flood repro"),
    ("hpab-unfixed", "build 380a594 without the heap fix"),
)
ANSI = re.compile(r"\x1b\[[0-9;]*m")
SCAN = re.compile(r"Found devices at addresses: \[([^\]]*)\]")
CYCLES = re.compile(r"(\d+) of (\d+) cycles")

# (self-test check, D4 constant, scale from the self test's unit to the constant's, unit)
QUANTITIES = [
    ("mem.int_min", "MEM_INT_MIN_B", 1024, "B"),
    ("mem.int_block", "MEM_INT_BLOCK_B", 1024, "B"),
    ("mem.dma_min", "MEM_DMA_MIN_B", 1, "B"),
    ("mem.psram_free", "MEM_PSRAM_FREE_B", 1024, "B"),
    ("mem.stk_adc", "STK_ADC_MIN_B", 1, "B"),
    ("mem.stk_lvgl", "STK_UI_MIN_B", 1, "B"),
    ("joy.x_cal_off", "REST_XY_MAX_MV", 1, "mV"),
    ("joy.y_cal_off", "REST_XY_MAX_MV", 1, "mV"),
    ("joy.twist_cal_off", "REST_TWIST_MAX_MV", 1, "mV"),
    ("joy.x_noise", "STILL_XY_MAX_MV", 1, "mV"),
    ("joy.y_noise", "STILL_XY_MAX_MV", 1, "mV"),
    ("joy.twist_noise", "STILL_TWIST_MAX_MV", 1, "mV"),
]


def pct(sorted_vals, q):
    """Nearest-rank percentile of a sorted list."""
    k = max(0, min(len(sorted_vals) - 1, round(q * (len(sorted_vals) - 1))))
    return sorted_vals[k]


def summarise(points):
    """points: list of (value, run). Returns stats and the runs at both extremes."""
    vals = sorted(v for v, _ in points)
    lo, hi = vals[0], vals[-1]
    return {
        "n": len(vals),
        "n_distinct": len(set(vals)),
        "min": lo,
        "p1": pct(vals, 0.01),
        "median": statistics.median(vals),
        "p99": pct(vals, 0.99),
        "max": hi,
        "min_runs": [r for v, r in points if v == lo][:4],
        "max_runs": [r for v, r in points if v == hi][:4],
    }


def load_runs(results):
    kept, excluded = [], []
    for d in sorted(glob.glob(os.path.join(results, "*"))):
        if not os.path.isdir(d):
            continue
        name = os.path.basename(d)
        st = os.path.join(d, "selftest.json")
        sm = os.path.join(d, "summary.json")
        summary = json.load(open(sm)) if os.path.exists(sm) else {}
        steps = {k: v.get("verdict") for k, v in summary.get("steps", {}).items()}
        if not os.path.exists(st):
            why = "no selftest.json"
            if steps.get("B2") == "FAIL":
                why += " (B2 FAIL)"
            elif steps.get("B2") == "NOT_RUN":
                why += " (B2 not run)"
            elif not summary:
                why += " (boot or drive data only)"
            excluded.append((name, why))
            continue
        data = json.load(open(st))
        fw = data.get("firmware", "")
        if re.search(r"dbg|debug|heapmap", fw + name, re.I):
            excluded.append((name, "debug build"))
            continue
        if steps.get("B2") == "FAIL":
            excluded.append((name, "B2 FAIL"))
            continue
        res = {r["name"]: r for r in data["results"]}
        kept.append({"run": name, "firmware": fw, "res": res})
    return kept, excluded


def quantity_points(kept, key, scale):
    pts = []
    for r in kept:
        row = r["res"].get(key)
        if row and row.get("value") is not None and row["result"] != "SKIP":
            pts.append((row["value"] * scale, r["run"]))
    return pts


def boot_scans(results, failed_dirs):
    """Boot-time scans: (kept, from debug or broken builds, from runs whose B2 failed)."""
    ok, bad, failed = {}, {}, {}
    for f in sorted(glob.glob(os.path.join(results, "*", "boot*.log"))):
        d = os.path.basename(os.path.dirname(f))
        reason = next((why for p, why in EXCLUDE_BOOT_DIRS if d.startswith(p)), None)
        text = ANSI.sub("", open(f, errors="replace").read())
        for m in SCAN.findall(text):
            addrs = tuple(sorted(int(a, 16) for a in m.replace(" ", "").split(",") if a))
            bucket = bad if reason else failed if d in failed_dirs else ok
            bucket.setdefault(addrs, []).append(d)
    return ok, bad, failed


def main():
    ap = argparse.ArgumentParser(description="Characterise the POST D4 limits from bench data")
    ap.add_argument("--results", required=True, help="bench results dir, e.g. C:/b/bench/results")
    ap.add_argument("--json", default="post_limits.json", help="JSON output path")
    args = ap.parse_args()

    kept, excluded = load_runs(args.results)
    out = {"runs_kept": len(kept), "runs_excluded": excluded,
           "firmwares": sorted({r["firmware"] for r in kept}), "quantities": {}}
    print(f"selftest runs kept: {len(kept)}, distinct firmware strings: {len(out['firmwares'])}")
    print(f"directories excluded: {len(excluded)}")
    print(f"{'check':<19}{'constant':<19}{'n':>3}{'dist':>5}{'min':>9}{'p1':>9}{'median':>9}"
          f"{'p99':>9}{'max':>9}  unit  min run / max run")
    for key, const, scale, unit in QUANTITIES:
        s = summarise(quantity_points(kept, key, scale))
        s.update(constant=const, unit=unit)
        out["quantities"][key] = s
        print(f"{key:<19}{const:<19}{s['n']:>3}{s['n_distinct']:>5}{s['min']:>9}{s['p1']:>9}"
              f"{s['median']:>9}{s['p99']:>9}{s['max']:>9}  {unit:<4}  "
              f"{s['min_runs'][0][:26]} / {s['max_runs'][0][:26]}")

    # Window length and ADC validity: parsed from joy.valid's "N of M cycles", and time.adc_*.
    cyc, perm = [], []
    for r in kept:
        row = r["res"].get("joy.valid")
        m = CYCLES.search(row["detail"]) if row else None
        if m:
            good, total = int(m.group(1)), int(m.group(2))
            cyc.append((total, r["run"]))
            perm.append((good * 1000 // total, r["run"]))
    for key, label, pts in (("window.cycles", "cycles per window", cyc),
                            ("adc.valid_permille", "ADC valid permille", perm),
                            ("time.adc_avg_us", "ADC cycle us",
                             quantity_points(kept, "time.adc_avg", 1)),
                            ("time.adc_max_us", "slowest cycle us",
                             quantity_points(kept, "time.adc_max", 1))):
        s = summarise(pts)
        s["label"] = label
        out["quantities"][key] = s
        print(f"{key:<19}{label:<19}{s['n']:>3}{s['n_distinct']:>5}{s['min']:>9}{s['p1']:>9}"
              f"{s['median']:>9}{s['p99']:>9}{s['max']:>9}")
    avg = out["quantities"]["time.adc_avg_us"]
    out["window_seconds"] = {}
    for n in (25, 30):
        lo_s, hi_s = n * avg["min"] / 1e6, n * avg["max"] / 1e6
        out["window_seconds"][str(n)] = [lo_s, hi_s]
        print(f"{n} cycles last {lo_s:.2f} s (fastest run) to {hi_s:.2f} s (slowest run)")

    # I2C: the self test's device list and every boot-time scan.
    devs = {}
    for r in kept:
        row = r["res"].get("i2c.devices")
        if row:
            devs.setdefault(row["detail"], []).append(r["run"])
    print("self-test i2c.devices:", {k: len(v) for k, v in devs.items()})
    failed_dirs = {n for n, w in excluded if "B2 FAIL" in w}
    ok, bad, failed = boot_scans(args.results, failed_dirs)

    def fmt(a):
        return "[" + ",".join(f"0x{x:02x}" for x in a) + "]"

    print("boot scans, kept dirs:")
    for a, ds in sorted(ok.items(), key=lambda kv: -len(kv[1])):
        print(f"  {len(ds):>4}  {fmt(a)}  e.g. {ds[0]}")
    print("boot scans, excluded dirs:")
    for a, ds in sorted(bad.items(), key=lambda kv: -len(kv[1])):
        print(f"  {len(ds):>4}  {fmt(a)}  e.g. {ds[0]}")
    out["i2c"] = {"selftest_devices": {k: len(v) for k, v in devs.items()},
                  "boot_scans_b2_failed": {fmt(a): len(d) for a, d in failed.items()},
                  "boot_scans_kept_count": {fmt(a): len(d) for a, d in ok.items()},
                  "boot_scans_excluded_count": {fmt(a): len(d) for a, d in bad.items()}}
    print("boot scans, runs whose B2 failed (kept as fault evidence, not as statistics):")
    for a, ds in sorted(failed.items(), key=lambda kv: -len(kv[1])):
        print(f"  {len(ds):>4}  {fmt(a)}  e.g. {ds[0]}")
    print("excluded:")
    for n, w in excluded:
        print(f"  {n}: {w}")
    with open(args.json, "w") as fh:
        json.dump(out, fh, indent=1, default=str)
    print("wrote", args.json)


if __name__ == "__main__":
    sys.exit(main())
