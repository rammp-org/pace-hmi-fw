#!/usr/bin/env python3
"""Run the joystick HMI's self test over RTPS and report the verdict.

Every check the HMI makes, and the limits it is held to, live in
main/selftest_spec.h. This script asks for a run, collects the report and
exits with the verdict, so a firmware change can be checked objectively
instead of by reading the code:

  python rtps_selftest.py                  # run once; exit 0 pass, 1 fail, 2 no answer
  python rtps_selftest.py --json out.json  # also save the report
  python rtps_selftest.py --serve          # answer pings and print every report, for
                                           # runs started from the HMI's SELF TEST row

It plays the MCB while it runs (it is an rtps_mcb_sim.McbStatusPublisher): the
HMI's RTPS checks need McbStatus arriving and their pings answered, and a run
requested from here holds the HMI to all of them (ST_REMOTE in the spec). So
close rtps_mcb_gui.py / rtps_mcb_sim.py first - two MCBs publishing at once
would show up as McbStatus loss - or use the GUI's own "Run self test" button.

On top of the HMI's checks it adds three only this side can make, prefixed
pc.: that the report arrived whole, that it lists exactly the checks in this
checkout's selftest_spec.h (catches a board running other firmware than you
think), and the joystick sample rate as received here.

The board and adapter are found the way rtps_mcb_sim.py finds them: --peer and
--advertised-address, else what worked last time, else discovery.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import rammp_rtps as spec  # noqa: E402  (path setup must run first)
import rtps_host  # noqa: E402
import rtps_mcb_sim  # noqa: E402

SELFTEST_SPEC_PATH = os.path.join(os.path.dirname(spec.HEADER_PATH), "selftest_spec.h")
# X(ID, "name", "unit", lo, hi, need, "what it proves")
_ROW_RE = re.compile(
    r'^\s*X\(\s*([A-Z0-9_]+)\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*([^,]+?)\s*,\s*([^,]+?)\s*,'
    r'\s*(ST_[A-Z]+)\s*,\s*"([^"]*)"\s*\)', re.M)

#: after FINISHED, how long the end-of-run repeat gets to fill dropped rows
REPORT_SETTLE_S = 3.0
#: resend the command this often until the HMI acknowledges with STARTED
COMMAND_RESEND_S = 1.0
COMMAND_ATTEMPTS = 6
#: how long pc.adc_rx_hz counts joystick samples, before the run starts
ADC_RATE_WINDOW_S = 3.0

EXIT_PASS, EXIT_FAIL, EXIT_NO_ANSWER = 0, 1, 2


class SpecRow:
    def __init__(self, name: str, unit: str, lo: str, hi: str, need: str, desc: str) -> None:
        self.name = name
        self.unit = unit
        self.lo = spec.INT32_MIN if lo.startswith("ST_ANY") else int(lo, 0)
        self.hi = spec.INT32_MAX if hi.startswith("ST_ANY") else int(hi, 0)
        self.need = need
        self.desc = desc


def load_spec() -> dict[str, SpecRow]:
    """This checkout's selftest_spec.h, keyed by check name."""
    with open(SELFTEST_SPEC_PATH, encoding="utf-8") as handle:
        rows = _ROW_RE.findall(handle.read())
    if not rows:
        raise RuntimeError(f"{SELFTEST_SPEC_PATH}: SELFTEST_TABLE parsed to nothing")
    return {name: SpecRow(name, unit, lo, hi, need, desc)
            for _id, name, unit, lo, hi, need, desc in rows}


def pc_check(index: int, name: str, value: int, lo: int, hi: int, unit: str = "",
             detail: str = "") -> spec.SelfTestResult:
    passed = lo <= value <= hi
    return spec.SelfTestResult(
        run_id=0, kind=spec.SELFTEST_KIND_RESULT, index=index, count=0,
        result=spec.SELFTEST_RESULT_PASS if passed else spec.SELFTEST_RESULT_FAIL,
        value=value, lo=lo, hi=hi, name=name, unit=unit, detail=detail)


def wait_for_board(harness: rtps_mcb_sim.McbStatusPublisher, timeout: float) -> bool:
    """Until the HMI streams joystick samples to us.

    That is the sign it has matched our readers from our announcements, and so
    will hear our command writer too. A couple of status periods more then let
    its link state reach CONNECTED before the run looks at it.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if harness.adc_rx_count > 0:
            time.sleep(3 * spec.MCB_STATUS_PERIOD_MS / 1000.0)  # noqa: F821  (scraped)
            return True
        time.sleep(0.1)
    return False


def collect(harness: rtps_mcb_sim.McbStatusPublisher, run_id: int, timeout: float,
            resend: bool) -> dict:
    """Wait for run `run_id` to finish; returns what arrived and when."""
    requested = time.monotonic()
    deadline = requested + timeout
    attempts = 1
    started_at = finished_at = None
    while time.monotonic() < deadline:
        now = time.monotonic()
        run = harness.selftest_reports.get(run_id, {})
        if started_at is None and (spec.SELFTEST_KIND_STARTED, 0) in run:
            started_at = now
            firmware = run[(spec.SELFTEST_KIND_STARTED, 0)].detail
            print(f"run {run_id} started on firmware {firmware}; about 15 s...", flush=True)
        if (resend and started_at is None and attempts < COMMAND_ATTEMPTS
                and now - requested >= attempts * COMMAND_RESEND_S):
            harness.send_selftest_command(run_id)
            attempts += 1
        if finished_at is None and run_id in harness.selftest_finished_at:
            finished_at = now
        if finished_at is not None:
            count = run[(spec.SELFTEST_KIND_FINISHED, 0)].count
            have = sum(1 for kind, _index in run if kind == spec.SELFTEST_KIND_RESULT)
            if have >= count or now - finished_at >= REPORT_SETTLE_S:
                break
        time.sleep(0.05)
    return {"run": dict(harness.selftest_reports.get(run_id, {}))}


def judge(run_id: int, got: dict, rows: dict[str, SpecRow]) -> tuple[int, dict]:
    """Print the table; returns (exit code, the report as a dict)."""
    run = got["run"]
    started = run.get((spec.SELFTEST_KIND_STARTED, 0))
    finished = run.get((spec.SELFTEST_KIND_FINISHED, 0))
    if started is None:
        print(f"run {run_id}: no report from the HMI (is its firmware new enough to have a "
              "self test, and is nothing else talking to it as the MCB?)")
        return EXIT_NO_ANSWER, {"run_id": run_id, "verdict": "NO_ANSWER"}

    device = sorted((r for (kind, _i), r in run.items() if kind == spec.SELFTEST_KIND_RESULT),
                    key=lambda r: r.index)
    count = started.count

    # --- checks only this side can make -----------------------------------
    pc: list[spec.SelfTestResult] = []
    missing = sorted(set(range(count)) - {r.index for r in device})
    pc.append(pc_check(len(pc), "pc.report_complete", len(device), count, count,
                       detail=f"missing rows {missing}" if missing else
                       ("" if finished else "no FINISHED sample")))
    if finished is None:
        pc[-1] = pc[-1]._replace(result=spec.SELFTEST_RESULT_FAIL, detail="run never finished")
    board_names = {r.name for r in device}
    spec_names = set(rows)
    only_board = sorted(board_names - spec_names)
    only_spec = sorted(spec_names - board_names) if not missing else []
    mismatch = ", ".join([f"board only: {n}" for n in only_board] +
                         [f"spec only: {n}" for n in only_spec])
    pc.append(pc_check(len(pc), "pc.spec_match", 0 if mismatch else 1, 1, 1,
                       detail=mismatch[:120]))
    adc_row = rows.get("rtps.adc_hz")
    if adc_row is not None and got.get("adc_rate") is not None:
        count, seconds = got["adc_rate"]
        pc.append(pc_check(len(pc), "pc.adc_rx_hz", int(round(count / seconds)), adc_row.lo,
                           adc_row.hi, "Hz",
                           detail=f"{count} joystick samples in {seconds:.1f} s before the run"))

    # --- the table ---------------------------------------------------------
    print(f"\nself test run {run_id} - firmware {started.detail}\n")
    print(f"{'':4}  {'check':<18} {'measured':>14}  {'limit':<20} detail")
    for r in device + pc:
        row = rows.get(r.name)
        print(spec.format_selftest_result(r, r.detail or (row.desc if row else "")))

    results = device + pc
    fails = [r for r in results if r.result == spec.SELFTEST_RESULT_FAIL]
    skips = [r for r in results if r.result == spec.SELFTEST_RESULT_SKIP]
    passes = len(results) - len(fails) - len(skips)
    verdict = "PASS" if not fails else "FAIL"
    duration = finished.detail if finished else "?"
    print(f"\n{verdict}: {passes} pass, {len(fails)} fail, {len(skips)} skip "
          f"(HMI run time {duration})")
    for r in fails:
        print(f"  FAIL {r.name}: {spec.format_selftest_value(r)} "
              f"(limit {spec.format_selftest_limits(r)}) {r.detail}")

    report = {
        "run_id": run_id,
        "firmware": started.detail,
        "verdict": verdict,
        "hmi_run_time": duration,
        "results": [
            {
                "name": r.name,
                "result": spec.SELFTEST_RESULT_NAMES.get(r.result, "?"),
                "value": None if r.result == spec.SELFTEST_RESULT_SKIP else r.value,
                "unit": r.unit,
                "lo": None if r.lo == spec.INT32_MIN else r.lo,
                "hi": None if r.hi == spec.INT32_MAX else r.hi,
                "detail": r.detail,
            }
            for r in results
        ],
    }
    return (EXIT_PASS if not fails else EXIT_FAIL), report


def serve(harness: rtps_mcb_sim.McbStatusPublisher, rows: dict[str, SpecRow]) -> int:
    print("serving: pings answered, McbStatus published. Start a run from the HMI's "
          "SELF TEST row; Ctrl-C to stop.", flush=True)
    shown: set[tuple[int, float]] = set()
    try:
        while True:
            for run_id, finished_at in list(harness.selftest_finished_at.items()):
                if (run_id, finished_at) in shown or time.monotonic() - finished_at < REPORT_SETTLE_S:
                    continue
                shown.add((run_id, finished_at))
                run = harness.selftest_reports.get(run_id, {})
                judge(run_id, {"run": dict(run)}, rows)
            time.sleep(0.2)
    except KeyboardInterrupt:
        return EXIT_PASS


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run the joystick HMI's self test over RTPS; exit 0 pass, 1 fail, 2 no answer.")
    parser.add_argument("--serve", action="store_true",
                        help="Do not start a run: answer pings and print every report that "
                             "arrives (for runs started on the HMI itself)")
    parser.add_argument("--json", metavar="PATH", help="Also write the report here as JSON")
    parser.add_argument("--timeout", type=float, default=90.0,
                        help="Seconds to wait for the report once requested (default 90)")
    parser.add_argument("--connect-timeout", type=float, default=45.0,
                        help="Seconds to wait for the HMI to appear (default 45)")
    parser.add_argument("--verbose", action="store_true",
                        help="Show the RTPS harness log as well as the report")
    parser.add_argument("--period", type=float, default=spec.MCB_STATUS_PERIOD_MS / 1000.0,  # noqa: F821
                        help="Seconds between McbStatus republishes (default from the spec)")
    parser.add_argument("--node-name", default="selftest", help="Local participant name")
    parser.add_argument("--domain-id", type=int, default=0, help="RTPS domain id")
    parser.add_argument("--participant-id", type=int, default=15,
                        help="Local participant id; distinct from the other scripts (10-14)")
    parser.add_argument("--bind-address", default=None, help="Local bind address")
    parser.add_argument("--advertised-address", default=None,
                        help="IPv4 address to advertise (see rtps_mcb_sim.py --list-interfaces)")
    parser.add_argument("--multicast-interface", default=None,
                        help="IPv4 interface for the multicast join/send, if it differs")
    parser.add_argument("--multicast-group", default="239.255.0.1",
                        help="RTPS metatraffic multicast group")
    parser.add_argument("--peer", action="append", default=None, metavar="HOST",
                        help="Hostname or IP of the joystick (repeatable)")
    parser.add_argument("--peer-participant-ids", type=rtps_host.parse_participant_id_range,
                        default="0-3", metavar="IDS",
                        help="Participant ids to try on each --peer (default 0-3)")
    parser.add_argument("--trace-packets", action="store_true",
                        help="Log every received UDP packet (implies --verbose)")
    cli = parser.parse_args()

    rows = load_spec()
    if not (cli.verbose or cli.trace_packets):
        # The harness narrates discovery on stdout; the report is what matters
        # here, so keep only lines that say something went wrong.
        rtps_host.log = lambda message: (
            print(message, flush=True)
            if "fail" in message.lower() and not message.startswith("[selftest]") else None)

    # Keep looking until --connect-timeout rather than giving up on the first
    # miss: the natural moment to run this is straight after flashing, while
    # the board is still rebooting and waiting on DHCP.
    give_up = time.monotonic() + cli.connect_timeout
    requested_peer = cli.peer
    while True:
        cli.peer = requested_peer
        peer, advertised = rtps_mcb_sim.resolve_endpoints(cli)
        if peer is not None or time.monotonic() >= give_up:
            break
        time.sleep(3.0)
    if peer is None:
        print("Could not find the board. Pass --peer, or run rtps_mcb_gui.py and use Scan.")
        return EXIT_NO_ANSWER
    cli.peer = [peer]
    cli.advertised_address = advertised
    print(f"board {peer} via {advertised}; spec {SELFTEST_SPEC_PATH} ({len(rows)} checks)",
          flush=True)

    harness = rtps_mcb_sim.McbStatusPublisher(rtps_mcb_sim.build_harness_args(cli))
    network = threading.Thread(target=harness.run, daemon=True)
    network.start()
    try:
        if not wait_for_board(harness, cli.connect_timeout):
            print(f"the HMI did not appear within {cli.connect_timeout:.0f} s "
                  "(no joystick samples arrived)")
            return EXIT_NO_ANSWER
        if cli.serve:
            return serve(harness, rows)
        # The link's own delivery rate, measured before the run: during it the
        # HMI is deliberately loaded (full-screen redraws), and that must not
        # read as loss on the way here.
        before, since = harness.adc_rx_count, time.monotonic()
        time.sleep(ADC_RATE_WINDOW_S)
        adc_rate = (harness.adc_rx_count - before, time.monotonic() - since)
        run_id = harness.request_selftest()
        got = collect(harness, run_id, cli.timeout, resend=True)
        got["adc_rate"] = adc_rate
        code, report = judge(run_id, got, rows)
        if cli.json:
            with open(cli.json, "w", encoding="utf-8") as handle:
                json.dump(report, handle, indent=2)
            print(f"\nreport written to {cli.json}")
        return code
    finally:
        harness.stop()
        network.join(timeout=2.0)


if __name__ == "__main__":
    sys.exit(main())
