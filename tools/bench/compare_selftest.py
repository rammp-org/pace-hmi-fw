"""B3: run the board's extended self test and compare it with the baseline, in bands.

    python compare_selftest.py --ip 192.168.137.180 --out run.json [--tree C:\\w\\bench]
    python compare_selftest.py --compare-only run.json

Exit 0 PASS, 1 FAIL, 2 INVALID. INVALID only for what the plan calls preflight:
a simulator or another self test already running (two MibStatus publishers
fail rtps.mcb_period and rtps.mcb_loss), or no answer from the board before
the run starts. Anything after the run started is PASS or FAIL.

PASS needs: rtps_selftest.py exit 0; the same check IDs as the baseline; the
same verdict per check; every value in its band (BANDS below).
Bands:
  exact    value == baseline (identity, presence, configuration, booleans)
  pct20    |value - baseline| <= 20 % of baseline (free memory, render/ADC timing)
  floor80  value >= 80 % of baseline: low-water marks (heap minimum, stack headroom)
           depend on what ran since boot. The baseline was taken at 500 s uptime
           after screenshots; B3 runs ~90 s after a reset, so its marks are higher.
           Only a drop is a regression (decided 02:10 after the dry run on
           e2047a4-identical code read +21..41 %; revisit).
  abs:N    |value - baseline| <= N (joystick rest voltages, 30 mV)
  limits   inside the check's own [lo, hi] (radio, network timing, battery,
           noise: things that legitimately move between runs)
  any      not compared (uptime)
`detail` is compared too where marked `+detail` (the I2C address list).
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402

BANDS = {
    "sys.clean_reset": "exact", "sys.cpu_mhz": "exact", "sys.uptime": "any",
    "log.capture": "exact", "net.link": "exact", "net.ip": "exact", "net.wifi_rssi": "limits",
    "rtps.mcb_link": "exact", "rtps.mcb_period": "pct20", "rtps.mcb_gap": "limits",
    "rtps.mcb_loss": "limits", "rtps.adc_hz": "limits", "rtps.rtt_p50": "limits",
    "rtps.rtt_p99": "limits", "rtps.ping_loss": "limits",
    "mem.int_free": "pct20", "mem.int_min": "floor80", "mem.int_block": "pct20",
    "mem.dma_free": "pct20", "mem.dma_min": "floor80", "mem.dma_block": "pct20",
    "mem.psram_free": "pct20", "mem.heap_ok": "exact",
    "mem.stk_lvgl": "floor80", "mem.stk_adc": "floor80", "mem.stk_rtps": "floor80",
    "i2c.missing": "exact", "i2c.devices": "exact+detail", "imu.accel": "limits",
    "rtc.tick": "limits", "pwr.vbat": "limits",
    "hap.drv_id": "exact", "hap.drv_fault": "exact", "hap.drv_play": "exact",
    "hap.da7280": "exact", "disp.direct": "exact", "disp.backlight": "exact",
    "time.render_avg": "pct20", "time.render_max": "pct20", "time.ui_stall": "limits",
    "time.adc_avg": "pct20", "time.adc_max": "limits",
    "joy.valid": "limits", "joy.x_rest": "abs:30", "joy.y_rest": "abs:30",
    "joy.twist_rest": "abs:30", "joy.x_noise": "limits", "joy.y_noise": "limits",
    "joy.twist_noise": "limits", "joy.cal_saved": "exact", "joy.x_cal_off": "limits",
    "joy.y_cal_off": "limits", "joy.twist_cal_off": "limits", "joy.button_idle": "exact",
    "pc.report_complete": "exact", "pc.spec_match": "exact", "pc.adc_rx_hz": "limits",
}


def in_band(band: str, got: dict, base: dict) -> tuple[bool, str]:
    v, b = got.get("value"), base.get("value")
    kind = band.split("+")[0]
    ok, why = True, ""
    if kind == "any":
        pass
    elif kind == "exact":
        ok, why = v == b, f"{v} != {b}"
    elif kind == "pct20":
        ok = v is not None and b is not None and abs(v - b) <= 0.2 * abs(b)
        why = f"{v} outside {b} +/-20%"
    elif kind == "floor80":
        ok = v is not None and b is not None and v >= 0.8 * b
        why = f"{v} below 80% of {b}"
    elif kind.startswith("abs:"):
        n = float(kind[4:])
        ok = v is not None and b is not None and abs(v - b) <= n
        why = f"{v} outside {b} +/-{n:g}"
    elif kind == "limits":
        lo, hi = got.get("lo"), got.get("hi")
        ok = v is not None and (lo is None or v >= lo) and (hi is None or v <= hi)
        why = f"{v} outside own limits [{lo}, {hi}]"
    else:
        ok, why = False, f"unknown band {band}"
    if ok and band.endswith("+detail") and got.get("detail") != base.get("detail"):
        ok, why = False, f"detail '{got.get('detail')}' != '{base.get('detail')}'"
    return ok, ("" if ok else why)


def compare(run: dict, base: dict) -> dict:
    problems, rows = [], []
    got = {r["name"]: r for r in run.get("results", [])}
    want = {r["name"]: r for r in base.get("results", [])}
    if list(got) != list(want):
        missing = [n for n in want if n not in got]
        extra = [n for n in got if n not in want]
        problems.append(f"check IDs differ: missing {missing}, extra {extra}"
                        + ("" if missing or extra else " (order)"))
    if run.get("verdict") != base.get("verdict"):
        problems.append(f"overall verdict {run.get('verdict')} != baseline {base.get('verdict')}")
    for name, b in want.items():
        g = got.get(name)
        if g is None:
            continue
        band = BANDS.get(name)
        row = {"name": name, "result": g.get("result"), "value": g.get("value"),
               "baseline": b.get("value"), "band": band}
        if band is None:
            row["problem"] = "no band defined for this check"
        elif g.get("result") != b.get("result"):
            row["problem"] = f"verdict {g.get('result')} != baseline {b.get('result')}"
        else:
            ok, why = in_band(band, g, b)
            if not ok:
                row["problem"] = why
        if "problem" in row:
            problems.append(f"{name}: {row['problem']}")
        rows.append(row)
    return {"verdict": "PASS" if not problems else "FAIL", "problems": problems, "checks": rows}


def run_selftest(ip: str, out: pathlib.Path, tree: pathlib.Path) -> tuple[str, dict]:
    """(verdict, report). Runs rtps_selftest.py from `tree`."""
    strays = common.stray_peers()
    if strays:
        return "INVALID", {"reason": "peer processes running (would double-publish MibStatus)",
                           "processes": strays}
    cmd = [common.python(), str(tree / "scripts" / "rtps_selftest.py"), "--peer", ip,
           "--bind-address", common.PC_IP, "--json", str(out)]
    common.log("selftest: " + " ".join(cmd))
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=400, cwd=str(tree),
                       env=common.child_env())
    text = r.stdout + r.stderr
    out.with_suffix(".stdout.txt").write_text(text, encoding="utf-8")
    report = {"selftest_exit": r.returncode, "stdout_tail": text[-1500:]}
    if r.returncode == 2 and ("Could not find the board" in text or "did not appear within" in text):
        report["reason"] = "no answer before the run started"
        return "INVALID", report
    if not out.exists():
        report["reason"] = f"rtps_selftest.py exit {r.returncode} and no JSON"
        return "FAIL", report
    run = json.loads(out.read_text(encoding="utf-8"))
    base = json.loads((common.BASELINE_DIR / "selftest.json").read_text(encoding="utf-8"))
    cmp = compare(run, base)
    report.update(cmp)
    if r.returncode != 0:
        report["problems"].insert(0, f"rtps_selftest.py exit {r.returncode}")
        report["verdict"] = "FAIL"
    return report["verdict"], report


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--ip")
    p.add_argument("--out", type=pathlib.Path, default=common.BENCH_HOME / f"selftest-{common.stamp()}.json")
    p.add_argument("--tree", type=pathlib.Path, default=common.REPO)
    p.add_argument("--compare-only", type=pathlib.Path)
    a = p.parse_args()
    if a.compare_only:
        base = json.loads((common.BASELINE_DIR / "selftest.json").read_text(encoding="utf-8"))
        report = compare(json.loads(a.compare_only.read_text(encoding="utf-8")), base)
        verdict = report["verdict"]
    else:
        if not a.ip:
            p.error("--ip is required unless --compare-only")
        import lease
        with lease.held():
            verdict, report = run_selftest(a.ip, a.out, a.tree)
    print(json.dumps({k: v for k, v in report.items() if k != "checks"}, indent=2))
    return {"PASS": 0, "FAIL": 1, "INVALID": 2}[verdict]


if __name__ == "__main__":
    sys.exit(main())
