#!/usr/bin/env python3
"""The HMI's serial monitor over Ethernet, in one command: bridge + idf monitor.

  python scripts/rtps_monitor.py                    # the only HMI found
  python scripts/rtps_monitor.py --device MAC       # one of several
  python scripts/rtps_monitor.py --peer 10.0.0.133  # a link with no multicast

Starts rtps_serial.py in the background (unless one is already serving), opens
esp-idf-monitor on it, and stops the bridge when the monitor quits (Ctrl-]).
In the monitor: type commands ('help'), Ctrl-T Ctrl-F builds and flashes over
Ethernet (the app goes as an OTA update), Ctrl-T Ctrl-R reboots the HMI if
--allow-reset was given. The bridge's own log goes to a file (path printed).

Runs with the ESP-IDF Python (it has pyserial and esp-idf-monitor); started with
another one, it restarts itself with that.
"""

from __future__ import annotations

import argparse
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
RFC2217_PORT, RAW_PORT = 4000, 4001  # rtps_serial.py's defaults
IDF_VENV_PYTHON = r"C:\Espressif\tools\python\v6.0\venv\Scripts\python.exe"
IDF_PATH = r"C:\esp\v6.0\esp-idf"


def has_tools() -> bool:
    try:
        import esp_idf_monitor  # noqa: F401
        import serial  # noqa: F401
    except ImportError:
        return False
    return True


def idf_python() -> str | None:
    """The ESP-IDF environment's Python: from the loaded environment, else the usual place."""
    env = os.environ.get("IDF_PYTHON_ENV_PATH")
    for candidate in ([os.path.join(env, "Scripts", "python.exe"),
                       os.path.join(env, "bin", "python")] if env else []) + [IDF_VENV_PYTHON]:
        if os.path.exists(candidate):
            return candidate
    return None


def serving(port: int) -> bool:
    try:
        socket.create_connection(("127.0.0.1", port), timeout=0.3).close()
        return True
    except OSError:
        return False


def start_bridge(extra: list[str]) -> tuple[subprocess.Popen, str]:
    log_path = os.path.join(tempfile.gettempdir(), "rtps_serial.log")
    log = open(log_path, "w", encoding="utf-8")
    # Its own process group: a Ctrl-C typed in the monitor must not stop it.
    flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
    bridge = subprocess.Popen([sys.executable, "-u", os.path.join(HERE, "rtps_serial.py")] + extra,
                              stdout=log, stderr=subprocess.STDOUT, creationflags=flags,
                              start_new_session=os.name != "nt")
    return bridge, log_path


def wait_serving(bridge: subprocess.Popen, timeout: float) -> bool:
    start = time.monotonic()
    told = False
    while time.monotonic() - start < timeout:
        if bridge.poll() is not None:
            return False
        if serving(RFC2217_PORT):
            return True
        if not told and time.monotonic() - start > 6:
            told = True
            print("still looking for the HMI...", flush=True)
        time.sleep(0.3)
    return False


def monitor_command(allow_reset: bool) -> list[str]:
    elf = os.path.join(REPO, "build", "rammp-hmi-p4.elf")
    command = [sys.executable, "-m", "esp_idf_monitor", "-p", f"rfc2217://127.0.0.1:{RFC2217_PORT}",
               "--toolchain-prefix", "riscv32-esp-elf-", "--target", "esp32p4"]
    if not allow_reset:
        command.append("--no-reset")
    idf_py = os.path.join(os.environ.get("IDF_PATH", IDF_PATH), "tools", "idf.py")
    if os.path.exists(idf_py):
        # Ctrl-T Ctrl-F: idf.py flash through the bridge's raw port (1 s for esptool,
        # not the 20 s the RFC 2217 port takes). Single quotes, as idf.py passes it: the
        # monitor shlex-splits this, which would eat bare Windows backslashes.
        parts = [sys.executable, idf_py, "-p", f"socket://127.0.0.1:{RAW_PORT}"]
        command += ["--make", " ".join(f"'{part}'" for part in parts)]
    if os.path.exists(elf):
        command.append(elf)  # decodes panics and addresses
    return command


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--device", metavar="MAC", help="the HMI (default: the only one)")
    parser.add_argument("--peer", action="append", default=[], metavar="HOST",
                        help="the HMI's address, for links with no multicast (repeatable)")
    parser.add_argument("--allow-reset", action="store_true",
                        help="the monitor's reset (Ctrl-T Ctrl-R) reboots the HMI")
    parser.add_argument("--wait", type=float, default=30.0, metavar="S",
                        help="how long to look for the HMI (default 30)")
    cli = parser.parse_args()

    if not has_tools():
        python = idf_python()
        if python is None or os.path.normcase(python) == os.path.normcase(sys.executable):
            print("needs pyserial and esp-idf-monitor: run it with the ESP-IDF Python")
            return 1
        signal.signal(signal.SIGINT, signal.SIG_IGN)  # Ctrl-C is the monitor's (below)
        return subprocess.call([python, os.path.abspath(__file__)] + sys.argv[1:])

    bridge = None
    if serving(RFC2217_PORT):
        print(f"a bridge is already serving port {RFC2217_PORT}: using it")
    else:
        extra = []
        if cli.device:
            extra += ["--device", cli.device]
        for peer in cli.peer:
            extra += ["--peer", peer]
        if cli.allow_reset:
            extra.append("--allow-reset")
        bridge, log_path = start_bridge(extra)
        print(f"starting the bridge (log: {log_path})...", flush=True)
        if not wait_serving(bridge, cli.wait):
            with open(log_path, encoding="utf-8", errors="replace") as handle:
                print(handle.read().strip() or "the bridge said nothing")
            print("no HMI found (try --peer <ip> or --device <mac>)")
            bridge.kill()
            return 2
    try:
        # Ctrl-C belongs to the monitor (it sends it to the HMI), not to us.
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        return subprocess.call(monitor_command(cli.allow_reset))
    finally:
        if bridge is not None:
            bridge.terminate()
            try:
                bridge.wait(5)
            except subprocess.TimeoutExpired:
                bridge.kill()


if __name__ == "__main__":
    sys.exit(main())
