"""Board access: the port by MAC, serial captures, partition table, /storage backup.

    python board.py port                          # COM port of board 2
    python board.py watch 20 [--out f.log]        # capture WITHOUT resetting the board
    python board.py boot 90 [--out f.log]         # deliberate reset, then capture
    python board.py pt --build-dir C:\\b\\main_bench # board table == build table?
    python board.py backup-storage                # read /storage once, never twice

Opening the USB-Serial/JTAG port resets the chip unless DTR and RTS are False
*before* open(), so every open here sets both first. The deliberate reset is
esptool's USB hard reset: RTS (EN) high for 200 ms, then low, DTR kept low so
the chip boots the app, not the ROM downloader.
Only reads touch the flash here; nothing in this module writes or erases it.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import sys
import tempfile
import threading
import time
from typing import Callable

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402

try:
    import serial
    import serial.tools.list_ports
except ImportError:  # pragma: no cover - only the venv python has pyserial
    serial = None


class NoBoard(RuntimeError):
    pass


def find_port(mac: str = common.BOARD_MAC) -> str:
    if serial is None:
        raise NoBoard("pyserial missing: run with " + common.VENV_PY)
    for p in serial.tools.list_ports.comports():
        if (p.serial_number or "").upper() == mac.upper() or mac.upper() in (p.hwid or "").upper():
            return p.device
    raise NoBoard(f"no USB serial port with serial number {mac}")


def _open(port: str, timeout: float = 0.2) -> "serial.Serial":
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, timeout
    s.dtr = False  # both before open(): an open with either asserted resets the chip
    s.rts = False
    s.open()
    return s


class Capture:
    """Lines read from the port, each with its host time since the capture began."""

    def __init__(self) -> None:
        self.lines: list[tuple[float, str]] = []
        self.started = time.monotonic()
        self.notes: list[str] = []

    def text(self) -> str:
        return "\n".join(line for _, line in self.lines) + "\n"

    def save(self, path: pathlib.Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(self.text(), encoding="utf-8")


def capture(port: str, seconds: float, reset: bool = False,
            on_line: Callable[[Capture, str], bool] | None = None,
            stop: "threading.Event | None" = None) -> Capture:
    """Read `seconds` of output. With reset, pulse EN first. `on_line` may return
    True to end the capture early; `stop` (set from another thread) ends it within one
    read timeout even when no line comes. A port that drops (USB re-enumeration) is
    reopened, still without a reset, until the window ends."""
    cap = Capture()
    end = time.monotonic() + seconds
    s = _open(port)
    try:
        if reset:
            # esptool's HardReset for USB-Serial/JTAG. Windows' usbser.sys only sends
            # SET_CONTROL_LINE_STATE when DTR is written, so each RTS change is followed
            # by a write of the unchanged DTR (esptool's _setRTS does the same). Without
            # it the pulse never reached the chip (found 2026-10-07): B2 was capturing
            # the boot that the preceding esptool hard-reset had started.
            s.rts = True
            s.dtr = s.dtr
            time.sleep(0.2)  # esptool's USB hard-reset pulse width
            s.rts = False
            s.dtr = s.dtr
            cap.notes.append("reset pulse sent (RTS 200 ms, DTR low, DTR rewritten)")
        partial = b""
        while time.monotonic() < end and not (stop is not None and stop.is_set()):
            try:
                chunk = s.read(4096)
            except (serial.SerialException, OSError) as e:
                cap.notes.append(f"{time.monotonic() - cap.started:.1f}s port lost: {e}")
                try:
                    s.close()
                except Exception:
                    pass
                s = None
                while time.monotonic() < end and s is None and not (
                        stop is not None and stop.is_set()):
                    try:
                        s = _open(port)
                        cap.notes.append(f"{time.monotonic() - cap.started:.1f}s port reopened")
                    except (serial.SerialException, OSError):
                        time.sleep(0.2)
                if s is None:
                    break
                continue
            if not chunk:
                continue
            partial += chunk
            *done, partial = partial.split(b"\n")
            for raw in done:
                line = raw.decode("utf-8", "replace").rstrip("\r")
                cap.lines.append((time.monotonic() - cap.started, line))
                if on_line is not None and on_line(cap, line):
                    end = 0  # stop requested
        if partial:
            cap.lines.append((time.monotonic() - cap.started,
                              partial.decode("utf-8", "replace").rstrip("\r")))
    finally:
        if s is not None:
            s.close()
    return cap


# --- flash reads -------------------------------------------------------------


def read_flash(port: str, offset: int, size: int, out: pathlib.Path, timeout: float = 600) -> None:
    r = common.esptool(["--chip", common.CHIP, "-p", port, "-b", str(common.BAUD),
                        "--before", "default-reset", "--after", "hard-reset",
                        "read-flash", hex(offset), hex(size), str(out)], timeout=timeout)
    if r.returncode != 0 or not out.exists() or out.stat().st_size != size:
        raise RuntimeError(f"read-flash {hex(offset)} failed (exit {r.returncode}): "
                           f"{(r.stdout + r.stderr)[-600:]}")


def compare_partition_table(port: str, build_dir: pathlib.Path,
                            keep: pathlib.Path | None = None) -> tuple[bool, str]:
    """(equal, detail). The board's 0xC00 bytes at 0x8000 against the build's
    partition-table.bin, padded with 0xFF (erased flash) to the same length."""
    want = (build_dir / "partition_table" / "partition-table.bin").read_bytes()
    with tempfile.TemporaryDirectory() as tmp:
        got_path = pathlib.Path(tmp) / "pt.bin"
        read_flash(port, common.PT_OFFSET, common.PT_SIZE, got_path, timeout=120)
        got = got_path.read_bytes()
        if keep is not None:
            keep.parent.mkdir(parents=True, exist_ok=True)
            keep.write_bytes(got)
    padded = want + b"\xff" * (len(got) - len(want))
    if len(want) > len(got):
        return False, f"build table is {len(want)} bytes, longer than the {len(got)} read"
    if got == padded:
        return True, f"equal ({len(want)} bytes + erased tail)"
    first = next(i for i in range(len(got)) if got[i] != padded[i])
    return False, f"differ from byte {first:#x}"


def backup_storage(port: str) -> tuple[pathlib.Path, bool]:
    """(path, made_now). Read /storage once: an existing backup is never replaced."""
    common.BENCH_HOME.mkdir(parents=True, exist_ok=True)
    existing = sorted(common.BENCH_HOME.glob("storage-*.bin"))
    existing = [p for p in existing if p.stat().st_size == common.STORAGE_SIZE]
    if existing:
        return existing[0], False
    out = common.BENCH_HOME / f"storage-{common.stamp()}.bin"
    read_flash(port, common.STORAGE_OFFSET, common.STORAGE_SIZE, out, timeout=900)
    return out, True


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("port")
    for name in ("watch", "boot"):
        c = sub.add_parser(name)
        c.add_argument("seconds", type=float)
        c.add_argument("--out", type=pathlib.Path)
    pt = sub.add_parser("pt")
    pt.add_argument("--build-dir", type=pathlib.Path, required=True)
    sub.add_parser("backup-storage")
    a = p.parse_args()

    port = find_port()
    if a.cmd == "port":
        print(port)
        return 0
    import lease
    with lease.held():
        if a.cmd in ("watch", "boot"):
            cap = capture(port, a.seconds, reset=(a.cmd == "boot"))
            if a.out:
                cap.save(a.out)
            else:
                sys.stdout.write(cap.text())
            for n in cap.notes:
                print("#", n, file=sys.stderr)
            return 0
        if a.cmd == "pt":
            equal, detail = compare_partition_table(port, a.build_dir)
            print(("EQUAL " if equal else "DIFFERENT ") + detail)
            return 0 if equal else 1
        path, made = backup_storage(port)
        print(("made " if made else "exists ") + str(path))
        return 0


if __name__ == "__main__":
    sys.exit(main())
