"""One command for the board tests of plan §6 (B0..B5).

    python run_bench.py --build-dir C:\\b\\main_bench --label baseline-bench --flash
    python run_bench.py --build-dir <dir> --label dry --steps B0,B1,B2,B3,B4,B5   # no flash

Takes the board lease, runs the steps in order, writes
C:\\b\\bench\\results\\<label>-<time>\\summary.json, releases the lease.
Exit 0 when every step that ran is PASS (or SKIP), 1 on any FAIL, 2 on INVALID.

Verdicts per step: PASS, FAIL, INVALID (only from the B0 preflight, and from
B2's tethering rule), SKIP (declared by the command line: B1's flash without
--flash, or a step not in --steps), NOT_RUN (an earlier step left nothing to
test against, e.g. no IP from B2: not a verdict on the firmware).

B0  port by MAC; tethering On; the PC holds 192.168.137.2; no stray rtps_mcb_*/
    selftest process; an RTPS sweep of the subnet finds at most one peer (the
    board); the board's partition table == the build's; with --flash, the build
    is a bench build (CONFIG_HMI_REMOTE_UI 1) with all images. Any false: INVALID,
    and nothing else runs.
B1  back up /storage once (C:\\b\\bench\\storage-*.bin, never overwritten); with
    --flash, write-flash @flash_args (no erase; the table is compared again just
    before writing). Exit 0 = PASS.
B2  deliberate reset, 90 s capture, boot_check.py. "WiFi joined" without "Got
    IP" for 60 s: restart tethering (once per run) and watch 60 s more; still no
    IP: INVALID. A flashed build that FAILs B2 is followed by a restore of the
    last-good image: if last-good boots with an IP the candidate's FAIL stands,
    otherwise B2 is INVALID. With --flash and every step PASS the build is
    saved as last-good <label> at the end of the run.
B3  compare_selftest.py (no sim may run).   B4  walk_check.py.
B4b ui_models_check.py: PIN pad (wrong PIN notice, 1234 opens the actuators page) and the
    Seat Functions cursor walk against the hmi_models goldens; navigation only.
B5  scenario_drive.py.
B5a..B5e (only with --sim-mode-steps; opt-in until they have run on the board
    once): scenario_hazards.py, the drive path against the sim's fault modes
    (exit hold, burger-key exit, relock on link loss, profile click after relock,
    ignored/dropped DISABLE). B5e is a characterisation: RECORD, not PASS/FAIL.
    RECORD counts as passing for the run's verdict and exit code, but a run with
    a RECORD step never saves last-good (that needs every step PASS).
"""

from __future__ import annotations

import argparse
import ipaddress
import json
import os
import pathlib
import socket
import subprocess
import sys
import time
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import board  # noqa: E402
import boot_check  # noqa: E402
import common  # noqa: E402
import compare_selftest  # noqa: E402
import flash  # noqa: E402
import lease  # noqa: E402
import scenario_drive  # noqa: E402
import scenario_hazards  # noqa: E402
import ui_models_check  # noqa: E402
import walk_check  # noqa: E402

ALL_STEPS = ["B0", "B1", "B2", "B3", "B4", "B4b", "B5"]
# Opt-in (--sim-mode-steps) until they have run on the board once; then they join ALL_STEPS.
SIM_MODE_STEPS = list(scenario_hazards.STEPS)
BOOT_CAPTURE_S = 90.0
NO_IP_AFTER_JOIN_S = 60.0


class Run:
    def __init__(self, a: argparse.Namespace):
        self.a = a
        self.dir = common.RESULTS_DIR / f"{a.label}-{common.stamp()}"
        self.dir.mkdir(parents=True, exist_ok=True)
        self.summary: dict = {"label": a.label, "build_dir": str(a.build_dir), "flash": a.flash,
                              "tree": str(a.tree), "started": common.now_iso(), "steps": {}}
        self.port: str | None = None
        self.ip: str | None = None
        self.hotspot_restarts = 0

    def record(self, step: str, verdict: str, **detail: object) -> str:
        self.summary["steps"][step] = {"verdict": verdict, **detail}
        common.log(f"{step}: {verdict}")
        self.save()
        return verdict

    def save(self) -> None:
        common.write_json(self.dir / "summary.json", self.summary)

    # --- tethering ---------------------------------------------------------

    def hotspot(self, action: str) -> tuple[int, str]:
        r = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                            str(common.HERE / "hotspot.ps1"), action],
                           capture_output=True, text=True, timeout=120)
        return r.returncode, (r.stdout + r.stderr).strip()

    def restart_hotspot_once(self) -> str:
        if self.hotspot_restarts >= 1:
            return "not restarted: the one restart of this run is used"
        self.hotspot_restarts += 1
        code, out = self.hotspot("restart")
        return f"restart exit {code}: {out}"

    # --- B0 ----------------------------------------------------------------

    def b0(self) -> str:
        checks = []

        def check(name: str, ok: bool, detail: str) -> None:
            checks.append({"name": name, "ok": ok, "detail": detail})

        try:
            self.port = board.find_port()
            check("port", True, f"{self.port} = {common.BOARD_MAC}")
        except board.NoBoard as e:
            check("port", False, str(e))
        code, out = self.hotspot("status")
        check("tethering", code == 0 and "STATE On" in out, out)
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                s.bind((common.PC_IP, 0))
            check("pc_ip", True, f"{common.PC_IP} is local")
        except OSError as e:
            check("pc_ip", False, f"cannot bind {common.PC_IP}: {e}")
        strays = common.stray_peers()
        check("no_stray_peers", not strays, "; ".join(strays) or "none")
        try:
            peers_found = self.rtps_sweep()
            check("rtps_peers", len(peers_found) <= 1,
                  f"responders {peers_found} (at most one: the board)")
        except Exception as e:
            check("rtps_peers", False, f"sweep failed: {e}")
        if self.port:
            if self.a.flash:
                checks.extend(flash.preflight(self.a.build_dir, self.port))
            else:
                try:
                    ok, detail = board.compare_partition_table(
                        self.port, self.a.build_dir, keep=self.dir / "board-pt.bin")
                except Exception as e:
                    ok, detail = False, f"could not read the board's table: {e}"
                check("partition_table", ok, detail)
                check("bench_config", True, "not applicable: no flash, the board keeps its image")
        ok = all(c["ok"] for c in checks)
        return self.record("B0", "PASS" if ok else "INVALID", checks=checks)

    def rtps_sweep(self) -> list[str]:
        sys.path.insert(0, str(self.a.tree / "scripts"))
        import rtps_net
        hosts = [str(h) for h in ipaddress.IPv4Network(common.SUBNET).hosts()
                 if str(h) not in (common.PC_IP, "192.168.137.1")]
        # In chunks: one listener seeding all 252 addresses spends its window on
        # ARP for empty addresses and never reached the board (seen 02:05).
        found: set[str] = set()
        for i in range(0, len(hosts), 64):
            found |= set(rtps_net._run_listener(hosts[i:i + 64], 6.0, common.PC_IP,
                                                stop_on_first=False))
        return sorted(found)

    # --- B1 ----------------------------------------------------------------

    def b1(self) -> str:
        backup, made = board.backup_storage(self.port)
        detail = {"storage_backup": str(backup), "backup_made_now": made}
        if not self.a.flash:
            return self.record("B1", "SKIP", reason="no --flash", **detail)
        code, out = flash.flash(self.a.build_dir, self.port)
        (self.dir / "flash.log").write_text(out, encoding="utf-8")
        if code != 0 and ("busy" in out.lower() or "could not open" in out.lower()):
            return self.record("B1", "INVALID", reason="port busy", exit=code, **detail)
        return self.record("B1", "PASS" if code == 0 else "FAIL", exit=code, **detail)

    # --- B2 ----------------------------------------------------------------

    def boot(self, tag: str) -> tuple[dict, list[str]]:
        """Reset, capture, grade. Applies the tethering rule. Returns (report, notes)."""
        notes: list[str] = []
        state = {"joined": None}

        def on_line(cap: board.Capture, line: str) -> bool:
            t = time.monotonic() - cap.started
            if "WiFi joined" in line and state["joined"] is None:
                state["joined"] = t
            return False

        cap = board.capture(self.port, BOOT_CAPTURE_S, reset=True, on_line=on_line)
        notes += cap.notes
        text = cap.text()
        if "Got IP" not in text and state["joined"] is not None:
            # The rule is 60 s from the join, so first finish that wait (no reset).
            left = NO_IP_AFTER_JOIN_S - (BOOT_CAPTURE_S - state["joined"])
            if left > 0:
                more = board.capture(self.port, left, reset=False,
                                     on_line=lambda c, l: "Got IP" in l)
                text += more.text()
                notes += more.notes
            if "Got IP" not in text:
                notes.append(f"WiFi joined at {state['joined']:.0f}s, no Got IP within "
                             f"{NO_IP_AFTER_JOIN_S:.0f}s: {self.restart_hotspot_once()}")
                more = board.capture(self.port, NO_IP_AFTER_JOIN_S, reset=False,
                                     on_line=lambda c, l: "Got IP" in l)
                text += more.text()
                notes += more.notes
        (self.dir / f"boot-{tag}.log").write_text(text, encoding="utf-8")
        baseline = (common.BASELINE_DIR / "boot-board2.log").read_text(encoding="utf-8",
                                                                        errors="replace")
        report = boot_check.analyse(text, baseline)
        report["joined_wifi"] = state["joined"] is not None
        return report, notes

    def b2(self) -> str:
        report, notes = self.boot("candidate")
        report.pop("verdict")
        no_ip_rule = report["ip"] is None and report["joined_wifi"]
        if not report["problems"]:
            self.ip = report["ip"]
            self.summary["board_ip"] = self.ip
            return self.record("B2", "PASS", notes=notes, **report)
        if not self.a.flash:
            verdict = "INVALID" if no_ip_rule and only_ip_missing(report) else "FAIL"
            return self.record("B2", verdict, notes=notes, **report)
        # A flashed candidate that does not boot: restore last-good and boot that.
        good = self.last_good()
        if good is None:
            notes.append("no last-good image to restore")
            return self.record("B2", "FAIL", notes=notes, **report)
        code, out = flash.restore(good, self.port)
        (self.dir / "restore.log").write_text(out, encoding="utf-8")
        good_report, good_notes = self.boot(f"lastgood-{good}")
        notes += [f"restored last-good {good} (exit {code})"] + good_notes
        report["last_good"] = {"label": good, "verdict": good_report.pop("verdict"),
                               "ip": good_report["ip"], "problems": good_report["problems"]}
        if good_report["ip"]:
            self.ip = None  # the board now runs last-good: later steps would test the wrong image
            return self.record("B2", "FAIL", notes=notes, **report)
        return self.record("B2", "INVALID", notes=notes, **report)

    def last_good(self) -> str | None:
        dirs = [d for d in common.GOOD_DIR.glob("*")
                if d.is_dir() and not d.name.endswith(".tmp") and d.name != self.a.label]
        if not dirs:
            return None
        return max(dirs, key=lambda d: d.stat().st_mtime).name

    # --- B3..B5 --------------------------------------------------------------

    def b3(self) -> str:
        verdict, report = compare_selftest.run_selftest(self.ip, self.dir / "selftest.json",
                                                        self.a.tree)
        report.pop("verdict", None)
        return self.record("B3", verdict, **report)

    def b4(self) -> str:
        report = walk_check.walk_check(self.ip, self.dir / "walk", self.a.tree)
        return self.record("B4", report.pop("verdict"), **report)

    def b4b(self) -> str:
        report = ui_models_check.check(self.ip, self.dir / "b4b", self.a.tree)
        return self.record("B4b", report.pop("verdict"), **report)

    def b5(self) -> str:
        report = scenario_drive.scenario(self.ip, self.dir / "drive", self.a.tree)
        return self.record("B5", report.pop("verdict"), **report)

    def sim_mode_step(self, step: str) -> str:
        report = scenario_hazards.run_step(step, self.ip, self.dir / "hazards" / step.lower(),
                                           self.a.tree)
        return self.record(step, report.pop("verdict"), **report)


def only_ip_missing(report: dict) -> bool:
    return all("got_ip" in p for p in report["problems"])


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--build-dir", type=pathlib.Path, required=True)
    p.add_argument("--label", required=True)
    p.add_argument("--flash", action="store_true")
    p.add_argument("--steps", default=",".join(ALL_STEPS))
    p.add_argument("--no-save", action="store_true",
                   help="never save this build as last-good (drafts that must not stay on the board)")
    p.add_argument("--tree", type=pathlib.Path, default=common.REPO,
                   help="where scripts/ (selftest, sim, hmi_ui) are run from")
    p.add_argument("--ip", default=None, help="board IP when B2 is not in --steps")
    p.add_argument("--sim-mode-steps", action="store_true",
                   help=f"also run {','.join(SIM_MODE_STEPS)} after B5 (scenario_hazards.py: "
                        "the sim's fault modes; not in the default list until run on the board)")
    a = p.parse_args()
    sequence = ALL_STEPS + (SIM_MODE_STEPS if a.sim_mode_steps else [])
    if a.sim_mode_steps and a.steps == p.get_default("steps"):
        a.steps = ",".join(sequence)
    by_upper = {s.upper(): s for s in ALL_STEPS + SIM_MODE_STEPS}
    steps = [by_upper.get(s.strip().upper(), s.strip()) for s in a.steps.split(",") if s.strip()]
    if not a.sim_mode_steps and any(s in SIM_MODE_STEPS for s in steps):
        p.error(f"{','.join(SIM_MODE_STEPS)} run only with --sim-mode-steps")
    if "B0" not in steps and (a.flash or "B1" in steps):
        p.error("flashing needs the B0 preflight")

    owner = f"run_bench:{a.label}"
    try:
        common.log(f"lease: {lease.acquire(owner)}")
    except lease.LeaseHeld as e:
        print(e)
        return 3
    os.environ["BENCH_LEASE_OWNER"] = owner
    run = Run(a)
    # The board's address is DHCP-assigned (.180 overnight, .218 since 11:31):
    # when B2 runs, the IP comes only from its boot log, never from --ip or a
    # stored value.
    if "B2" in steps and a.ip:
        p.error("--ip is only for runs without B2: B2 takes the IP from the boot log")
    run.ip = a.ip
    order = {"B0": run.b0, "B1": run.b1, "B2": run.b2, "B3": run.b3, "B4": run.b4, "B4b": run.b4b,
             "B5": run.b5}
    for step in SIM_MODE_STEPS:
        order[step] = lambda step=step: run.sim_mode_step(step)
    try:
        stop_reason = None
        for step in sequence:
            if step not in steps:
                run.record(step, "SKIP", reason="not in --steps")
                continue
            if stop_reason:
                run.record(step, "NOT_RUN", reason=stop_reason)
                continue
            if step in ("B1", "B2") and run.port is None:
                run.port = board.find_port()
            if step in ("B3", "B4", "B4b", "B5", *SIM_MODE_STEPS) and not run.ip:
                run.record(step, "NOT_RUN", reason="no board IP (B2 did not pass)")
                continue
            try:
                verdict = order[step]()
            except Exception as e:  # a crash in the runner is not a verdict on the firmware
                run.record(step, "NOT_RUN", reason=f"runner error: {e}",
                           traceback=traceback.format_exc()[-2000:])
                stop_reason = f"runner error in {step}"
                continue
            if step == "B0" and verdict == "INVALID":
                stop_reason = "B0 preflight INVALID"
            if step == "B1" and verdict in ("FAIL", "INVALID"):
                stop_reason = f"B1 {verdict}"
    finally:
        run.summary["finished"] = common.now_iso()
        run.summary["hotspot_restarts"] = run.hotspot_restarts
        verdicts = [s["verdict"] for s in run.summary["steps"].values()]
        run.summary["verdict"] = ("FAIL" if "FAIL" in verdicts else
                                  "INVALID" if "INVALID" in verdicts else
                                  "INCOMPLETE" if "NOT_RUN" in verdicts else "PASS")
        # RECORD (a characterisation step, B5e) passes the run but is not PASS.
        # Last-good only when every step passed (B0..B5 all run and PASS).
        if a.flash and not a.no_save and verdicts and all(v == "PASS" for v in verdicts):
            run.summary["saved_last_good"] = str(flash.save(a.build_dir, a.label, "B0-B5 PASS"))
        run.save()
        common.log(f"lease: {lease.release(owner)}")
    print(json.dumps({k: v["verdict"] for k, v in run.summary["steps"].items()}, indent=1))
    print(f"board_ip {run.summary.get('board_ip')}  summary {run.dir / 'summary.json'}")
    return {"PASS": 0, "FAIL": 1, "INVALID": 2, "INCOMPLETE": 1}[run.summary["verdict"]]


if __name__ == "__main__":
    sys.exit(main())
