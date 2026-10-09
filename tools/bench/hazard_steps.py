"""The hazard fixes' bench steps (B5''), as one catalogue: which fix each belongs to, the
plan for a firmware image, and the runner run_bench.py and the command line use.

    python hazard_steps.py --ip 192.168.137.218 --hazard c1,c3 [--steps B5''-1,B5pp-6]
                           [--port COM7] [--out DIR] [--tree DIR] [--soak-s 1800]
    python hazard_steps.py --list
    run_bench.py ... --hazard c1,c3          (the same steps, after B5..B5e)

Groups, in the order the fixes land (hazard-decisions.md §4): c1 (B5''-1..15,
scenario_c1.py), c3 (B5''-16..22, scenario_c3.py), c4 (B5f..B5j, scenario_c4.py), c2
(C2-1..16, scenario_c2.py). `--hazard` names the fixes the image has: it selects their
steps and sets what the steps expect (C3 F6: with c3 every step's start reads
STATE post = PASS instead of sending PERMIT POST pass). None of these steps is in
run_bench.py's default list: they need firmware that has the fixes' bench verbs, so they run
only when named (--hazard, or --steps with their names).

Retired as the fixes land (the specs' declared changes): with c3, B5''-15 is replaced by
B5''-18b (C3 F5); with c2, B5''-12 goes (C2 E9: PERMIT STICK is removed).

Names: B5''-N is the specs' spelling; B5pp-N is accepted for shells that dislike quotes.
Every step needs the stick-injection build (ci/sdkconfig.stick_inject); B5''-16, -18b,
-21, B5i and B5j also read the serial log (the board's port: run_bench.py has it from B0, or
--port). Verdicts: PASS, FAIL, NOT_RUN (the firmware lacks a verb or STATE field the step
needs, or the bench could not do its part; never a verdict on the firmware); INVALID only
from run_bench.py when the sole-sim proof fails (no step injects without it).
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import sys
from typing import Callable

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402
import hazard_rig  # noqa: E402
import scenario_c1  # noqa: E402
import scenario_c2  # noqa: E402
import scenario_c3  # noqa: E402
import scenario_c4  # noqa: E402
import ui_client  # noqa: E402

GROUP_ORDER = ("c1", "c3", "c4", "c2")
MODULES = {"c1": scenario_c1, "c3": scenario_c3, "c4": scenario_c4, "c2": scenario_c2}
GROUPS: dict[str, list[str]] = {g: list(MODULES[g].STEPS) for g in GROUP_ORDER}
ALL_STEPS: list[str] = [s for g in GROUP_ORDER for s in GROUPS[g]]
STEP_GROUP = {s: g for g in GROUP_ORDER for s in GROUPS[g]}
# Retired by a later fix: (step, the fix that retires it, why).
RETIRED_BY = {"B5''-15": ("c3", "C3 F5: replaced by B5''-18b"),
              "B5''-12": ("c2", "C2 E9: PERMIT STICK is gone, the monitor writes stick health")}
NEEDS_SERIAL = scenario_c1.NEEDS_SERIAL | scenario_c3.NEEDS_SERIAL | scenario_c4.NEEDS_SERIAL
# Steps whose injection is expected to lapse: a STALL blocks the remote-UI connection.
ALLOW_LAPSE = {"B5i", "B5j", "C2-16"}


def _restart_after(rig: hazard_rig.Rig) -> None:
    """CAL UNSAVED is RAM only: the board is restarted after the step (C1 §6)."""
    rig.inj.pause()
    rig.restart_hmi()
    rig.wait_back()
    rig.read_cal()
    rig.centre()


CLEANUP: dict[str, Callable[[hazard_rig.Rig], None]] = {
    **{s: _restart_after for s in scenario_c1.RESTART_AFTER},
    **scenario_c3.CLEANUP,
    **scenario_c2.CLEANUP,
}
SELFTEST_AFTER = scenario_c2.SELFTEST_AFTER
NOT_RUNNABLE = scenario_c2.NOT_RUNNABLE


def canonical(name: str) -> str | None:
    """A step's canonical name from any accepted spelling (case, B5pp for B5'')."""
    key = name.strip().upper().replace("PP", "''")
    for s in ALL_STEPS:
        if s.upper() == key:
            return s
    return None


def parse_groups(text: str | None) -> list[str]:
    """`c1,c3` or `all` -> the groups in landing order. ValueError on an unknown one."""
    if not text:
        return []
    asked = {g.strip().lower() for g in text.split(",") if g.strip()}
    if "all" in asked:
        return list(GROUP_ORDER)
    unknown = sorted(asked - set(GROUP_ORDER))
    if unknown:
        raise ValueError(f"unknown hazard groups {unknown}; known: {','.join(GROUP_ORDER)}")
    return [g for g in GROUP_ORDER if g in asked]


def plan(groups: list[str] | set[str]) -> list[str]:
    """The steps for an image with these fixes, retired ones left out."""
    have = set(groups)
    return [s for g in GROUP_ORDER if g in have for s in GROUPS[g]
            if not (s in RETIRED_BY and RETIRED_BY[s][0] in have)]


def run_step(name: str, ip: str, out: pathlib.Path, tree: pathlib.Path,
             groups: list[str] | set[str], port: str | None = None, proven: str | None = None,
             sweep: Callable[[], list[str]] | None = None, params: dict | None = None) -> dict:
    """One hazard step on the board; the step's result dict (verdict, checks, records)."""
    script, grade, title = MODULES[STEP_GROUP[name]].STEPS[name]
    st = hazard_rig.HazardStep(name, out)
    st.record("title", title)
    st.record("groups", sorted(groups))
    if name in NOT_RUNNABLE:
        st.not_run = f"bench: {NOT_RUNNABLE[name]}"
        return st.result()
    if name in NEEDS_SERIAL and not port:
        st.not_run = "bench: the step reads the serial log; no board port (B0, or --port)"
        return st.result()
    deferred = None
    try:
        with hazard_rig.Rig(ip, tree, out, st, set(groups), port, proven, sweep,
                            params) as rig:
            try:
                p = script(rig)
                tr = rig.collect()
                if rig.inj.lapses and name not in ALLOW_LAPSE:
                    raise hazard_rig.NotRun("bench", f"the stick injection lapsed "
                                            f"{rig.inj.lapses[:3]} (refresh gap over "
                                            f"{hazard_rig.INJECT_LAPSE_S} s)")
                if name in SELFTEST_AFTER:
                    deferred = (tr, p)
                else:
                    grade(st, tr, p)
            finally:
                _clean_up(rig, name)
        if deferred is not None:
            tr, p = deferred
            p.update(_selftest(ip, out, tree))
            grade(st, tr, p)
    except hazard_rig.NotRun as e:
        st.not_run = f"{e.kind}: {e.reason}"
    except ui_client.RemoteUiError as e:
        st.not_run = f"bench: remote UI: {e}"
    return st.result()


def _clean_up(rig: hazard_rig.Rig, name: str) -> None:
    try:
        if name in CLEANUP:
            CLEANUP[name](rig)
        rig.end()
    except (hazard_rig.NotRun, ui_client.RemoteUiError) as e:
        rig.st.check("clean-up", False, str(e))


def _selftest(ip: str, out: pathlib.Path, tree: pathlib.Path) -> dict:
    """C2-15: the self test after the soak, with no sim running (they both publish)."""
    import compare_selftest
    verdict, report = compare_selftest.run_selftest(ip, out / "selftest.json", tree)
    path = out / "selftest.json"
    if not path.exists():
        return {"selftest": {}, "selftest_note": f"{verdict}: {report.get('reason', '')}"}
    run = json.loads(path.read_text(encoding="utf-8"))
    return {"selftest": {r["name"]: r for r in run.get("results", [])},
            "selftest_note": f"self test {verdict}"}


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--ip")
    p.add_argument("--hazard", default="", help="fixes in the image: c1,c3,c4,c2 or all")
    p.add_argument("--steps", default=None, help="comma-separated (default: the --hazard plan)")
    p.add_argument("--port", default=None, help="the board's serial port (serial steps)")
    p.add_argument("--out", type=pathlib.Path,
                   default=common.BENCH_HOME / f"hazard-{common.stamp()}")
    p.add_argument("--tree", type=pathlib.Path, default=common.REPO)
    p.add_argument("--soak-s", type=float, default=None, help="C2-15's soak (default 1800)")
    p.add_argument("--list", action="store_true", help="list the steps and exit")
    a = p.parse_args()
    if a.list:
        for g in GROUP_ORDER:
            for s in GROUPS[g]:
                title = MODULES[g].STEPS[s][2]
                retired = f"  (retired with {RETIRED_BY[s][0]}: {RETIRED_BY[s][1]})" \
                    if s in RETIRED_BY else ""
                print(f"{g}  {s:10} {title}{retired}")
        return 0
    if not a.ip:
        p.error("--ip is required")
    try:
        groups = parse_groups(a.hazard)
    except ValueError as e:
        p.error(str(e))
    if a.steps:
        names = [canonical(s) or s for s in a.steps.split(",") if s.strip()]
        unknown = [s for s in names if s not in ALL_STEPS]
        if unknown:
            p.error(f"unknown steps {unknown}")
    else:
        names = plan(groups)
    if not names:
        p.error("no steps: name the fixes in the image with --hazard (or --steps)")
    ok, proof = hazard_rig.sole_sim_proof(a.ip, lambda: hazard_rig.rtps_sweep(a.tree))
    if not ok:
        print(json.dumps({"verdict": "INVALID", "reason": proof}, indent=2))
        return 2
    import lease
    params = {"soak_s": a.soak_s} if a.soak_s else {}
    with lease.held():
        report = {n: run_step(n, a.ip, a.out / n.replace("''", "pp").lower(), a.tree, groups,
                              a.port, proof, params=params) for n in names}
    print(json.dumps({n: r["verdict"] for n, r in report.items()}, indent=1))
    common.write_json(a.out / "hazard.json", report)
    return 1 if any(r["verdict"] == "FAIL" for r in report.values()) else 0


if __name__ == "__main__":
    sys.exit(main())
