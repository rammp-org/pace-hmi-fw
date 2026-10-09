"""run_bench.py's step sequencing against a fake board: no port, no network, no lease.

    python tools/bench/run_bench.py selftest        (or this file directly)

The fake board models what matters here: esptool's `--after hard-reset` (B0's table
read, B1's backup and flash) reboots it, a reboot prints a boot log ending in `Got IP`
on the serial port, the remote UI answers PING only once it is up, and every app step
(B3..B5e) is graded PASS only when the board was up at its IP when it ran. Cases
BENCH-001.., Unity's output format. Stdlib only (runs in CI's python3).
"""

from __future__ import annotations

import argparse
import contextlib
import os
import pathlib
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import board  # noqa: E402
import common  # noqa: E402
import run_bench  # noqa: E402

IP = "192.168.137.218"


class FakeBoard:
    def __init__(self, comes_back: bool = True, ip_after_reset: str = IP):
        self.up = True
        self.ip = IP
        self.comes_back = comes_back
        self.ip_after_reset = ip_after_reset
        self.log: list[str] = []     # what happened, in order

    def reset(self, why: str) -> None:
        self.up = False
        self.log.append(f"reset:{why}")

    def capture(self, port, seconds, reset=False, on_line=None):
        cap = board.Capture()
        if reset:
            self.reset("B2")
        self.log.append(f"capture:reset={reset}")
        if not self.up and self.comes_back:
            self.ip = self.ip_after_reset
            lines = ["ESP-ROM:esp32p4", "Calling app_main()",
                     f"[rtps_comms/I][27.158]: Got IP {self.ip} (gateway 192.168.137.2)",
                     "after Got IP"]
            for line in lines:
                cap.lines.append((0.0, line))
                if on_line is not None and on_line(cap, line):
                    break
            self.up = True
        return cap

    def ping(self, ip, within, **_):
        ok = self.up and ip == self.ip
        self.log.append(f"ping:{ok}")
        return ok, "PING OK" if ok else "no PING answer"

    def app_step(self, name: str, ip: str) -> dict:
        ok = self.up and ip == self.ip
        self.log.append(f"{name}:{'up' if ok else 'BOOTING'}")
        return {"verdict": "PASS" if ok else "FAIL",
                "problems": [] if ok else [f"{name} ran while the board was not up at {ip}"]}


@contextlib.contextmanager
def fake_bench(fake: FakeBoard, backup_made: bool = False):
    """Patch every board, network and child-process touch point of run_bench."""
    saved = []

    def patch(obj, name, value):
        saved.append((obj, name, getattr(obj, name)))
        setattr(obj, name, value)

    with tempfile.TemporaryDirectory() as tmp:
        patch(common, "RESULTS_DIR", pathlib.Path(tmp))
        patch(common, "PC_IP", "127.0.0.1")
        patch(common, "log", lambda message: None)
        patch(common, "stray_peers", lambda *a, **k: [])
        patch(board, "find_port", lambda *a, **k: "COMFAKE")
        patch(board, "capture", fake.capture)

        def compare(port, build_dir, keep=None):
            fake.reset("B0")
            return True, "equal"
        patch(board, "compare_partition_table", compare)

        def backup(port):
            if backup_made:
                fake.reset("B1")
            return pathlib.Path(tmp) / "storage.bin", backup_made
        patch(board, "backup_storage", backup)
        patch(run_bench.Run, "hotspot", lambda self, action: (0, "STATE On"))
        patch(run_bench.Run, "rtps_sweep", lambda self: [fake.ip])
        patch(run_bench.boot_check, "analyse",
              lambda text, baseline, **k: {"verdict": "PASS", "problems": [], "ip": fake.ip})
        patch(run_bench.ui_client, "ping", fake.ping)
        patch(run_bench.compare_selftest, "run_selftest",
              lambda ip, out, tree: (fake.app_step("B3", ip)["verdict"], {}))
        patch(run_bench.walk_check, "walk_check", lambda ip, out, tree: fake.app_step("B4", ip))
        patch(run_bench.ui_models_check, "check", lambda ip, out, tree: fake.app_step("B4b", ip))
        patch(run_bench.scenario_drive, "scenario", lambda ip, out, tree: fake.app_step("B5", ip))
        patch(run_bench.scenario_hazards, "run_step",
              lambda step, ip, out, tree: fake.app_step(step, ip))
        try:
            yield
        finally:
            for obj, name, value in reversed(saved):
                setattr(obj, name, value)


def bench(fake: FakeBoard, steps: str, ip: str | None = IP,
          backup_made: bool = False) -> tuple[dict, list[str]]:
    with fake_bench(fake, backup_made):
        a = argparse.Namespace(label="selftest", build_dir=pathlib.Path("."), flash=False,
                               tree=common.REPO, no_save=True, ip=ip)
        sequence, wanted = run_bench.plan_steps(steps)
        run = run_bench.Run(a)
        run.ip = ip
        run_bench.run_steps(run, sequence, wanted)
        return run.summary, fake.log


def verdicts(summary: dict) -> dict:
    return {k: v["verdict"] for k, v in summary["steps"].items()
            if v["verdict"] != "SKIP"}


def expect(what: str, got: object, want: object) -> None:
    if got != want:
        raise AssertionError(f"{what}: got {got!r}, want {want!r}")


def t_b0_then_b5_waits() -> None:
    summary, log = bench(FakeBoard(), "B0,B5")
    expect("verdicts", verdicts(summary), {"B0": "PASS", "B5": "PASS"})
    expect("the boot is captured without a reset before B5",
           log, ["reset:B0", "capture:reset=False", "ping:True", "B5:up"])
    back = summary["board_back"]
    expect("recorded", (len(back), back[0]["after"], back[0]["before"], back[0]["got_ip"],
                        back[0]["back"]),
           (1, "B0: partition-table read", "B5", IP, True))


def t_b2_boots_it_no_extra_wait() -> None:
    summary, log = bench(FakeBoard(), "B0,B2,B5", ip=None)
    expect("verdicts", verdicts(summary), {"B0": "PASS", "B2": "PASS", "B5": "PASS"})
    expect("only B2's capture", log, ["reset:B0", "reset:B2", "capture:reset=True", "B5:up"])
    expect("no board_back", "board_back" in summary, False)


def t_b1_backup_then_app_steps() -> None:
    summary, log = bench(FakeBoard(), "B0,B1,B3,B4,B5", backup_made=True)
    expect("verdicts", verdicts(summary), {"B0": "PASS", "B3": "PASS", "B4": "PASS", "B5": "PASS"})
    expect("B1 ran (backup) and is SKIP without --flash",
           (summary["steps"]["B1"]["verdict"], summary["steps"]["B1"]["backup_made_now"]),
           ("SKIP", True))
    expect("one wait, after the last reset, before the first app step", log,
           ["reset:B0", "reset:B1", "capture:reset=False", "ping:True", "B3:up", "B4:up",
            "B5:up"])
    expect("the wait names B1", summary["board_back"][0]["after"], "B1: storage backup read")


def t_never_back() -> None:
    summary, log = bench(FakeBoard(comes_back=False), "B0,B5,B5a")
    v = verdicts(summary)
    expect("verdicts", v, {"B0": "PASS", "B5": "NOT_RUN", "B5a": "NOT_RUN"})
    expect("reason", "did not come back" in summary["steps"]["B5"]["reason"], True)
    expect("no app step ran on a booting board", [x for x in log if ":" in x and
                                                  x.split(":")[0].startswith("B5")], [])
    expect("run verdict", run_bench_verdict(summary), "INCOMPLETE")


def run_bench_verdict(summary: dict) -> str:
    vs = [s["verdict"] for s in summary["steps"].values()]
    return ("FAIL" if "FAIL" in vs else "INVALID" if "INVALID" in vs else
            "INCOMPLETE" if "NOT_RUN" in vs else "PASS")


def t_new_ip_from_boot_log() -> None:
    summary, log = bench(FakeBoard(ip_after_reset="192.168.137.77"), "B0,B5")
    expect("B5 ran against the new address", verdicts(summary)["B5"], "PASS")
    expect("recorded", (summary["board_ip"], "ip_changed" in summary["board_back"][0]),
           ("192.168.137.77", True))


def t_ip_from_the_wait() -> None:
    summary, log = bench(FakeBoard(), "B0,B5,B5a", ip=None)
    expect("verdicts", verdicts(summary), {"B0": "PASS", "B5": "PASS", "B5a": "PASS"})
    expect("the IP is the boot log's", (summary["board_ip"], summary["board_back"][0]["got_ip"]),
           (IP, IP))
    expect("one wait, then the steps", log, ["reset:B0", "capture:reset=False", "ping:True",
                                             "B5:up", "B5a:up"])


def t_no_ip_anywhere() -> None:
    summary, _ = bench(FakeBoard(), "B5", ip=None)
    expect("no reset, no --ip: NOT_RUN", verdicts(summary), {"B5": "NOT_RUN"})
    expect("reason", "no board IP" in summary["steps"]["B5"]["reason"], True)


def _record(checks: list[bool], problems: list[str] | None = None) -> dict:
    return {"verdict": "RECORD", "checks": [{"check": f"c{i}", "ok": ok}
                                             for i, ok in enumerate(checks)],
            "problems": problems if problems is not None else
            [f"c{i}" for i, ok in enumerate(checks) if not ok]}


def t_last_good_clean_record_saves() -> None:
    passed = {s: {"verdict": "PASS"} for s in ("B0", "B1", "B2", "B5")}
    expect("all PASS", run_bench.last_good_decision(passed)[0], True)
    expect("PASS + a clean RECORD", run_bench.last_good_decision(
        {**passed, "B5e": _record([True, True, True])})[0], True)
    # a step's record as run_bench stores it: the clean RECORD keeps its checks
    fake = FakeBoard()
    real = fake.app_step
    fake.app_step = lambda name, ip: (_record([True]) if name == "B5e" else real(name, ip))
    summary, _ = bench(fake, "B5,B5e")
    ran = {k: v for k, v in summary["steps"].items() if k in ("B5", "B5e")}
    expect("run", (verdicts(summary), run_bench.last_good_decision(ran)[0]),
           ({"B5": "PASS", "B5e": "RECORD"}, True))


def t_last_good_dirty_record_does_not() -> None:
    passed = {"B0": {"verdict": "PASS"}}
    for what, rec in (("a failed check", _record([True, False])),
                      ("a problem listed, checks ok", _record([True], ["clean-up: not locked"])),
                      ("no graded checks at all", _record([])),
                      ("a check without ok", {"verdict": "RECORD", "checks": [{"check": "x"}],
                                              "problems": []})):
        save, why = run_bench.last_good_decision({**passed, "B5e": rec})
        expect(what, (save, "B5e RECORD" in why), (False, True))


def t_last_good_fail_does_not() -> None:
    for verdict in ("FAIL", "INVALID", "NOT_RUN", "SKIP"):
        save, why = run_bench.last_good_decision({"B0": {"verdict": "PASS"},
                                                  "B5": {"verdict": verdict},
                                                  "B5e": _record([True])})
        expect(verdict, (save, why), (False, f"B5 {verdict}"))
    expect("nothing ran", run_bench.last_good_decision({})[0], False)


def t_no_reset_no_wait() -> None:
    summary, log = bench(FakeBoard(), "B5,B5c")
    expect("verdicts", verdicts(summary), {"B5": "PASS", "B5c": "PASS"})
    expect("nothing but the steps", log, ["B5:up", "B5c:up"])


def t_plan() -> None:
    try:
        run_bench.plan_steps("B0,B6")
    except ValueError:
        pass
    else:
        raise AssertionError("an unknown step was accepted")
    expect("the default list ends with B5 then B5a..B5e", run_bench.plan_steps(None)[1],
           ["B0", "B1", "B2", "B3", "B4", "B4b", "B5", "B5a", "B5b", "B5c", "B5d", "B5e"])
    expect("case-insensitive", run_bench.plan_steps("b0,b4B,b5E")[1], ["B0", "B4b", "B5e"])



def t_net_spec() -> None:
    expect("hotspot", common._net("hotspot"),
           ("192.168.137.2", "192.168.137.0/24", "WiFi", ("192.168.137.1",)))
    expect("lan", common._net("lan:192.168.9.226/24"),
           ("192.168.9.226", "192.168.9.0/24", "Ethernet", ()))
    expect("routed", common._net("routed:100.92.133.114@10.0.0.0/24"),
           ("100.92.133.114", "10.0.0.0/24", "Ethernet", ()))
    for bad in ("lan:192.168.9.226/16", "lan:nonsense", "wifi", "routed:100.92.133.114",
                "routed:100.92.133.114@10.0.0.0/16", "routed:x@10.0.0.0/24"):
        try:
            common._net(bad)
        except (SystemExit, ValueError):
            continue
        raise AssertionError(f"BENCH_NET={bad!r} was accepted")


def t_boot_check_on_ethernet() -> None:
    import boot_check
    baseline = (common.BASELINE_DIR / "boot-board2.log").read_text(encoding="utf-8",
                                                                    errors="replace")
    settings_re = boot_check.MARKERS[0][1]
    (_, base_settings), = boot_check.find(baseline.splitlines(), settings_re)[:1]
    wanted = boot_check.OVERRIDES.get("settings_loaded", {}).get("value", base_settings)
    # A boot as board 2 logs it today, on each link.
    wifi_log = baseline.replace(base_settings, wanted)
    eth_log = wifi_log.replace(wanted, wanted[:-len("network 1")] + "network 0").replace(
        "Network: WiFi", "Network: Ethernet")
    saved = common.LINK
    try:
        common.LINK = "WiFi"
        expect("WiFi bench, WiFi boot", boot_check.analyse(wifi_log, baseline)["problems"], [])
        expect("WiFi bench, Ethernet boot", sorted(
            p.split(":")[0] for p in boot_check.analyse(eth_log, baseline)["problems"]),
            ["network_link", "settings_loaded"])
        common.LINK = "Ethernet"
        expect("Ethernet bench, Ethernet boot", boot_check.analyse(eth_log, baseline)["problems"],
               [])
        expect("Ethernet bench, WiFi boot", sorted(
            p.split(":")[0] for p in boot_check.analyse(wifi_log, baseline)["problems"]),
            ["network_link", "settings_loaded"])
    finally:
        common.LINK = saved

CASES = [
    ("BENCH-001 after B0's reset, B5 waits for the boot (Got IP) and PING before it runs",
     t_b0_then_b5_waits),
    ("BENCH-002 with B2 after B0, B2's own boot is the wait; nothing extra",
     t_b2_boots_it_no_extra_wait),
    ("BENCH-003 B1's backup read resets too; one wait before the first app step",
     t_b1_backup_then_app_steps),
    ("BENCH-004 a board that does not come back leaves the app steps NOT_RUN with the reason",
     t_never_back),
    ("BENCH-005 a different Got IP after the reset replaces --ip", t_new_ip_from_boot_log),
    ("BENCH-006 no reset, no wait: --steps B5,B5c --ip runs the steps alone", t_no_reset_no_wait),
    ("BENCH-007 the step plan: B5a..B5e in the default list, unknown names refused, any case",
     t_plan),
    ("BENCH-008 B0 then app steps with no --ip and no B2: the wait's Got IP gives the IP",
     t_ip_from_the_wait),
    ("BENCH-009 no reset and no --ip: the app steps are NOT_RUN, no IP", t_no_ip_anywhere),
    ("BENCH-010 last-good: every step PASS, or a RECORD with clean graded checks, saves",
     t_last_good_clean_record_saves),
    ("BENCH-011 last-good: a RECORD with a failed, listed or missing graded check does not save",
     t_last_good_dirty_record_does_not),
    ("BENCH-012 last-good: FAIL, INVALID, NOT_RUN or SKIP does not save", t_last_good_fail_does_not),
    ("BENCH-013 BENCH_NET: hotspot, lan:<pc ip>/<prefix>, routed:<pc ip>@<subnet>; "
     "anything else refused", t_net_spec),
    ("BENCH-014 on Ethernet B2 expects 'Network: Ethernet' and network 0, nothing else",
     t_boot_check_on_ethernet),
]


def main() -> int:
    import hazard_selftest  # BENCH-015..: the hazard fixes' bench steps
    import ui_client  # its Unity-format case runner
    return ui_client.run_cases("tools/bench/run_bench_selftest.py",
                               CASES + hazard_selftest.CASES)


if __name__ == "__main__":
    sys.exit(main())
