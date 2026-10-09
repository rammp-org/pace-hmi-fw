"""scripts/rtps_mcb_sim.py, unchanged, plus a record of the XYTwist it receives.

    python sim_child.py --tree C:\\w\\bench -- --peer 192.168.137.180 --bind-address 192.168.137.2

Everything after `--` goes to rtps_mcb_sim.py (its --event-log and fault-mode
flags included), and every stdin command it knows (ok, e, x, s, p, r, ign, drop,
mark, q, ...) works as before. These are answered here and never reach the sim:
  jstart   start recording every XYTwist sample (clears the record)
  jstop    stop, print `XYT {json}`: count, max |x|, |y|, |twist|, the button
           values seen, first and last sample
  jcount   print `XYCOUNT n`: samples received since start
  jlast    print `XYLAST {json}`: the newest sample, [t, x, y, twist, buttons], whether
           recording or not (null before the first)
  jsave P  write the samples recorded since jstart to file P, one JSON line each:
           [t, x, y, twist, buttons], t = time.monotonic() when it arrived (the sim's
           event log stamps `mono` on the same clock); print `JSAVED n P`
Each sim command is echoed as `CMD <text>` so a driver can see it was taken.
The sim's own classes and decoder are used; only the decoder is wrapped to
keep a copy of each sample.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
import threading
import time


def summarise(samples: list[tuple]) -> dict:
    """jstop's summary of (t, x, y, twist, buttons) samples."""
    if not samples:
        return {"count": 0}
    return {"count": len(samples),
            "max_abs_x": max(abs(v[1]) for v in samples),
            "max_abs_y": max(abs(v[2]) for v in samples),
            "max_abs_twist": max(abs(v[3]) for v in samples),
            "buttons_seen": sorted({int(v[4]) for v in samples}),
            "first": list(samples[0][1:]), "last": list(samples[-1][1:])}


def write_samples(path: pathlib.Path, samples: list[tuple]) -> int:
    """jsave: one JSON line per sample, [t, x, y, twist, buttons]."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        for v in samples:
            f.write(json.dumps([round(v[0], 4), v[1], v[2], v[3], int(v[4])]) + "\n")
    return len(samples)


def read_samples(path: pathlib.Path) -> list[tuple]:
    """What write_samples wrote, as tuples."""
    text = path.read_text(encoding="utf-8")
    return [tuple(json.loads(line)) for line in text.splitlines() if line.strip()]


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--tree", type=pathlib.Path, required=True)
    p.add_argument("rest", nargs=argparse.REMAINDER)
    a = p.parse_args()
    rest = a.rest[1:] if a.rest[:1] == ["--"] else a.rest

    sys.path.insert(0, str(a.tree / "scripts"))
    import rtps_mcb_sim as sim  # noqa: E402

    lock = threading.Lock()
    rec = {"on": False, "samples": [], "count": 0, "last": None}
    decode = sim.spec.unpack_xy_twist

    def recording_decode(payload: bytes):
        sample = decode(payload)
        if sample is not None:
            with lock:
                rec["count"] += 1
                rec["last"] = (time.monotonic(), *tuple(sample))
                if rec["on"]:
                    rec["samples"].append(rec["last"])
        return sample

    sim.spec.unpack_xy_twist = recording_decode

    def summary() -> dict:
        with lock:
            s = list(rec["samples"])
        return summarise(s)

    def scripted_input(_prompt: str = "") -> str:
        while True:
            line = sys.stdin.readline()
            if not line:
                raise EOFError
            cmd = line.strip()
            if cmd == "jstart":
                with lock:
                    rec["on"], rec["samples"] = True, []
                print("JSTART", flush=True)
            elif cmd == "jstop":
                with lock:
                    rec["on"] = False
                print("XYT " + json.dumps(summary()), flush=True)
            elif cmd.startswith("jsave "):
                with lock:
                    s = list(rec["samples"])
                target = pathlib.Path(cmd[len("jsave "):].strip())
                print(f"JSAVED {write_samples(target, s)} {target}", flush=True)
            elif cmd == "jlast":
                with lock:
                    last = rec["last"]
                print("XYLAST " + json.dumps(None if last is None else list(last)), flush=True)
            elif cmd == "jcount":
                with lock:
                    n = rec["count"]
                print(f"XYCOUNT {n}", flush=True)
            else:
                print(f"CMD {cmd}", flush=True)
                return line

    sim.input = scripted_input  # run_interactive looks `input` up in its module first
    sys.argv = ["rtps_mcb_sim.py", *rest]
    return sim.main()


if __name__ == "__main__":
    sys.exit(main())
