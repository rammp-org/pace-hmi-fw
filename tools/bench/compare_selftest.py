"""B3: run the board's extended self test and compare it with the baseline, in bands.

    python compare_selftest.py --ip 192.168.137.180 --out run.json [--tree <tree>]
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
the check's row, never graded; the check ID must still be there.
Declared new checks (common.variant_checks, from the image's build config, e.g. the
allocation guard's mem.alloc_*): rows the baseline cannot have. They are not "extra" IDs;
each must be there (the build's option adds it) and is graded on its own verdict (FAIL fails),
unless it is report-only (mem.alloc_adc, owner T12); pc.report_complete counts them on top of
the baseline's value. Owner decision P3
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
    # bench rows (Need::BENCH): only an image with the allocation guard reports them, so a
    # baseline taken without it has none and such a run differs in its check IDs. Counts grow
    # with uptime; mem.alloc_adc is report-only on the device too (owner T12)
    "mem.alloc_adc": "any", "mem.alloc_lvgl": "any", "mem.alloc_rtps": "any",
    "pc.report_complete": "exact", "pc.spec_match": "exact", "pc.adc_rx_hz": "limits",
}

# Report-only: shown, never graded. Owner decision P3 (docs/project-profile.md, Bench >
# Power): board 2 has NO battery and is powered over PoE, so pwr.vbat does not read a
# battery and is ignored for now. Remove the prefix once a battery is fitted.
REPORT_ONLY_PREFIXES = ("pwr.",)
REPORT_ONLY_WHY = "report-only: owner P3, board 2 has no battery (PoE); see project-profile Bench"


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


def compare(run: dict, base: dict, declared: dict[str, dict] | None = None) -> dict:
    """`declared` (common.variant_checks): new checks this build adds, name -> {why, report_only}."""
    declared = declared or {}
    problems, rows = [], []
    got_all = {r["name"]: r for r in run.get("results", [])}
    got = {n: r for n, r in got_all.items() if n not in declared}
    want = {r["name"]: r for r in base.get("results", [])}
    added = [n for n in got_all if n in declared]
    gone = [n for n in declared if n not in got_all]
    if gone:
        problems.append(f"declared new checks missing: {gone} (the build's config adds them)")
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
        if name == "pc.report_complete" and added and isinstance(g.get("value"), int):
            g = dict(g, value=g["value"] - len(added))  # the declared rows on top of the baseline's
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
    for name in added:
        g, d = got_all[name], declared[name]
        row = {"name": name, "result": g.get("result"), "value": g.get("value"), "baseline": None,
               "band": "new", "declared": d["why"]}
        if d.get("report_only"):
            row.update(graded=False, note=d["report_only"])
        elif g.get("result") == "FAIL":
            row["problem"] = f"declared new check FAILs on its own limits ({g.get('detail', '')})"
            problems.append(f"{name}: {row['problem']}")
        rows.append(row)
    return {"verdict": "PASS" if not problems else "FAIL", "problems": problems, "checks": rows}


def run_selftest(ip: str, out: pathlib.Path, tree: pathlib.Path,
                 declared: dict[str, dict] | None = None) -> tuple[str, dict]:
    """(verdict, report). Runs rtps_selftest.py from `tree`; `declared`: see compare()."""
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
    cmp = compare(run, base, declared)
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


def _guard_run(base: dict, adc: dict, lvgl: dict) -> dict:
    """The baseline as an alloc-guard image reports it: three mem.alloc_* rows after the device
    rows, pc.report_complete 3 more."""
    run = copy.deepcopy(base)
    device = [r for r in run["results"] if not r["name"].startswith("pc.")]
    pc = [r for r in run["results"] if r["name"].startswith("pc.")]
    new = [{"name": "mem.alloc_adc", "unit": "", "lo": 0, "hi": None, **adc},
           {"name": "mem.alloc_lvgl", "unit": "", "lo": 0, "hi": None, **lvgl},
           {"name": "mem.alloc_rtps", "unit": "", "lo": 0, "hi": None, "result": "PASS",
            "value": 2, "detail": "first 16 B"}]
    for r in pc:
        if r["name"] == "pc.report_complete":
            r.update(value=r["value"] + 3, lo=r["lo"] + 3, hi=r["hi"] + 3)
    run["results"] = device + new + pc
    return run


GUARD = {"mem.alloc_adc": {"why": "guard", "report_only": "owner T12"},
         "mem.alloc_lvgl": {"why": "guard", "report_only": None},
         "mem.alloc_rtps": {"why": "guard", "report_only": None}}


def t_declared_new_checks_pass() -> None:
    base = _baseline()
    run = _guard_run(base, {"result": "PASS", "value": 900, "detail": "first 24 B"},
                     {"result": "PASS", "value": 4000, "detail": "first 64 B"})
    report = compare(run, base, GUARD)
    _expect("verdict", (report["verdict"], report["problems"]), ("PASS", []))
    row = next(r for r in report["checks"] if r["name"] == "mem.alloc_adc")
    _expect("the adc row is shown, not graded", (row["value"], row["graded"]), (900, False))
    _expect("without the declaration the same run fails", compare(run, base)["verdict"], "FAIL")


def t_declared_report_only_and_graded() -> None:
    base = _baseline()
    fail_adc = _guard_run(base, {"result": "FAIL", "value": 900, "detail": "first 24 B"},
                          {"result": "PASS", "value": 4000, "detail": ""})
    _expect("a FAIL on report-only mem.alloc_adc does not fail",
            compare(fail_adc, base, GUARD)["verdict"], "PASS")
    fail_lvgl = _guard_run(base, {"result": "PASS", "value": 0, "detail": ""},
                           {"result": "FAIL", "value": 4000, "detail": ""})
    report = compare(fail_lvgl, base, GUARD)
    _expect("a FAIL on a graded declared row fails",
            (report["verdict"], [p.split(":")[0] for p in report["problems"]]),
            ("FAIL", ["mem.alloc_lvgl"]))


def t_declared_missing_or_other_extra() -> None:
    base = _baseline()
    _expect("declared rows absent from the run fail", compare(copy.deepcopy(base), base, GUARD)[
        "verdict"], "FAIL")
    run = _guard_run(base, {"result": "PASS", "value": 0, "detail": ""},
                     {"result": "PASS", "value": 0, "detail": ""})
    only_two = {k: v for k, v in GUARD.items() if k != "mem.alloc_rtps"}
    report = compare(run, base, only_two)
    _expect("an undeclared extra row still fails", (report["verdict"], any(
        "extra ['mem.alloc_rtps']" in p for p in report["problems"])), ("FAIL", True))


CASES = [
    ("CMP-001 a run equal to the baseline passes", t_equal_passes),
    ("CMP-002 pwr.vbat SKIP vs baseline PASS does not fail; the row shows it (owner P3)",
     t_vbat_skip_not_graded),
    ("CMP-003 pwr.vbat FAIL out of its limits does not fail either (report-only)",
     t_vbat_out_of_limits_not_graded),
    ("CMP-004 SKIP vs baseline PASS on any other check still fails", t_other_skip_still_fails),
    ("CMP-005 report-only does not drop the check from the ID comparison",
     t_vbat_must_still_be_there),
    ("CMP-006 an alloc-guard image's declared mem.alloc_* rows and pc.report_complete +3 pass; "
     "undeclared, the same run fails", t_declared_new_checks_pass),
    ("CMP-007 declared rows: report-only mem.alloc_adc is never graded; another declared row's "
     "FAIL fails", t_declared_report_only_and_graded),
    ("CMP-008 a declared row missing from the run fails; an extra row not declared still fails",
     t_declared_missing_or_other_extra),
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
    p.add_argument("--build-dir", type=pathlib.Path,
                   help="the image's build dir: its config says which new checks it declares")
    a = p.parse_args()
    declared = common.variant_checks(a.build_dir, a.tree)
    if a.compare_only:
        base = json.loads((common.BASELINE_DIR / "selftest.json").read_text(encoding="utf-8"))
        report = compare(json.loads(a.compare_only.read_text(encoding="utf-8")), base, declared)
        verdict = report["verdict"]
    else:
        if not a.ip:
            p.error("--ip is required unless --compare-only")
        import lease
        with lease.held():
            verdict, report = run_selftest(a.ip, a.out, a.tree, declared)
    print(json.dumps({k: v for k, v in report.items() if k != "checks"}, indent=2))
    return {"PASS": 0, "FAIL": 1, "INVALID": 2}[verdict]


if __name__ == "__main__":
    if sys.argv[1:] == ["selftest"]:
        sys.exit(selftest())
    sys.exit(main())
