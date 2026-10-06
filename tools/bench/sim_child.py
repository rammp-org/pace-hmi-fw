"""scripts/rtps_mcb_sim.py, unchanged, plus a record of the XYTwist it receives.

    python sim_child.py --tree C:\\w\\bench -- --peer 192.168.137.180 --bind-address 192.168.137.2

Everything after `--` goes to rtps_mcb_sim.py, and every stdin command it
knows (ok, e, x, s, q, ...) works as before. Three more are answered here and
never reach the sim:
  jstart   start recording every XYTwist sample (clears the record)
  jstop    stop, print `XYT {json}`: count, max |x|, |y|, |twist|, the button
           values seen, first and last sample
  jcount   print `XYCOUNT n`: samples received since start
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


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--tree", type=pathlib.Path, required=True)
    p.add_argument("rest", nargs=argparse.REMAINDER)
    a = p.parse_args()
    rest = a.rest[1:] if a.rest[:1] == ["--"] else a.rest

    sys.path.insert(0, str(a.tree / "scripts"))
    import rtps_mcb_sim as sim  # noqa: E402

    lock = threading.Lock()
    rec = {"on": False, "samples": [], "count": 0}
    decode = sim.spec.unpack_xy_twist

    def recording_decode(payload: bytes):
        sample = decode(payload)
        if sample is not None:
            with lock:
                rec["count"] += 1
                if rec["on"]:
                    rec["samples"].append(tuple(sample))
        return sample

    sim.spec.unpack_xy_twist = recording_decode

    def summary() -> dict:
        with lock:
            s = list(rec["samples"])
        if not s:
            return {"count": 0}
        return {"count": len(s),
                "max_abs_x": max(abs(v[0]) for v in s),
                "max_abs_y": max(abs(v[1]) for v in s),
                "max_abs_twist": max(abs(v[2]) for v in s),
                "buttons_seen": sorted({int(v[3]) for v in s}),
                "first": s[0], "last": s[-1]}

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
