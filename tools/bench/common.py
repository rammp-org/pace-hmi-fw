"""Shared constants and helpers for the bench runner (tools/bench).

Runtime data never goes in the repo: everything this runner writes lives under
BENCH_HOME (C:\\b\\bench by default, BENCH_HOME in the environment overrides).
Run every script with the IDF venv python, the only one with pyserial/esptool.
"""

from __future__ import annotations

import datetime
import ipaddress
import json
import os
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent.parent  # the worktree these tools live in
BENCH_HOME = pathlib.Path(os.environ.get("BENCH_HOME", r"C:\b\bench"))
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
    raise SystemExit(f"BENCH_NET={spec!r}: expected 'hotspot' or 'lan:<pc ip>/<prefix>'")


PC_IP, SUBNET, LINK, SWEEP_SKIP = _net(NET)
HOTSPOT = NET == "hotspot"
CHIP = "esp32p4"
BAUD = 460800

# Flash layout (partitions.csv; checked against the build's table, never assumed
# for writing).
PT_OFFSET, PT_SIZE = 0x8000, 0xC00
STORAGE_OFFSET, STORAGE_SIZE = 0xC20000, 0x3E0000

VENV_PY = r"C:\Espressif\tools\python\v6.0\venv\Scripts\python.exe"


def python() -> str:
    """The interpreter for child scripts: this one if it has pyserial, else the venv."""
    try:
        import serial  # noqa: F401
        return sys.executable
    except ImportError:
        return VENV_PY


def child_env(**extra: str) -> dict:
    """Environment for child processes. No .pyc files: the scripts we run may sit
    in a worktree we must not write to."""
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1", PYTHONUNBUFFERED="1")
    env.update(extra)
    return env


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
    cmd = [VENV_PY, "-m", "esptool", *args]
    log("esptool " + " ".join(args))
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, cwd=cwd,
                          env=child_env())


def python_processes() -> list[tuple[int, str]]:
    """(pid, command line) of every python process, from CIM."""
    ps = ("Get-CimInstance Win32_Process | Where-Object { $_.Name -like 'python*' } | "
          "ForEach-Object { \"$($_.ProcessId)`t$($_.CommandLine)\" }")
    out = subprocess.run(["powershell", "-NoProfile", "-Command", ps],
                         capture_output=True, text=True, timeout=60)
    rows = []
    for line in out.stdout.splitlines():
        pid, _, cmd = line.partition("\t")
        if pid.strip().isdigit():
            rows.append((int(pid), cmd))
    return rows


def stray_peers(exclude_pids: set[int] | None = None) -> list[str]:
    """Running rtps_mcb_* / rtps_selftest / sim_child processes, our own excluded."""
    exclude = set(exclude_pids or ()) | {os.getpid()}
    hits = []
    for pid, cmd in python_processes():
        if pid in exclude:
            continue
        if any(k in cmd for k in ("rtps_mcb_", "rtps_selftest", "sim_child.py",
                                  "rtps_drive_game", "rtps_host.py")):
            hits.append(f"{pid}: {cmd.strip()[:160]}")
    return hits
