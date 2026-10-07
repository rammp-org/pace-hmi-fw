"""The simulated MCB as a child process (sim_child.py around rtps_mcb_sim.py).

Readiness is a condition, not a sleep (TS-DET-01): the sim is ready once the
board streams XYTwist to it, which means discovery matched both ways.

With `event_log`, the sim writes its JSONL event log there (rtps_mcb_sim.py
--event-log): every DriveCommand it received and what it did with it, the
MibStatus states it sent, XYTwist. `event_mark(label)` puts a mark in that log
and `events(after=label)` reads what came after it, so a step grades only what
its own action caused, by order in the sim's log rather than by clocks.
"""

from __future__ import annotations

import json
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
    def __init__(self, ip: str, tree: pathlib.Path, log_path: pathlib.Path | None = None,
                 event_log: pathlib.Path | None = None):
        cmd = [common.python(), str(common.HERE / "sim_child.py"), "--tree", str(tree), "--",
               "--peer", ip, "--bind-address", common.PC_IP]
        self.event_log = event_log
        if event_log is not None:
            event_log.parent.mkdir(parents=True, exist_ok=True)
            cmd += ["--event-log", str(event_log)]
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

    def reply(self, cmd: str, regex: str, timeout: float = 5.0) -> re.Match | None:
        """Send a sim command and wait for the sim's own answer to it (not the echo)."""
        at = self.mark()
        self.send(cmd)
        return self.wait_for(regex, timeout, at)

    def event_mark(self, label: str, timeout: float = 5.0) -> bool:
        """A mark in the event log; True once the sim has written it. Labels are
        lower case: the sim lower-cases every command."""
        return self.reply(f"mark {label}", r"MARK " + re.escape(label) + r"$", timeout) is not None

    def events(self, after: str | None = None) -> list[dict]:
        """The event log's records; with `after`, only those after the last mark so named
        (none if that mark is not there)."""
        records = read_jsonl(self.event_log) if self.event_log is not None else []
        if after is None:
            return records
        marks = [i for i, r in enumerate(records)
                 if r.get("ev") == "mark" and r.get("label") == after]
        return records[marks[-1] + 1:] if marks else []

    def wait_event(self, predicate, timeout: float, after: str) -> dict | None:
        """The first record after mark `after` for which predicate(record) holds."""
        deadline = time.monotonic() + timeout
        while True:
            for record in self.events(after):
                if predicate(record):
                    return record
            if time.monotonic() >= deadline or self.proc.poll() is not None:
                return None
            time.sleep(0.1)  # poll period of the log file, bounded by the deadline

    def not_ready_reason(self, timeout: float = 45.0) -> str:
        """Why wait_ready() gave up: the sim's own last words if it exited (e.g. it
        could not find a board that was still booting), else the plain timeout."""
        code = self.proc.poll()
        if code is None:
            return f"the simulated MCB got no XYTwist in {timeout:.0f} s"
        with self._cv:
            tail = [line for _, line in self.lines if line.strip()][-2:]
        return (f"the simulated MCB exited (code {code}) before any XYTwist: "
                + " | ".join(tail))

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


def read_jsonl(path: pathlib.Path) -> list[dict]:
    """Every complete line of a JSON-lines file; the part after the last newline (a
    line still being written) is left out. A missing file is empty."""
    try:
        text = path.read_text(encoding="utf-8")
    except FileNotFoundError:
        return []
    return [json.loads(line) for line in text.split("\n")[:-1] if line.strip()]
