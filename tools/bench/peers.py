"""The simulated MCB as a child process (sim_child.py around rtps_mcb_sim.py).

Readiness is a condition, not a sleep (TS-DET-01): the sim is ready once the
board streams XYTwist to it, which means discovery matched both ways.
"""

from __future__ import annotations

import os
import pathlib
import re
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402


class SimChild:
    def __init__(self, ip: str, tree: pathlib.Path, log_path: pathlib.Path | None = None):
        cmd = [common.python(), str(common.HERE / "sim_child.py"), "--tree", str(tree), "--",
               "--peer", ip, "--bind-address", common.PC_IP]
        common.log("sim: " + " ".join(cmd))
        self.proc = subprocess.Popen(cmd, cwd=str(tree), stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     text=True, encoding="utf-8", errors="replace",
                                     env=common.child_env())
        self.lines: list[tuple[float, str]] = []
        self.t0 = time.monotonic()
        self.log_path = log_path
        self._cv = threading.Condition()
        threading.Thread(target=self._pump, daemon=True).start()

    def _pump(self) -> None:
        assert self.proc.stdout is not None
        for line in self.proc.stdout:
            with self._cv:
                self.lines.append((time.monotonic() - self.t0, line.rstrip()))
                self._cv.notify_all()

    def send(self, cmd: str) -> None:
        assert self.proc.stdin is not None
        self.proc.stdin.write(cmd + "\n")
        self.proc.stdin.flush()

    def wait_for(self, regex: str, timeout: float, since: int = 0) -> re.Match | None:
        """First line at index >= since matching regex, within timeout."""
        r = re.compile(regex)
        deadline = time.monotonic() + timeout
        with self._cv:
            while True:
                for _, line in self.lines[since:]:
                    m = r.search(line)
                    if m:
                        return m
                left = deadline - time.monotonic()
                if left <= 0 or self.proc.poll() is not None:
                    return None
                self._cv.wait(min(left, 0.5))

    def mark(self) -> int:
        with self._cv:
            return len(self.lines)

    def command(self, cmd: str, timeout: float = 5.0) -> bool:
        """Send a sim command and wait for its echo."""
        at = self.mark()
        self.send(cmd)
        return self.wait_for(r"^CMD " + re.escape(cmd) + r"$", timeout, at) is not None

    def xy_count(self, timeout: float = 5.0) -> int | None:
        at = self.mark()
        self.send("jcount")
        m = self.wait_for(r"^XYCOUNT (\d+)", timeout, at)
        return int(m.group(1)) if m else None

    def wait_ready(self, timeout: float = 45.0) -> bool:
        """Until the board has sent XYTwist to us."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            n = self.xy_count()
            if n:
                return True
            if self.proc.poll() is not None:
                return False
            time.sleep(0.5)  # poll interval, not a wait for a condition
        return False

    def stop(self) -> None:
        try:
            self.send("q")
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()
            self.proc.wait(timeout=5)
        if self.log_path is not None:
            self.log_path.parent.mkdir(parents=True, exist_ok=True)
            self.log_path.write_text("\n".join(f"{t:8.2f} {l}" for t, l in self.lines) + "\n",
                                     encoding="utf-8")

    def __enter__(self) -> "SimChild":
        return self

    def __exit__(self, *_: object) -> None:
        self.stop()
