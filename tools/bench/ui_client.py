"""The remote-UI client the bench steps use: hmi_ui.Hmi with timeouts, a retry and a log.

    hmi = ui_client.open_hmi(hmi_ui, ip, out / "remote-ui.jsonl")   # hmi_ui from --tree
    python ui_client.py selftest                                     # cases UI-001.., no board

scripts/hmi_ui.Hmi, as it is, opens one socket with a 20 s timeout and nothing else: a
reply that takes 19 s on every command is waited out in silence, and the stick button's
hold is timed on the PC between the BTN 1 and BTN 0 replies, so a slow reply stretches
the hold on the board. This subclass of the --tree's Hmi (its holds, taps and menu
moves are unchanged; only the I/O under them is replaced) gives every command:

- a timeout: COMMAND_TIMEOUT_S (SHOT: SHOT_TIMEOUT_S, a 1.8 MB frame; SWIPE: + its ms);
- on a timeout or a dropped connection, a reconnect, then ONE retry if the verb is
  idempotent (setting a level or reading: PING SCREEN FOCUS TASKS SHOT KEY BTN THEME
  PRESS RELEASE). TAP and SWIPE are not retried: the first may have been carried out
  with only its reply lost, and a second tap on a toggle (the burger key) undoes it.
  They raise RemoteUiError after the reconnect, as does a retry that fails too. The
  reconnect itself releases whatever was held: the board's remote UI lets go of the
  touch, the key and the button when a client goes (components/remote_ui/src/remote_ui.cpp);
- a JSON line in the step's log: the command, when it was sent and answered (wall
  clock), its duration, the gap since the previous answer (time spent on the PC, not
  on the wire), the attempt, and the reply or the error. `stats()` sums it up for the
  step's results: a stall is data, not a mystery.
Stdlib only.
"""

from __future__ import annotations

import datetime
import inspect
import json
import os
import pathlib
import socket
import sys
import tempfile
import threading
import time
import traceback
from typing import Callable

COMMAND_TIMEOUT_S = 5.0     # a TAP is ~0.2 s plus up to 2 x 1 s waiting for pointer reads
SHOT_TIMEOUT_S = 20.0       # 720 x 1280 x 2 bytes over Wi-Fi
CONNECT_TIMEOUT_S = 10.0    # the board serves one client; a new one waits in the backlog
SLOW_S = 1.0                # a command slower than this is counted as slow in stats()
IDEMPOTENT = {"PING", "SCREEN", "FOCUS", "TASKS", "SHOT", "KEY", "BTN", "THEME", "PRESS",
              "RELEASE"}


class RemoteUiError(RuntimeError):
    """A remote-UI command that got no answer, after the reconnect (and retry, if allowed)."""


def _now() -> str:
    return datetime.datetime.now().isoformat(timespec="milliseconds")


class CommandLog:
    def __init__(self, path: pathlib.Path | None):
        self.path = path
        self.rows: list[dict] = []
        self._lock = threading.Lock()
        if path is not None:
            path.parent.mkdir(parents=True, exist_ok=True)

    def write(self, row: dict) -> None:
        with self._lock:
            self.rows.append(row)
            if self.path is not None:
                with open(self.path, "a", encoding="utf-8", newline="\n") as f:
                    f.write(json.dumps(row) + "\n")

    def stats(self) -> dict:
        done = [r for r in self.rows if "error" not in r]
        return {"log": str(self.path) if self.path else None,
                "commands": len(self.rows),
                "errors": sum(1 for r in self.rows if "error" in r),
                "retries": sum(1 for r in self.rows if r["attempt"] > 1),
                "slow_over_s": SLOW_S,
                "slow": [{k: r[k] for k in ("cmd", "sent", "dur_s")}
                         for r in self.rows if r["dur_s"] > SLOW_S],
                "max_dur_s": max((r["dur_s"] for r in done), default=0.0),
                "max_gap_s": max((r["gap_s"] for r in self.rows if r["gap_s"] is not None),
                                 default=0.0)}


def _timeout_for(text: str) -> float:
    words = text.split()
    verb = words[0].upper() if words else ""
    if verb == "SHOT":
        return SHOT_TIMEOUT_S
    if verb == "SWIPE" and len(words) > 5 and words[5].isdigit():
        return COMMAND_TIMEOUT_S + int(words[5]) / 1000.0
    return COMMAND_TIMEOUT_S


def open_hmi(hmi_ui, host: str, log_path: pathlib.Path | None, port: int | None = None):
    """A connected client: a subclass of `hmi_ui.Hmi` (the module from --tree)."""

    class LoggedHmi(hmi_ui.Hmi):
        def __init__(self) -> None:  # no super().__init__: it connects with a 20 s timeout
            self.host = host
            self.port = port if port is not None else hmi_ui.PORT
            self.log = CommandLog(log_path)
            self._last_answer: float | None = None
            self.sock = None
            self.buf = b""
            self._connect()

        def _connect(self) -> None:
            self.sock = socket.create_connection((self.host, self.port),
                                                 timeout=CONNECT_TIMEOUT_S)
            self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.buf = b""

        def _reconnect(self) -> str:
            try:
                self.sock.close()
            except OSError:
                pass
            try:
                self._connect()
                return "reconnected"
            except OSError as e:
                self.sock = None
                return f"reconnect failed: {e}"

        def _call(self, text: str, read: Callable[[], object],
                  describe: Callable[[object], str]) -> object:
            words = text.split()
            verb = words[0].upper() if words else ""
            for attempt in (1, 2):
                sent, t0 = _now(), time.monotonic()
                gap = None if self._last_answer is None else round(t0 - self._last_answer, 3)
                row = {"cmd": text, "attempt": attempt, "sent": sent, "gap_s": gap}
                try:
                    if self.sock is None:
                        raise ConnectionError("not connected")
                    self.sock.settimeout(_timeout_for(text))
                    self.sock.sendall((text + "\n").encode("ascii"))
                    result = read()
                except (OSError, ConnectionError) as e:  # socket.timeout is an OSError
                    row.update(answered=_now(), dur_s=round(time.monotonic() - t0, 3),
                               error=f"{type(e).__name__}: {e}", then=self._reconnect())
                    self._last_answer = time.monotonic()
                    retry = attempt == 1 and verb in IDEMPOTENT and self.sock is not None
                    if not retry:
                        row["retried"] = False
                        self.log.write(row)
                        why = ("" if verb in IDEMPOTENT
                               else " (not retried: not idempotent)")
                        raise RemoteUiError(f"{text!r}: {row['error']} after "
                                            f"{row['dur_s']:.1f}s, attempt {attempt}{why}; "
                                            f"{row['then']}") from e
                    row["retried"] = True
                    self.log.write(row)
                    continue
                self._last_answer = time.monotonic()
                row.update(answered=_now(), dur_s=round(self._last_answer - t0, 3),
                           reply=describe(result))
                self.log.write(row)
                return result
            raise AssertionError("unreachable")

        def command(self, text: str) -> str:
            return self._call(text, self._line, str)

        def shot(self, half: bool = False) -> tuple[int, int, bytes]:
            def read() -> tuple[int, int, bytes]:
                reply = self._line()
                if not reply.startswith("FRAME "):
                    raise RuntimeError(reply)
                _, w, h, count = reply.split()
                return int(w), int(h), self._exact(int(count))
            return self._call("SHOT 2" if half else "SHOT", read,
                              lambda r: f"FRAME {r[0]} {r[1]} ({len(r[2])} bytes)")

        def stats(self) -> dict:
            return self.log.stats()

        def close(self) -> None:
            if self.sock is not None:
                self.sock.close()

    return LoggedHmi()


def ping(host: str, within: float, port: int = 3333, every: float = 1.0) -> tuple[bool, str]:
    """Poll the remote UI's PING until it answers OK or `within` seconds pass."""
    deadline = time.monotonic() + within
    tries, last = 0, "no attempt"
    while True:
        tries += 1
        try:
            with socket.create_connection((host, port), timeout=min(3.0, within)) as s:
                s.settimeout(3.0)
                s.sendall(b"PING\n")
                buf = b""
                while b"\n" not in buf:
                    chunk = s.recv(64)
                    if not chunk:
                        raise ConnectionError("closed")
                    buf += chunk
            reply = buf.split(b"\n")[0].decode("ascii", "replace").strip()
            if reply == "OK":
                return True, f"PING OK after {tries} tr{'y' if tries == 1 else 'ies'}"
            last = f"answer {reply!r}"
        except OSError as e:
            last = f"{type(e).__name__}: {e}"
        if time.monotonic() + every > deadline:
            return False, f"no PING answer in {within:.0f}s ({tries} tries; last: {last})"
        time.sleep(every)  # poll period, bounded by the deadline


# ---------------------------------------------------------------- selftest


class FakeRemoteUi:
    """A localhost stand-in for components/remote_ui/src/remote_ui.cpp: one client at a time, line in, line out.
    `stall` maps a verb to how many of its next commands get no answer (the connection
    stays open, as on a stalled Wi-Fi link)."""

    def __init__(self) -> None:
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(4)
        self.port = self.listener.getsockname()[1]
        self.stall: dict[str, int] = {}
        self.seen: list[str] = []
        self.clients = 0
        self.screen = "LockedScreen"
        self._stop = False
        threading.Thread(target=self._serve, daemon=True).start()

    def _serve(self) -> None:
        while not self._stop:
            try:
                client, _ = self.listener.accept()
            except OSError:
                return
            self.clients += 1
            threading.Thread(target=self._client, args=(client,), daemon=True).start()

    def _client(self, client: socket.socket) -> None:
        buf = b""
        with client:
            while True:
                try:
                    chunk = client.recv(256)
                except OSError:
                    return
                if not chunk:
                    return
                buf += chunk
                while b"\n" in buf:
                    line, _, buf = buf.partition(b"\n")
                    text = line.decode()
                    verb = text.split()[0]
                    self.seen.append(text)
                    if self.stall.get(verb, 0) > 0:
                        self.stall[verb] -= 1
                        continue  # no answer
                    if verb == "SCREEN":
                        client.sendall(f"OK {self.screen}\n".encode())
                    elif verb == "SHOT":
                        client.sendall(b"FRAME 2 2 8\n" + bytes(range(8)))
                    else:
                        client.sendall(b"OK\n")

    def close(self) -> None:
        self._stop = True
        self.listener.close()


def _hmi_ui():
    repo = pathlib.Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(repo / "scripts"))
    import hmi_ui
    return hmi_ui


def _with_fake(fn: Callable[[FakeRemoteUi, object, pathlib.Path], None]) -> Callable[[], None]:
    def run() -> None:
        global COMMAND_TIMEOUT_S
        saved, COMMAND_TIMEOUT_S = COMMAND_TIMEOUT_S, 0.3
        fake = FakeRemoteUi()
        try:
            with tempfile.TemporaryDirectory() as tmp:
                log = pathlib.Path(tmp) / "remote-ui.jsonl"
                hmi = open_hmi(_hmi_ui(), "127.0.0.1", log, port=fake.port)
                try:
                    fn(fake, hmi, log)
                finally:
                    hmi.close()
        finally:
            COMMAND_TIMEOUT_S = saved
            fake.close()
    run.__wrapped__ = fn
    return run


def expect(what: str, got: object, want: object) -> None:
    if got != want:
        raise AssertionError(f"{what}: got {got!r}, want {want!r}")


def _rows(log: pathlib.Path) -> list[dict]:
    return [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()]


def t_logged(fake, hmi, log) -> None:
    expect("screen", hmi.screen(), "LockedScreen")
    hmi.press(10)  # BTN 1, BTN 0: the hold helpers run on the logged I/O
    expect("shot", hmi.shot()[:2], (2, 2))
    rows = _rows(log)
    expect("one line per command", [r["cmd"] for r in rows], ["SCREEN", "BTN 1", "BTN 0", "SHOT"])
    expect("fields", sorted(rows[1]), sorted(["cmd", "attempt", "sent", "answered", "dur_s",
                                              "gap_s", "reply"]))
    expect("first gap is None, later ones measured",
           (rows[0]["gap_s"] is None, rows[2]["gap_s"] >= 0.01), (True, True))
    expect("reply", (rows[0]["reply"], rows[3]["reply"]), ("OK LockedScreen",
                                                           "FRAME 2 2 (8 bytes)"))


def t_retry_idempotent(fake, hmi, log) -> None:
    fake.stall["BTN"] = 1
    expect("BTN answered on the retry", hmi.command("BTN 0"), "OK")
    rows = _rows(log)
    expect("attempts", [(r["attempt"], "error" in r, r.get("retried")) for r in rows],
           [(1, True, True), (2, False, None)])
    expect("a new connection for the retry", fake.clients, 2)
    expect("stats", {k: hmi.stats()[k] for k in ("commands", "errors", "retries")},
           {"commands": 2, "errors": 1, "retries": 1})


def t_tap_not_retried(fake, hmi, log) -> None:
    fake.stall["TAP"] = 1
    try:
        hmi.tap(360, 1198)
    except RemoteUiError as e:
        expect("reason", "not retried: not idempotent" in str(e), True)
    else:
        raise AssertionError("a stalled TAP did not raise")
    expect("the TAP was sent once", fake.seen.count("TAP 360 1198"), 1)
    expect("reconnected for what comes next", hmi.screen(), "LockedScreen")
    expect("logged", [(r["cmd"], r.get("retried")) for r in _rows(log)],
           [("TAP 360 1198", False), ("SCREEN", None)])


def t_retry_fails_too(fake, hmi, log) -> None:
    fake.stall["SCREEN"] = 2
    try:
        hmi.screen()
    except RemoteUiError as e:
        expect("attempt 2 named", "attempt 2" in str(e), True)
    else:
        raise AssertionError("two stalls did not raise")
    expect("two attempts logged", [r["attempt"] for r in _rows(log)], [1, 2])


def t_shot_timeout_longer() -> None:
    expect("SHOT", _timeout_for("SHOT"), SHOT_TIMEOUT_S)
    expect("SWIPE adds its ms", _timeout_for("SWIPE 1 2 3 4 1000"), COMMAND_TIMEOUT_S + 1.0)
    expect("others", _timeout_for("TAP 1 2"), COMMAND_TIMEOUT_S)


def t_ping() -> None:
    fake = FakeRemoteUi()
    try:
        expect("answers", ping("127.0.0.1", 2.0, port=fake.port)[0], True)
    finally:
        fake.close()
    probe = socket.socket()
    probe.bind(("127.0.0.1", 0))
    free = probe.getsockname()[1]
    probe.close()
    ok, detail = ping("127.0.0.1", 0.5, port=free, every=0.1)
    expect("nothing listening", (ok, "no PING answer" in detail), (False, True))


CASES = [
    ("UI-001 every command is logged with its times, duration, gap and reply",
     _with_fake(t_logged)),
    ("UI-002 an idempotent command that times out is retried once on a new connection",
     _with_fake(t_retry_idempotent)),
    ("UI-003 a TAP that times out is not retried, raises, and the client reconnects",
     _with_fake(t_tap_not_retried)),
    ("UI-004 a retry that times out too raises RemoteUiError", _with_fake(t_retry_fails_too)),
    ("UI-005 SHOT and SWIPE get longer timeouts", t_shot_timeout_longer),
    ("UI-006 ping polls PING until OK, or reports what it last saw", t_ping),
]


def run_cases(script: str, cases: list[tuple[str, Callable[[], None]]]) -> int:
    """Unity's format, as tools/guards/guardlib.run_cases prints it."""
    fails = 0
    for name, fn in cases:
        line = inspect.getsourcelines(getattr(fn, "__wrapped__", fn))[1]
        try:
            fn()
            print(f"{script}:{line}:{name}:PASS")
        except Exception as exc:  # noqa: BLE001 - every exception is a failed case
            fails += 1
            print(f"{script}:{line}:{name}:FAIL: {str(exc) or type(exc).__name__}")
            traceback.print_exc(file=sys.stdout)
    print("\n-----------------------")
    print(f"{len(cases)} Tests {fails} Failures 0 Ignored")
    print("OK" if fails == 0 else "FAIL")
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    if sys.argv[1:] != ["selftest"]:
        print(__doc__)
        sys.exit(2)
    sys.dont_write_bytecode = True  # hmi_ui is imported from scripts/
    sys.exit(run_cases("tools/bench/ui_client.py", CASES))
