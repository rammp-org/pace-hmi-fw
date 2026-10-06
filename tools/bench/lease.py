"""The board lease (TS-PAR-02): one JSON lock file says who holds the board.

    python lease.py status
    python lease.py acquire --owner bench-agent
    python lease.py release --owner bench-agent

The file holds {owner, since, pid}. Acquiring fails while another owner holds
it with a live pid; a lease whose pid is dead is stale and is taken over (the
takeover is reported). Child scripts started by run_bench.py inherit
BENCH_LEASE_OWNER and treat a lease held by that owner as theirs.
"""

from __future__ import annotations

import argparse
import contextlib
import ctypes
import json
import os
import pathlib
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402

LEASE = pathlib.Path(os.environ.get(
    "BENCH_LEASE", r"C:\Users\halai\Offline_Documents\ATDev\rammp\.board-lease"))


class LeaseHeld(RuntimeError):
    pass


def pid_alive(pid: int) -> bool:
    if pid <= 0:
        return False
    if os.name != "nt":
        try:
            os.kill(pid, 0)
            return True
        except OSError:
            return False
    kernel32 = ctypes.windll.kernel32
    handle = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return False
    try:
        code = ctypes.c_ulong()
        if not kernel32.GetExitCodeProcess(handle, ctypes.byref(code)):
            return False
        return code.value == 259  # STILL_ACTIVE
    finally:
        kernel32.CloseHandle(handle)


def read() -> dict | None:
    try:
        return json.loads(LEASE.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return None
    except (OSError, ValueError):
        return {"owner": "?unreadable", "since": "?", "pid": -1}


def status() -> str:
    held = read()
    if held is None:
        return "free"
    alive = pid_alive(int(held.get("pid", -1)))
    return f"held by {held.get('owner')} since {held.get('since')} pid {held.get('pid')} " \
           f"({'live' if alive else 'stale'})"


def acquire(owner: str, pid: int | None = None) -> str:
    """Take the lease. Returns a note (e.g. a stale takeover); raises LeaseHeld."""
    pid = pid or os.getpid()
    held = read()
    note = "acquired"
    if held is not None:
        if held.get("owner") == owner and pid_alive(int(held.get("pid", -1))):
            return "already held by this owner"
        if pid_alive(int(held.get("pid", -1))):
            raise LeaseHeld(f"board lease {status()}")
        note = f"took over a stale lease ({held.get('owner')} pid {held.get('pid')})"
    LEASE.parent.mkdir(parents=True, exist_ok=True)
    tmp = LEASE.with_suffix(".tmp")
    tmp.write_text(json.dumps({"owner": owner, "since": common.now_iso(), "pid": pid}),
                   encoding="utf-8")
    os.replace(tmp, LEASE)
    return note


def release(owner: str) -> str:
    held = read()
    if held is None:
        return "already free"
    if held.get("owner") != owner:
        raise LeaseHeld(f"not ours to release: {status()}")
    LEASE.unlink()
    return "released"


@contextlib.contextmanager
def held(owner: str | None = None):
    """Hold the lease for a block. Inside a run_bench child (BENCH_LEASE_OWNER
    set and matching the file) this is a no-op."""
    inherited = os.environ.get("BENCH_LEASE_OWNER")
    current = read()
    if inherited and current and current.get("owner") == inherited:
        yield inherited
        return
    owner = owner or f"bench-{os.getpid()}"
    common.log(f"lease: {acquire(owner)}")
    try:
        yield owner
    finally:
        common.log(f"lease: {release(owner)}")


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("action", choices=["status", "acquire", "release"])
    p.add_argument("--owner", default="bench")
    p.add_argument("--pid", type=int, default=None,
                   help="pid recorded in the lease (default: this process, which then dies: "
                        "pass the long-lived owner's pid)")
    a = p.parse_args()
    try:
        if a.action == "status":
            print(status())
        elif a.action == "acquire":
            print(acquire(a.owner, a.pid))
        else:
            print(release(a.owner))
    except LeaseHeld as e:
        print(e)
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
