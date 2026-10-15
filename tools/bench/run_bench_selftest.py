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
    def __init__(self, comes_back: bool = True, ip_after_reset: str = IP,
                 settings: str | None = None, changes: tuple[str, ...] = ()):
        self.settings = settings     # the saved settings line its boots print (None: none)
        self.changes = changes       # app steps that change a saved setting (brightness -10)
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
                     *([f"[settings/I][7.872]: {self.settings}"] if self.settings else []),
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
        if name in self.changes and self.settings:
            self.settings = self.settings.replace("brightness 80", "brightness 70")
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
        def analyse(text, baseline, **k):
            hits = board_settings(text)
            return {"verdict": "PASS", "problems": [], "ip": fake.ip,
                    "markers": [{"name": "settings_loaded", "value": h} for h in hits[:1]]}
        patch(run_bench.boot_check, "analyse", analyse)
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


def board_settings(text: str) -> list[str]:
    return [v for _, v in run_bench.boot_check.find(text.splitlines(), run_bench.SETTINGS_RE)]


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

def t_boot_check_variant_rows() -> None:
    import boot_check
    baseline = (common.BASELINE_DIR / "boot-board2.log").read_text(encoding="utf-8",
                                                                    errors="replace")
    settings_re = boot_check.MARKERS[0][1]
    (_, base_settings), = boot_check.find(baseline.splitlines(), settings_re)[:1]
    wanted = boot_check.OVERRIDES.get("settings_loaded", {}).get("value", base_settings)
    plain = boot_check.selftest_count(common.REPO)
    log = baseline.replace(base_settings, wanted)
    guard = common.variant_checks(None, common.REPO, {"CONFIG_HMI_BENCH_ALLOC_GUARD": "1"})
    expect("the guard's rows come from the tree's Need::BENCH rows", sorted(guard),
           ["mem.alloc_adc", "mem.alloc_lvgl", "mem.alloc_rtps"])
    expect("report-only per T12", [n for n, v in guard.items() if v["report_only"]],
           ["mem.alloc_adc"])
    expect("option off or absent: no rows",
           (common.variant_checks(None, common.REPO, {"CONFIG_HMI_BENCH_ALLOC_GUARD": "0"}),
            common.variant_checks(None, common.REPO, {})), ({}, {}))
    guard_log = log.replace(f"ready: {plain} checks", f"ready: {plain + 3} checks")
    saved = common.LINK
    try:
        common.LINK = "WiFi"
        expect("plain image, no variant: PASS", boot_check.analyse(log, baseline)["problems"], [])
        report = boot_check.analyse(guard_log, baseline, variant=guard)
        expect("guard image, guard variant: PASS with a finding",
               (report["problems"], any("+3 rows" in f for f in report["findings"])), ([], True))
        expect("guard image read as plain: selftest_ready fails", [
            p.split(":")[0] for p in boot_check.analyse(guard_log, baseline)["problems"]],
            ["selftest_ready"])
        expect("plain image with the variant claimed: selftest_ready fails", [
            p.split(":")[0] for p in boot_check.analyse(log, baseline, variant=guard)["problems"]],
            ["selftest_ready"])
    finally:
        common.LINK = saved
    # additive: a tree count the hazard fixes declare (57 with C4) plus the guard's rows
    text, problems, findings = boot_check.expected_selftest_ready(
        f"ready: {plain} checks (selftest_spec.h)", 57, guard)
    expect("C4's 57 plus the guard: 60 expected, no problem",
           (text, problems, len(findings)), ("ready: 60 checks (selftest_spec.h)", [], 2))
    with tempfile.TemporaryDirectory() as tmp:
        cfg = pathlib.Path(tmp) / "config"
        cfg.mkdir()
        (cfg / "sdkconfig.h").write_text("#define CONFIG_HMI_REMOTE_UI 1\n"
                                         "#define CONFIG_HMI_BENCH_ALLOC_GUARD 1\n",
                                         encoding="utf-8")
        expect("read from the build dir's sdkconfig.h",
               sorted(common.variant_checks(pathlib.Path(tmp), common.REPO)),
               ["mem.alloc_adc", "mem.alloc_lvgl", "mem.alloc_rtps"])


SETTINGS = ("loaded: brightness 80, theme 0, menu_slide 0, flip 0, stick_sensitivity 9, "
            "drive_speed 10, stick_invert_x 0, stick_invert_y 0, stick_swap 0, sounds 1, network 1")


def t_settings_guard() -> None:
    summary, log = bench(FakeBoard(settings=SETTINGS), "B0,B2,B5", ip=None)
    expect("unchanged: SETTINGS PASS after a closing reset", (
        verdicts(summary), log[-2:], run_bench_verdict(summary)),
        ({"B0": "PASS", "B2": "PASS", "B5": "PASS", "SETTINGS": "PASS"},
         ["reset:B2", "capture:reset=True"], "PASS"))
    summary, log = bench(FakeBoard(settings=SETTINGS, changes=("B5",)), "B0,B2,B5", ip=None)
    g = summary["steps"]["SETTINGS"]
    expect("changed by a step: a rig fault, INVALID, both lines in the record",
           (g["verdict"], g["reason"].startswith("rig fault"), "brightness 80" in g["before"],
            "brightness 70" in g["after"], run_bench_verdict(summary)),
           ("INVALID", True, True, True, "INVALID"))
    summary, log = bench(FakeBoard(settings=SETTINGS), "B0,B2", ip=None)
    expect("nothing ran after B2: no closing reset", ("SETTINGS" in summary["steps"],
                                                      log.count("capture:reset=True")), (False, 1))
    expect("no line after the reset: NOT_RUN", run_bench.settings_verdict(SETTINGS, None)[0],
           "NOT_RUN")


# --- platform switches (Windows and Linux) ---------------------------------------------------

def _env_value(var: str, value: str, expr: str) -> str:
    """`expr` evaluated in a fresh python with `var` set (module-level defaults are read at
    import)."""
    import subprocess
    code = (f"import sys; sys.path.insert(0, {str(common.HERE)!r}); import common, lease; "
            f"print({expr})")
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1", **{var: value})
    out = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True,
                         timeout=60, env=env)
    if out.returncode != 0:
        raise AssertionError(f"{var}={value}: {out.stderr.strip()[-300:]}")
    return out.stdout.strip()


def t_platform_defaults() -> None:
    home = pathlib.Path.home()
    expect("BENCH_HOME on Windows", common.default_bench_home(True), r"C:\b\bench")
    expect("BENCH_HOME on Linux", common.default_bench_home(False), str(home / "bench"))
    expect("venv python on Windows", common.default_venv_py(True),
           r"C:\Espressif\tools\python\v6.0\venv\Scripts\python.exe")
    expect("venv python on Linux", common.default_venv_py(False),
           str(home / ".espressif/tools/python/v6.0/venv/bin/python"))
    expect("lease on Windows", common.default_lease(True),
           r"C:\Users\halai\Offline_Documents\ATDev\rammp\.board-lease")
    expect("lease on Linux", common.default_lease(False),
           str(home / "Documents/ATDev/rammp/.board-lease"))
    expect("this platform's defaults are in use (no override set)",
           (_env_value("BENCH_HOME", "", "common.BENCH_HOME"),
            _env_value("BENCH_LEASE", "", "lease.LEASE"),
            _env_value("BENCH_VENV_PY", "", "common.VENV_PY")),
           (str(pathlib.Path(common.default_bench_home())),
            str(pathlib.Path(common.default_lease())), common.default_venv_py()))
    tmp = str(pathlib.Path(tempfile.gettempdir()) / "bench-x")
    expect("BENCH_HOME overrides", _env_value("BENCH_HOME", tmp, "common.RESULTS_DIR"),
           str(pathlib.Path(tmp) / "results"))
    expect("BENCH_LEASE overrides", _env_value("BENCH_LEASE", tmp, "lease.LEASE"), tmp)
    expect("BENCH_VENV_PY overrides", _env_value("BENCH_VENV_PY", tmp, "common.VENV_PY"), tmp)
    saved = common._has
    try:
        common._has = lambda module: False
        expect("no pyserial/esptool here: the venv runs children and esptool",
               (common.python(), common.esptool_python()), (common.VENV_PY, common.VENV_PY))
        common._has = lambda module: True
        expect("both here: this interpreter", (common.python(), common.esptool_python()),
               (sys.executable, sys.executable))
    finally:
        common._has = saved
    for bad in (["erase-flash"], ["erase_region", "0x0", "0x1000"], ["-e"], ["--erase-all"]):
        try:
            common.esptool(["--chip", "esp32p4", *bad])
        except SystemExit:
            continue
        raise AssertionError(f"esptool {bad} was not refused")


def t_process_table_parsers() -> None:
    expect("stat: a comm with spaces and parentheses",
           common.parse_proc_stat("4242 (py (x) y) S 17 4242 4242 0 -1 4194304"), ("py (x) y", 17))
    ps = ("    1     0 /sbin/init splash\n"
          "  100     1 /usr/bin/python3 /w/scripts/rtps_mcb_sim.py --peer 1.2.3.4\n"
          "  101   100 python3.12 -c pass\n"
          "  102     1 bash -c sleep 9\n"
          "  103     1 [kworker/0:1]\n"
          "  104     1\n")
    rows = common.parse_ps(ps)
    expect("ps rows", [(pid, ppid, name) for pid, ppid, name, _ in rows],
           [(1, 0, "/sbin/init"), (100, 1, "/usr/bin/python3"), (101, 100, "python3.12"),
            (102, 1, "bash"), (103, 1, "[kworker/0:1]"), (104, 1, "")])
    expect("python by name or argv[0]",
           [common.is_python(n, a) for n, a in (
               ("python3", ""), ("python.exe", ""), ("rtps_mcb_sim.p", "/usr/bin/python3"),
               ("bash", "bash"), ("", r"C:\v\Scripts\python.exe"), ("pythonw.exe", ""),
               ("ipython", "ipython"))],
           [True, True, True, False, True, True, False])
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        procs = {10: (1, "python", [".venv/bin/python", "sim_child.py", "--tree", "T"]),
                 11: (10, "python3", ["python3", "-c", "pass"]),
                 12: (11, "sleep", ["sleep", "9"]),
                 20: (1, "python3", ["/usr/bin/python3", "/w/rtps_mcb_sim.py", "--peer", "x"]),
                 30: (1, "bash", ["bash", "-c", "rtps_selftest.py"])}
        for pid, (ppid, comm, argv) in procs.items():
            d = root / str(pid)
            d.mkdir()
            (d / "stat").write_text(f"{pid} ({comm}) S {ppid} 1 1 0 -1 0\n")
            (d / "cmdline").write_bytes(b"\0".join(a.encode() for a in argv) + b"\0")
        (root / "40").mkdir()  # a process gone between the listing and the read
        (root / "self").mkdir()
        rows = common._table_proc(root)
        full = sorted((pid, ppid, cmd) for pid, ppid, _, cmd in rows)
        expect("every /proc entry that could be read", [r[0] for r in full], [10, 11, 12, 20, 30])
        expect("the command line is argv joined", full[0][2], ".venv/bin/python sim_child.py --tree T")
        table = [(pid, ppid, cmd) for pid, ppid, name, cmd in rows
                 if common.is_python(name, cmd.split(None, 1)[0])]
        expect("python rows only", sorted(r[0] for r in table), [10, 11, 20])
        expect("the whole tree of 10 (from the full table)",
               common.descendants({10}, [(p, pp, c) for p, pp, _, c in rows]), {10, 11, 12})
        strays = common.stray_peers(exclude_trees={10}, table=table)
        expect("this run's sim is not a stray; the other sim is; bash is not python",
               [x.split(":")[0] for x in strays], ["20"])
    live = common.process_table(only_python=True)
    me = [r for r in live if r[0] == os.getpid()]
    expect("the live table has this process with its parent", [(r[0], r[1]) for r in me],
           [(os.getpid(), os.getppid())])


def t_kill_tree() -> None:
    if common.WINDOWS:
        return  # taskkill /T: covered on the bench PC by the runner itself
    import subprocess
    import time
    code = ("import subprocess, sys, time; "
            "p = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)']); "
            "print(p.pid, flush=True); time.sleep(60)")
    child = subprocess.Popen([sys.executable, "-c", code], stdout=subprocess.PIPE, text=True)
    try:
        grandchild = int(child.stdout.readline())
        out = common.kill_tree(child.pid)
        expect("both killed", out.startswith("SIGKILL ") and str(grandchild) in out
               and str(child.pid) in out, True)
        child.wait(timeout=10)

        def gone(pid: int) -> bool:
            try:
                state = pathlib.Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()[0]
                return state in ("Z", "X")
            except (OSError, IndexError):
                return True
        deadline = time.monotonic() + 10
        while not gone(grandchild) and time.monotonic() < deadline:
            time.sleep(0.05)
        expect("the grandchild is gone too", gone(grandchild), True)
    finally:
        if child.poll() is None:
            child.kill()


def t_hotspot_off_windows() -> None:
    saved = common.WINDOWS
    try:
        common.WINDOWS = False
        code, out = run_bench.Run.hotspot(None, "status")
        expect("no hotspot off Windows: exit 2, not 'STATE On' (B0's tethering is INVALID)",
               (code, "STATE On" in out, "BENCH_NET=lan:" in out), (2, False, True))
    finally:
        common.WINDOWS = saved
    saved_h = common.HOTSPOT, common.NET
    try:
        common.HOTSPOT, common.NET = False, "lan:192.168.1.193/24"
        run = run_bench.Run.__new__(run_bench.Run)
        run.hotspot_restarts = 0
        expect("BENCH_NET=lan: no restart, nothing called",
               (run.restart_hotspot_once(), run.hotspot_restarts),
               ("not restarted: BENCH_NET=lan:192.168.1.193/24 has no hotspot", 0))
    finally:
        common.HOTSPOT, common.NET = saved_h
    with fake_bench(FakeBoard()):
        saved = common.HOTSPOT
        try:
            common.HOTSPOT = False
            a = argparse.Namespace(label="selftest", build_dir=pathlib.Path("."), flash=False,
                                   tree=common.REPO, no_save=True, ip=IP)
            run = run_bench.Run(a)
            run.hotspot = lambda action: (_ for _ in ()).throw(AssertionError("hotspot called"))
            expect("B0 on a LAN never calls the hotspot", run.b0(), "PASS")
            t = [c for c in run.summary["steps"]["B0"]["checks"] if c["name"] == "tethering"]
            expect("tethering not applicable", t[0]["detail"].startswith("not applicable"), True)
        finally:
            common.HOTSPOT = saved


def t_open_lines() -> None:
    """The line states an open walks through. Windows: nothing changes at open. Linux:
    cdc_acm raises both, then pyserial writes DTR, then RTS, then _open lowers DTR. The
    USB-Serial/JTAG resets the chip on RTS high with DTR low."""
    def walk(windows: bool) -> list[tuple[bool, bool]]:
        dtr, rts = board.open_lines(windows)
        states = [(False, False)] if windows else [(True, True)]  # (DTR, RTS) after open
        for line in (("dtr", dtr), ("rts", rts)):  # pyserial's order
            d, r = states[-1]
            states.append((line[1], r) if line[0] == "dtr" else (d, line[1]))
        if dtr:
            states.append((False, states[-1][1]))
        return states
    expect("Windows: both low before open", board.open_lines(True), (False, False))
    expect("Linux: DTR high, RTS low before open, DTR low after", board.open_lines(False),
           (True, False))
    for windows in (True, False):
        states = walk(windows)
        expect(f"{'Windows' if windows else 'Linux'}: never RTS high with DTR low (a reset)",
               [s for s in states if s == (False, True)], [])
        expect(f"{'Windows' if windows else 'Linux'}: ends with both low", states[-1],
               (False, False))


def _rtps_modules():
    sys.path.insert(0, str(common.REPO / "scripts"))
    import rtps_host
    import rtps_net
    return rtps_host, rtps_net


def t_send_or_drop() -> None:
    import errno
    rtps_host, _ = _rtps_modules()

    class Sock:
        def __init__(self, exc):
            self.exc = exc

        def sendto(self, payload, target):
            if self.exc is not None:
                raise self.exc
    send = rtps_host.RtpsHostHarness._send_or_drop
    expect("sent", send(Sock(None), b"x", ("1.2.3.4", 7400)), True)
    expect("EAGAIN (a full send buffer, Linux's ARP queue): dropped",
           send(Sock(BlockingIOError(errno.EAGAIN, "again")), b"x", ("1.2.3.4", 7400)), False)
    expect("ENOBUFS: dropped", send(Sock(OSError(errno.ENOBUFS, "nobufs")), b"x",
                                    ("1.2.3.4", 7400)), False)
    try:
        send(Sock(OSError(errno.EPERM, "denied")), b"x", ("1.2.3.4", 7400))
    except OSError:
        pass
    else:
        raise AssertionError("any other send error must still raise")


def t_sweep_listener_died() -> None:
    _, rtps_net = _rtps_modules()

    class Dead:
        def __init__(self, args, targets):
            self.responders = ["192.168.1.135"]
            self.first_response = __import__("threading").Event()

        def run(self):
            raise RuntimeError("the receive thread died")  # noqa: TRY002 - the fault modelled

        def stop(self):
            pass
    saved = rtps_net._Listener, rtps_net._listener_args
    import threading
    hook = threading.excepthook
    try:
        rtps_net._Listener = Dead
        rtps_net._listener_args = lambda *a: None
        threading.excepthook = lambda args: None  # the modelled crash, not noise
        expect("not strict: what it heard (as before)",
               rtps_net._run_listener(["192.168.1.135"], 2.0, "127.0.0.1", False), ["192.168.1.135"])
        try:
            rtps_net._run_listener(["192.168.1.135"], 2.0, "127.0.0.1", False, strict=True)
        except rtps_net.ListenerDied:
            pass
        else:
            raise AssertionError("strict: a dead listener must raise")
        saved_net = common.SUBNET, common.PC_IP, common.SWEEP_SKIP
        try:
            common.SUBNET, common.PC_IP, common.SWEEP_SKIP = "192.168.1.0/30", "192.168.1.1", ()
            import hazard_rig
            try:
                hazard_rig.rtps_sweep(common.REPO)
            except rtps_net.ListenerDied:
                pass
            else:
                raise AssertionError("the bench sweep (B0, sole-sim proof) must be strict")
        finally:
            common.SUBNET, common.PC_IP, common.SWEEP_SKIP = saved_net
    finally:
        rtps_net._Listener, rtps_net._listener_args = saved
        threading.excepthook = hook


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
    ("BENCH-072 B2 with a build variant: the alloc guard's config adds the tree's Need::BENCH "
     "rows to the self-test count, on top of the tree's (and C4's) count; read from the build "
     "dir's sdkconfig.h", t_boot_check_variant_rows),
    ("BENCH-071 SETTINGS: a run whose steps change a saved setting ends INVALID (rig fault) "
     "after a closing reset; unchanged is PASS; no step after B2, no reset", t_settings_guard),
    ("BENCH-073 platform defaults: BENCH_HOME, the venv python and the lease file per platform, "
     "each overridden by its environment variable; the interpreters for children and esptool; "
     "erase options refused", t_platform_defaults),
    ("BENCH-074 the process table on Linux: /proc stat and cmdline (and ps) parsed, python rows "
     "by comm or argv[0], the whole-tree exclusion of this run's sim, this process in the live "
     "table", t_process_table_parsers),
    ("BENCH-075 kill_tree off Windows kills the process and its grandchild (SIGKILL each)",
     t_kill_tree),
    ("BENCH-076 the hotspot off Windows: exit 2 (BENCH_NET=hotspot's B0 tethering is INVALID); "
     "BENCH_NET=lan never calls or restarts it", t_hotspot_off_windows),
    ("BENCH-077 opening the port never passes RTS high with DTR low (the chip's reset): both "
     "low on Windows; DTR high at open, then low, on Linux (cdc_acm raises both)",
     t_open_lines),
    ("BENCH-078 RTPS sends: a full send buffer (EAGAIN, ENOBUFS: Linux queues datagrams to "
     "unresolved ARP) drops the datagram instead of killing the receive thread; other errors "
     "raise", t_send_or_drop),
    ("BENCH-079 the bench's RTPS sweep is strict: a listener that dies early raises "
     "(B0 INVALID, no sole-sim proof) instead of passing on what it heard", t_sweep_listener_died),
]


def main() -> int:
    import hazard_selftest  # BENCH-015..: the hazard fixes' bench steps
    import ui_client  # its Unity-format case runner
    return ui_client.run_cases("tools/bench/run_bench_selftest.py",
                               CASES + hazard_selftest.CASES)


if __name__ == "__main__":
    sys.exit(main())
