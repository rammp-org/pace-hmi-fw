"""B3: run the board's extended self test and compare it with the baseline, in bands.

    python compare_selftest.py --ip 192.168.137.180 --out run.json [--tree C:\\w\\bench]
    python compare_selftest.py --compare-only run.json
    python compare_selftest.py selftest      # cases CMP-001.., inline samples, no board

Exit 0 PASS, 1 FAIL, 2 INVALID. INVALID only for what the plan calls preflight:
a simulator or another self test already running (two MibStatus publishers
fail rtps.mcb_period and rtps.mcb_loss), or no answer from the board before
the run starts. Anything after the run started is PASS or FAIL.

PASS needs: rtps_selftest.py exit 0; the same check IDs as the baseline; the
same verdict per check; every value in its band (BANDS below).
Bands:
  exact    value == baseline (identity, presence, configuration, booleans)
  pctN     |value - baseline| <= N % of baseline (pct20: free memory, render/ADC timing;
           pct30: time.render_max, a worst-case value that was outside 20 % once in 6
           runs on 2026-10-06; widened on the owner's decision, P4)
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
Report-only (REPORT_ONLY_PREFIXES, pwr.*): value, verdict and baseline are shown in
the check's row, never graded; the check ID must still be there. Owner decision P3
(docs/project-profile.md, Bench > Power): board 2 has no battery and runs on PoE, so
pwr.vbat measures no pack (4346 mV read as "no pack fitted", SKIP, on chrome-0677bb5).
Until a battery is fitted.
"""

from __future__ import annotations

import argparse
import copy
import inspect
import json
import os
import pathlib
import subprocess
import sys
import traceback

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
    "time.render_avg": "pct20", "time.render_max": "pct30", "time.ui_stall": "limits",
    "time.adc_avg": "pct20", "time.adc_max": "limits",
    "joy.valid": "limits", "joy.x_rest": "abs:30", "joy.y_rest": "abs:30",
    "joy.twist_rest": "abs:30", "joy.x_noise": "limits", "joy.y_noise": "limits",
    "joy.twist_noise": "limits", "joy.cal_saved": "exact", "joy.x_cal_off": "limits",
    "joy.y_cal_off": "limits", "joy.twist_cal_off": "limits", "joy.button_idle": "exact",
    "pc.report_complete": "exact", "pc.spec_match": "exact", "pc.adc_rx_hz": "limits",
}

# Report-only: shown, never graded. Owner decision P3 (docs/project-profile.md, Bench >
# Power): board 2 has NO battery and is powered over PoE, so pwr.vbat does not read a
# battery and is ignored for now. Remove the prefix once a battery is fitted.
REPORT_ONLY_PREFIXES = ("pwr.",)
REPORT_ONLY_WHY = "report-only: owner P3, board 2 has no battery (PoE); see project-profile Bench"


# Checks added after the baseline was taken, each a declared change approved with its spec
# (hazard-decisions.md G1): accepted as extra IDs, in this order, after the board's own checks
# of the baseline and before the PC's pc.* rows (rtps_selftest.py appends those), and graded
# against their own limits (there is no baseline value). pc.report_complete counts the board's
# reports, so it is the baseline's plus one per declared check. Remove a name once a new
# baseline carries it.
DECLARED_NEW = (
    "ctl.wdt", "ctl.ui_age_max", "ctl.ui_stalls_drive",  # hazard-c4-spec.md §8 B3, §9.2
    "joy.health", "joy.bad_samples", "joy.xy_age_max",  # hazard-c2-spec.md §8.6 E10
)


def report_only(name: str) -> bool:
    return name.startswith(REPORT_ONLY_PREFIXES)


def in_band(band: str, got: dict, base: dict) -> tuple[bool, str]:
    v, b = got.get("value"), base.get("value")
    kind = band.split("+")[0]
    ok, why = True, ""
    if kind == "any":
        pass
    elif kind == "exact":
        ok, why = v == b, f"{v} != {b}"
    elif kind.startswith("pct") and kind[3:].isdigit():
        pct = int(kind[3:])
        ok = v is not None and b is not None and abs(v - b) <= pct / 100 * abs(b)
        why = f"{v} outside {b} +/-{pct}%"
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


def declared_in_place(names: list[str], new: list[str]) -> bool:
    """The declared new checks are in DECLARED_NEW's order, after every board check and before
    every pc.* row of the run."""
    if new != [n for n in DECLARED_NEW if n in new]:
        return False
    at = {n: i for i, n in enumerate(names)}
    board = [at[n] for n in names if n not in new and not n.startswith("pc.")]
    pc = [at[n] for n in names if n.startswith("pc.")]
    return all((not board or at[n] > max(board)) and (not pc or at[n] < min(pc)) for n in new)


def compare(run: dict, base: dict) -> dict:
    problems, rows = [], []
    got = {r["name"]: r for r in run.get("results", [])}
    want = {r["name"]: r for r in base.get("results", [])}
    new = [n for n in got if n in DECLARED_NEW and n not in want]
    if not declared_in_place(list(got), new):
        problems.append(f"declared new checks out of order or not between the board's checks "
                        f"and the pc.* rows: {new}")
    got_old = {n: r for n, r in got.items() if n not in new}
    if list(got_old) != list(want):
        missing = [n for n in want if n not in got_old]
        extra = [n for n in got_old if n not in want]
        problems.append(f"check IDs differ: missing {missing}, extra {extra}"
                        + ("" if missing or extra else " (order)"))
    if run.get("verdict") != base.get("verdict"):
        problems.append(f"overall verdict {run.get('verdict')} != baseline {base.get('verdict')}")
    for name, b in want.items():
        g = got.get(name)
        if g is None:
            continue
        if name == "pc.report_complete" and new and isinstance(b.get("value"), int):
            b = dict(b, value=b["value"] + len(new))  # one report per declared check
        band = BANDS.get(name)
        row = {"name": name, "result": g.get("result"), "value": g.get("value"),
               "baseline": b.get("value"), "band": band}
        if report_only(name):
            row.update(baseline_result=b.get("result"), graded=False, note=REPORT_ONLY_WHY)
        elif band is None:
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
    for name in new:
        g = got[name]
        row = {"name": name, "result": g.get("result"), "value": g.get("value"),
               "baseline": None, "band": "limits", "note": "declared new check, no baseline"}
        ok, why = in_band("limits", g, {})
        if g.get("result") != "PASS" or not ok:
            row["problem"] = why or f"verdict {g.get('result')}"
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


# ---------------------------------------------------------------- selftest


def _baseline() -> dict:
    return json.loads((common.BASELINE_DIR / "selftest.json").read_text(encoding="utf-8"))


def _with(base: dict, name: str, **fields: object) -> dict:
    run = copy.deepcopy(base)
    for r in run["results"]:
        if r["name"] == name:
            r.update(fields)
    return run


def _expect(what: str, got: object, want: object) -> None:
    if got != want:
        raise AssertionError(f"{what}: got {got!r}, want {want!r}")


def t_equal_passes() -> None:
    base = _baseline()
    _expect("the baseline against itself", compare(copy.deepcopy(base), base)["verdict"], "PASS")


def t_vbat_skip_not_graded() -> None:
    base = _baseline()
    run = _with(base, "pwr.vbat", result="SKIP", value=4346, detail="taken as no pack fitted")
    report = compare(run, base)
    _expect("verdict", (report["verdict"], report["problems"]), ("PASS", []))
    row = next(r for r in report["checks"] if r["name"] == "pwr.vbat")
    _expect("the row still shows it", (row["result"], row["value"], row["baseline_result"],
                                       row["graded"]), ("SKIP", 4346, "PASS", False))


def t_vbat_out_of_limits_not_graded() -> None:
    base = _baseline()
    report = compare(_with(base, "pwr.vbat", result="FAIL", value=100), base)
    _expect("a FAIL far out of limits on pwr.vbat", report["verdict"], "PASS")


def t_other_skip_still_fails() -> None:
    base = _baseline()
    for name in ("imu.accel", "hap.drv_id", "joy.x_rest"):
        report = compare(_with(base, name, result="SKIP"), base)
        _expect(f"SKIP vs PASS on {name}", (report["verdict"],
                                             any(p.startswith(name) for p in report["problems"])),
                ("FAIL", True))


def t_vbat_must_still_be_there() -> None:
    base = _baseline()
    run = copy.deepcopy(base)
    run["results"] = [r for r in run["results"] if r["name"] != "pwr.vbat"]
    _expect("a missing pwr.vbat is still a changed check list", compare(run, base)["verdict"],
            "FAIL")


def _with_declared(base: dict) -> dict:
    """The baseline as a run of a C4 image reports it: the three ctl.* checks after the board's
    checks, before the PC's pc.* rows, and pc.report_complete three reports more."""
    run = copy.deepcopy(base)
    rows = run["results"]
    first_pc = next((i for i, r in enumerate(rows) if r["name"].startswith("pc.")), len(rows))
    rows[first_pc:first_pc] = [
        {"name": "ctl.wdt", "result": "PASS", "value": 1, "lo": 1, "hi": 1},
        {"name": "ctl.ui_age_max", "result": "PASS", "value": 127, "lo": 0, "hi": 1000},
        {"name": "ctl.ui_stalls_drive", "result": "PASS", "value": 0, "lo": 0, "hi": 0}]
    for r in rows:
        if r["name"] == "pc.report_complete" and isinstance(r.get("value"), int):
            r["value"] += 3
    return run


def t_declared_new_checks() -> None:
    base = _baseline()  # its last rows are the PC's: pc.report_complete 54, ...
    run = _with_declared(base)
    _expect("C4's three declared checks before the pc.* rows, pc.report_complete 57",
            (compare(run, base)["verdict"], compare(run, base)["problems"]), ("PASS", []))
    count = copy.deepcopy(run)
    next(r for r in count["results"] if r["name"] == "pc.report_complete")["value"] = 54
    _expect("pc.report_complete not counting them", compare(count, base)["verdict"], "FAIL")
    bad = copy.deepcopy(run)
    next(r for r in bad["results"] if r["name"] == "ctl.ui_stalls_drive").update(result="FAIL",
                                                                                   value=2)
    _expect("a declared check out of its limits", compare(bad, base)["verdict"], "FAIL")
    moved = copy.deepcopy(run)
    moved["results"].insert(0, moved["results"].pop(
        next(i for i, r in enumerate(moved["results"]) if r["name"] == "ctl.wdt")))
    _expect("a declared check before the board's checks", compare(moved, base)["verdict"], "FAIL")
    late = copy.deepcopy(run)
    late["results"].append(late["results"].pop(
        next(i for i, r in enumerate(late["results"]) if r["name"] == "ctl.wdt")))
    _expect("a declared check after the pc.* rows", compare(late, base)["verdict"], "FAIL")
    c2 = _with_declared(base)  # C2's three joy.* after C4's three, pc.report_complete + 6
    rows = c2["results"]
    first_pc = next(i for i, r in enumerate(rows) if r["name"].startswith("pc."))
    rows[first_pc:first_pc] = [
        {"name": "joy.health", "result": "PASS", "value": 1, "lo": 1, "hi": 1},
        {"name": "joy.bad_samples", "result": "PASS", "value": 0, "lo": 0, "hi": 0},
        {"name": "joy.xy_age_max", "result": "PASS", "value": 140, "lo": 0, "hi": 200}]
    next(r for r in rows if r["name"] == "pc.report_complete")["value"] += 3
    _expect("C2's joy.* after C4's ctl.*, pc.report_complete 60",
            (compare(c2, base)["verdict"], compare(c2, base)["problems"]), ("PASS", []))
    swapped = copy.deepcopy(c2)
    names = [r["name"] for r in swapped["results"]]
    i, j = names.index("joy.health"), names.index("joy.xy_age_max")
    swapped["results"][i], swapped["results"][j] = swapped["results"][j], swapped["results"][i]
    _expect("C2's checks out of their order", compare(swapped, base)["verdict"], "FAIL")
    other = copy.deepcopy(base)
    other["results"].append({"name": "x.new", "result": "PASS", "value": 1})
    _expect("an undeclared extra check", compare(other, base)["verdict"], "FAIL")


CASES = [
    ("CMP-001 a run equal to the baseline passes", t_equal_passes),
    ("CMP-002 pwr.vbat SKIP vs baseline PASS does not fail; the row shows it (owner P3)",
     t_vbat_skip_not_graded),
    ("CMP-003 pwr.vbat FAIL out of its limits does not fail either (report-only)",
     t_vbat_out_of_limits_not_graded),
    ("CMP-004 SKIP vs baseline PASS on any other check still fails", t_other_skip_still_fails),
    ("CMP-005 report-only does not drop the check from the ID comparison",
     t_vbat_must_still_be_there),
    ("CMP-006 the declared new checks (C4's ctl.*) are accepted after the board's checks and "
     "before the pc.* rows, graded by their limits; pc.report_complete counts them",
     t_declared_new_checks),
]


def selftest() -> int:
    """Unity's format, as tools/guards/guardlib.run_cases prints it."""
    fails = 0
    for name, fn in CASES:
        line = inspect.getsourcelines(fn)[1]
        try:
            fn()
            print(f"tools/bench/compare_selftest.py:{line}:{name}:PASS")
        except Exception as exc:  # noqa: BLE001 - every exception is a failed case
            fails += 1
            print(f"tools/bench/compare_selftest.py:{line}:{name}:FAIL: "
                  f"{str(exc) or type(exc).__name__}")
            traceback.print_exc(file=sys.stdout)
    print("\n-----------------------")
    print(f"{len(CASES)} Tests {fails} Failures 0 Ignored")
    print("OK" if fails == 0 else "FAIL")
    return 0 if fails == 0 else 1


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
    if sys.argv[1:] == ["selftest"]:
        sys.exit(selftest())
    sys.exit(main())
