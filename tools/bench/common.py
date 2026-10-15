"""Shared constants and helpers for the bench runner (tools/bench).

Runtime data never goes in the repo: everything this runner writes lives under
BENCH_HOME (C:\\b\\bench on Windows, ~/bench on Linux; BENCH_HOME in the environment
overrides). Run every script with the IDF venv python, the only one with pyserial/esptool
(VENV_PY below; BENCH_VENV_PY in the environment overrides).

Platform switches (one place, each a pure function of the platform so the selftest covers
both: BENCH-073..): the defaults of BENCH_HOME, VENV_PY and the lease file, the python
process table (CIM on Windows, /proc or `ps` elsewhere), kill_tree (taskkill /T, or
SIGKILL to each process of the tree) and the hotspot (Windows Mobile Hotspot only).
"""

from __future__ import annotations

import datetime
import ipaddress
import json
import os
import pathlib
import shlex
import signal
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent.parent  # the worktree these tools live in
WINDOWS = sys.platform == "win32"


def default_bench_home(windows: bool = WINDOWS) -> str:
    """Where the runner writes when BENCH_HOME is unset."""
    return r"C:\b\bench" if windows else str(pathlib.Path.home() / "bench")


def default_venv_py(windows: bool = WINDOWS) -> str:
    """The ESP-IDF v6.0 venv's python (the one with pyserial and esptool)."""
    if windows:
        return r"C:\Espressif\tools\python\v6.0\venv\Scripts\python.exe"
    return str(pathlib.Path.home() / ".espressif" / "tools" / "python" / "v6.0" / "venv" / "bin"
               / "python")


def default_lease(windows: bool = WINDOWS) -> str:
    """The board lease file (TS-PAR-02), beside the workspace; BENCH_LEASE overrides."""
    if windows:
        return r"C:\Users\halai\Offline_Documents\ATDev\rammp\.board-lease"
    return str(pathlib.Path.home() / "Documents" / "ATDev" / "rammp" / ".board-lease")


BENCH_HOME = pathlib.Path(os.environ.get("BENCH_HOME") or default_bench_home())
GOOD_DIR = BENCH_HOME / "good"
RESULTS_DIR = BENCH_HOME / "results"
BASELINE_DIR = REPO / "tests" / "characterisation" / "baseline-e2047a4"
# Things learnt by a run that the repo's baseline does not hold (the walk's
# screen names were not saved with the baseline PNGs).
LOCAL_BASELINE_DIR = BENCH_HOME / "baseline"

BOARD_MAC = "80:F1:B2:D1:51:A6"

# The bench network. BENCH_NET unset (or "hotspot"): the PC's Mobile Hotspot, the PC at
# 192.168.137.2 and the board on WiFi. BENCH_NET="lan:<pc ip>/<prefix>", e.g.
# "lan:192.168.9.226/24": the board on Ethernet (its saved Connection setting, N1 = 0)
# in the same LAN as that PC address; no tethering is checked or restarted.
# BENCH_NET="routed:<pc ip>@<board subnet>", e.g. "routed:100.92.133.114@10.0.0.0/24": the
# board on Ethernet in a subnet the PC reaches through a router (a Tailscale subnet route);
# RTPS is unicast to the board, the sweep covers the board's subnet, and the round-trip
# times include the route (grade them against the reference image on the same route).
NET = os.environ.get("BENCH_NET", "hotspot")


def _net(spec: str) -> tuple[str, str, str, tuple[str, ...]]:
    """(PC_IP, SUBNET, LINK, addresses the RTPS sweep skips besides PC_IP)."""
    if spec == "hotspot":
        return "192.168.137.2", "192.168.137.0/24", "WiFi", ("192.168.137.1",)
    if spec.startswith("lan:"):
        iface = ipaddress.IPv4Interface(spec[4:])
        if iface.network.prefixlen < 22:
            raise SystemExit(f"BENCH_NET={spec}: the RTPS sweep covers at most a /22")
        return str(iface.ip), str(iface.network), "Ethernet", ()
    if spec.startswith("routed:") and "@" in spec:
        pc, subnet = spec[7:].split("@", 1)
        net = ipaddress.IPv4Network(subnet)
        if net.prefixlen < 22:
            raise SystemExit(f"BENCH_NET={spec}: the RTPS sweep covers at most a /22")
        return str(ipaddress.IPv4Address(pc)), str(net), "Ethernet", ()
    raise SystemExit(f"BENCH_NET={spec!r}: expected 'hotspot', 'lan:<pc ip>/<prefix>' or "
                     "'routed:<pc ip>@<board subnet>'")


PC_IP, SUBNET, LINK, SWEEP_SKIP = _net(NET)
HOTSPOT = NET == "hotspot"
CHIP = "esp32p4"
BAUD = 460800

# Flash layout (partitions.csv; checked against the build's table, never assumed
# for writing).
PT_OFFSET, PT_SIZE = 0x8000, 0xC00
STORAGE_OFFSET, STORAGE_SIZE = 0xC20000, 0x3E0000

VENV_PY = os.environ.get("BENCH_VENV_PY") or default_venv_py()


def _has(module: str) -> bool:
    import importlib.util
    try:
        return importlib.util.find_spec(module) is not None
    except (ImportError, ValueError):
        return False


def python() -> str:
    """The interpreter for child scripts: this one if it has pyserial, else the venv."""
    return sys.executable if _has("serial") else VENV_PY


def esptool_python() -> str:
    """The interpreter that runs esptool: this one if it has esptool, else the venv."""
    return sys.executable if _has("esptool") else VENV_PY


def child_env(**extra: str) -> dict:
    """Environment for child processes. No .pyc files: the scripts we run may sit
    in a worktree we must not write to."""
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1", PYTHONUNBUFFERED="1")
    env.update(extra)
    return env


def kill_tree(pid: int) -> str:
    """Kill a process and everything below it (taskkill /T): killing only a venv launcher
    leaves its base-interpreter child running, holding its sockets. Elsewhere SIGKILL to
    each process of the tree (from the whole process table), children first."""
    if WINDOWS:
        r = subprocess.run(["taskkill", "/T", "/F", "/PID", str(pid)], capture_output=True,
                           text=True, timeout=30)
        return (r.stdout + r.stderr).strip()
    tree = descendants({pid}, process_table(only_python=False))
    done = []
    for victim in sorted(tree - {pid}) + [pid]:
        try:
            os.kill(victim, signal.SIGKILL)
            done.append(str(victim))
        except OSError as e:
            done.append(f"{victim}: {e.strerror}")
    return "SIGKILL " + ", ".join(done)


def stamp() -> str:
    return datetime.datetime.now().strftime("%Y%m%d-%H%M%S")


def now_iso() -> str:
    return datetime.datetime.now().isoformat(timespec="seconds")


def write_json(path: pathlib.Path, data: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, default=str), encoding="utf-8")


def log(message: str) -> None:
    print(f"[bench {datetime.datetime.now():%H:%M:%S}] {message}", flush=True)


def esptool(args: list[str], timeout: float = 600.0, cwd: str | None = None) -> subprocess.CompletedProcess:
    """Run esptool with the venv python. Callers build the argument list; this
    module never adds erase options."""
    for bad in ("erase", "erase_flash", "erase-flash", "erase_region", "erase-region",
                "--erase-all", "-e"):
        if bad in args:
            raise SystemExit(f"refusing esptool call with {bad!r}: the bench never erases")
    cmd = [esptool_python(), "-m", "esptool", *args]
    log("esptool " + " ".join(args))
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, cwd=cwd,
                          env=child_env())


def is_python(name: str, argv0: str) -> bool:
    """A python interpreter process: by its name (CIM's Name, /proc's comm) or argv[0]."""
    return any(t.replace("\\", "/").rsplit("/", 1)[-1].lower().startswith("python")
               for t in (name, argv0))


def parse_proc_stat(stat: str) -> tuple[str, int]:
    """(comm, parent pid) from /proc/<pid>/stat: `pid (comm) state ppid ...`; the comm may
    hold spaces and parentheses, so the fields after it start at the last ')'."""
    comm = stat[stat.index("(") + 1:stat.rindex(")")]
    return comm, int(stat[stat.rindex(")") + 2:].split()[1])


def parse_ps(text: str) -> list[tuple[int, int, str, str]]:
    """(pid, parent pid, name, args) from `ps -eo pid=,ppid=,args=`; the name is argv[0]
    (a comm column beside args would be ambiguous: either may hold spaces)."""
    rows = []
    for line in text.splitlines():
        parts = line.split(None, 2)
        if len(parts) >= 2 and parts[0].isdigit() and parts[1].isdigit():
            args = parts[2] if len(parts) == 3 else ""
            first = args.split(None, 1)[0] if args.strip() else ""
            rows.append((int(parts[0]), int(parts[1]), first, args))
    return rows


def _table_proc(root: pathlib.Path) -> list[tuple[int, int, str, str]]:
    """(pid, parent pid, comm, command line) of every process in a /proc tree."""
    rows = []
    for entry in root.iterdir():
        if not entry.name.isdigit():
            continue
        try:
            comm, ppid = parse_proc_stat((entry / "stat").read_text(encoding="utf-8",
                                                                    errors="replace"))
            raw = (entry / "cmdline").read_bytes()
        except (OSError, ValueError):
            continue  # gone since the listing, or not ours to read
        argv = [a.decode("utf-8", "replace") for a in raw.split(b"\0") if a]
        rows.append((int(entry.name), ppid, comm, shlex.join(argv) if argv else ""))
    return rows


def _table_windows() -> list[tuple[int, int, str, str]]:
    ps = ("Get-CimInstance Win32_Process | ForEach-Object { "
          "\"$($_.ProcessId)`t$($_.ParentProcessId)`t$($_.Name)`t$($_.CommandLine)\" }")
    out = subprocess.run(["powershell", "-NoProfile", "-Command", ps],
                         capture_output=True, text=True, timeout=60)
    rows = []
    for line in out.stdout.splitlines():
        fields = line.split("\t", 3)
        if len(fields) == 4 and fields[0].strip().isdigit() and fields[1].strip().isdigit():
            rows.append((int(fields[0]), int(fields[1]), fields[2], fields[3]))
    return rows


def process_table(only_python: bool = True) -> list[tuple[int, int, str]]:
    """(pid, parent pid, command line) of every process (or every python process): CIM on
    Windows, /proc on Linux, `ps -eo pid=,ppid=,args=` where there is no /proc."""
    if WINDOWS:
        rows = _table_windows()
    elif pathlib.Path("/proc/self/stat").is_file():
        rows = _table_proc(pathlib.Path("/proc"))
    else:
        out = subprocess.run(["ps", "-eo", "pid=,ppid=,args="], capture_output=True,
                             text=True, timeout=60)
        rows = parse_ps(out.stdout)
    # Windows: by CIM's Name alone, as before (a quoted argv[0] may hold spaces).
    return [(pid, ppid, cmd) for pid, ppid, name, cmd in rows
            if not only_python
            or is_python(name, "" if WINDOWS or not cmd else cmd.split(None, 1)[0])]


def python_process_table() -> list[tuple[int, int, str]]:
    """(pid, parent pid, command line) of every python process."""
    return process_table(only_python=True)


def python_processes() -> list[tuple[int, str]]:
    """(pid, command line) of every python process."""
    return [(pid, cmd) for pid, _, cmd in python_process_table()]


def descendants(roots: set[int], table: list[tuple[int, int, str]]) -> set[int]:
    """`roots` and every process below them in `table` ((pid, parent pid, cmd) rows).
    A venv's python.exe on Windows is a launcher that runs the base interpreter as its
    child, and the child is the process doing the work: a child started from a venv is a
    tree of two. (A Linux venv's python is a symlink: one process, a tree of one.)"""
    found = set(roots)
    grew = True
    while grew:
        grew = False
        for pid, ppid, _ in table:
            if ppid in found and pid not in found:
                found.add(pid)
                grew = True
    return found


def stray_peers(exclude_pids: set[int] | None = None, exclude_trees: set[int] | None = None,
                table: list[tuple[int, int, str]] | None = None) -> list[str]:
    """Running rtps_mcb_* / rtps_selftest / sim_child processes, our own excluded:
    `exclude_pids` alone, `exclude_trees` with everything below them (a child started
    through the venv launcher, e.g. this run's own sim)."""
    rows = python_process_table() if table is None else table
    exclude = (set(exclude_pids or ()) | {os.getpid()}
               | descendants(set(exclude_trees or ()), rows))
    hits = []
    for pid, _, cmd in rows:
        if pid in exclude:
            continue
        if any(k in cmd for k in ("rtps_mcb_", "rtps_selftest", "sim_child.py",
                                  "rtps_drive_game", "rtps_host.py")):
            hits.append(f"{pid}: {cmd.strip()[:160]}")
    return hits


# --- build variants that add self-test rows -------------------------------------------------
# Some build options add rows to the self test that the B2/B3 baseline (a plain bench build)
# does not have. The bench reads the option from the image's own build config
# (<build dir>/config/sdkconfig.h), never from a fixed count, so the variant's rows go on top of
# whatever the tree's main/selftest_spec.hpp declares (C4's ctl.*, C2's joy.*, ...).
#   option -> need: the tree's selftest_spec rows with this `need` are the variant's rows;
#             why: what the variant is; report_only: row -> why it is shown and never graded.
VARIANT_ROWS: dict[str, dict] = {
    "CONFIG_HMI_BENCH_ALLOC_GUARD": {
        "need": "bench",
        "why": "on-chip allocation guard, TS-DET-08 (ci/sdkconfig.allocguard)",
        "report_only": {
            "mem.alloc_adc": "report-only: owner T12 (2026-10-11), Read ADC allocates in espp's "
                             "publish until that is fixed",
        },
    },
}


def build_config(build_dir: pathlib.Path | str | None) -> dict[str, str]:
    """The build's `#define CONFIG_<X> <value>` lines (config/sdkconfig.h); {} without one."""
    if build_dir is None:
        return {}
    path = pathlib.Path(build_dir) / "config" / "sdkconfig.h"
    if not path.is_file():
        return {}
    out: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3 and parts[0] == "#define" and parts[1].startswith("CONFIG_"):
            out[parts[1]] = parts[2].strip()
    return out


def variant_checks(build_dir: pathlib.Path | str | None, tree: pathlib.Path,
                   config: dict[str, str] | None = None) -> dict[str, dict]:
    """Every self-test row the build's variant options add: name -> {"why", "report_only"}
    (report_only: the reason it is never graded, or None). Rows from the tree's
    main/selftest_spec.hpp; `config` stands in for the build's sdkconfig.h (selftests)."""
    config = build_config(build_dir) if config is None else config
    enabled = [(opt, v) for opt, v in VARIANT_ROWS.items() if config.get(opt) == "1"]
    spec = pathlib.Path(tree) / "main" / "selftest_spec.hpp"
    if not enabled or not spec.is_file():
        return {}
    sys.path.insert(0, str(REPO / "scripts"))
    import rammp_rtps  # the parser only; the file is the tree's
    rows = rammp_rtps.parse_selftest_spec(spec.read_text(encoding="utf-8"))
    out: dict[str, dict] = {}
    for _opt, v in enabled:
        for row in rows:
            if row.need == v["need"]:
                out[row.name] = {"why": v["why"], "report_only": v["report_only"].get(row.name)}
    return out
