"""DIAGNOSTIC (dev_ai_diag_ab, owner-approved 2026-10-13; never merged).

Boot series for the T1 A/B: N deliberate resets (B2's RTS pulse through the port, reset
reason USB) of the image on the board, each with the hazard rig (the sim plays the MCB; no
motion, the stick centred by injection after the remote UI is back). Per boot it records:
- the boot DISABLE: every DriveCommand DISABLE the sim saw after the reset, its time from the
  reset mark and from the restarted sim's first mark; and the serial "DriveCommand ... not
  published" warning (a refused publish);
- STATE's stick block xy_age_max_ms at the first poll after the remote UI is back and at the
  end of the boot's window (about 30 s after the reset);
- the self test's joy.xy_age_max (run after the rig, with no sim: they both publish MibStatus);
- the serial boot capture (App version, POST RESULT).
Data only: the script grades nothing (the comparison is in the results file).

  python diag_ab_boots.py --tree C:/w/diag_ab --ip 192.168.137.x --port COM9 --label A --boots 8
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
import time


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", type=pathlib.Path, required=True)
    ap.add_argument("--ip", required=True)
    ap.add_argument("--port", required=True)
    ap.add_argument("--label", required=True)
    ap.add_argument("--boots", type=int, default=8)
    ap.add_argument("--out", type=pathlib.Path, default=pathlib.Path(r"C:\b\bench\diag_ab"))
    a = ap.parse_args()
    sys.path.insert(0, str(a.tree / "tools" / "bench"))
    import common
    import compare_selftest
    import hazard_grade as hg
    import hazard_rig
    import lease
    from scenario_c2 import _stick, begin_c2

    out = a.out / f"{a.label}-{common.stamp()}"
    out.mkdir(parents=True, exist_ok=True)
    ok, proof = hazard_rig.sole_sim_proof(a.ip, lambda: hazard_rig.rtps_sweep(a.tree))
    if not ok:
        print(json.dumps({"verdict": "INVALID", "reason": proof}))
        return 2
    rows = []
    with lease.held(f"diag_ab-{a.label}"):
        for i in range(1, a.boots + 1):
            d = out / f"boot-{i}"
            st = hazard_rig.HazardStep(f"boot-{i}", d)
            row: dict = {"boot": i}
            try:
                with hazard_rig.Rig(a.ip, a.tree, d, st, {"c1", "c3", "c4", "c2"}, a.port,
                                    proof) as rig:
                    try:
                        begin_c2(rig)
                        rig.inj.pause()
                        rig.mark("restart")
                        rig._restart_by_port(45.0)  # the deliberate reset; serial kept 45 s
                        rig.wait_back()
                        rig.centre()
                        rig.mark("back")
                        t_back = time.monotonic()
                        rig.watch(15.0, until=hg.field_is("post", "PASS"))
                        rig.watch(max(1.0, t_back + 25.0 - time.monotonic()))
                        serial = rig.restart_watch.stop() if rig.restart_watch else ""
                        tr = rig.collect()
                    finally:
                        rig.end()
                r, b = tr.mark("restart"), tr.mark("back")
                sim_r = tr.marks.get("sim-restarted")
                dis = hg.drive_commands(tr.events, r, float("inf"), "DISABLE")
                ena = hg.drive_commands(tr.events, r, float("inf"), "ENABLE")
                after_back = [s for t, s in tr.states if t >= b]
                first = after_back[0] if after_back else None
                last = tr.states[-1][1] if tr.states else None
                row.update({
                    "disables_seen": len(dis),
                    "disable_t_from_reset_s": [round(e["mono"] - r, 2) for e in dis],
                    "disable_t_from_sim_restart_s": [round(e["mono"] - sim_r, 2) for e in dis]
                    if sim_r is not None else None,
                    "enables_seen": len(ena),
                    "back_from_reset_s": round(b - r, 2),
                    "xy_age_first_poll_ms": _stick(first, "xy_age_max_ms") if first else None,
                    "xy_age_last_poll_ms": _stick(last, "xy_age_max_ms") if last else None,
                    "stick_first": _stick(first, "state") if first else None,
                    "post_last": last.get("post") if last else None,
                    "refused_publish_warning": "not published" in serial,
                    "app_version": next(iter(re.findall(r"App version:\s*(\S+)", serial)), None),
                    "post_result": next(iter(re.findall(r"POST RESULT (\w+)", serial)), None),
                    "reset_reason": next(iter(re.findall(r"rst:(0x[0-9a-f]+ \(\w+\))", serial)),
                                         None),
                })
                (d / "serial.log").write_text(serial, encoding="utf-8")
            except Exception as e:  # noqa: BLE001 - a rig fault is recorded, the series goes on
                row["error"] = f"{type(e).__name__}: {e}"
            if st.not_run:
                row["not_run"] = st.not_run
            verdict, _ = compare_selftest.run_selftest(a.ip, d / "selftest.json", a.tree)
            try:
                res = json.loads((d / "selftest.json").read_text(encoding="utf-8"))["results"]
                xy = next((x for x in res if x["name"] == "joy.xy_age_max"), None)
                row["selftest_joy_xy_age_max"] = xy.get("value") if xy else None
                row["selftest_verdict"] = verdict
            except (OSError, ValueError, KeyError) as e:
                row["selftest_error"] = f"{verdict}: {e}"
            common.write_json(d / "result.json", st.result())
            rows.append(row)
            print(json.dumps(row), flush=True)
    common.write_json(out / "boots.json", rows)
    print(f"OUT {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
