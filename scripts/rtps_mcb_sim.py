#!/usr/bin/env python3
"""Stand-in for the Main Control Board: drives the joystick's status labels.

The joystick HMI is a slave of the MCB — the DRIVE and STATE labels on its
StatusPanel show whatever arrives on ``rammp/mcb/status``. This script plays the
MCB from a laptop so that path can be exercised without the real board.

Topics, type names, enum values and the wire layout all come from
``rammp_rtps.py``, which scrapes ``main/rammp_rtps_spec.h`` — the same header the
firmware builds against, so there is nothing here to keep in sync by hand.

Usage:
  python rtps_mcb_sim.py                  # interactive: type a/i/ok/err, p/r to pause
  python rtps_mcb_sim.py --cycle          # rotate through every combination
  python rtps_mcb_sim.py --list-interfaces
  python rtps_mcb_sim.py --advertised-address 192.168.1.42

The status is republished every --period seconds, not only when it changes: the
writer is best-effort with no durability, so a joystick that reboots or joins
late would otherwise sit on a stale label until the next keypress here.

Multi-homed PCs (VirtualBox, Tailscale, WSL, VPNs) are the usual reason nothing
arrives: RTPS discovery goes out of exactly one interface, and the automatic
pick is often a virtual one. --list-interfaces shows the candidates and
--advertised-address forces the choice; --multicast-interface does the same for
the multicast join if it differs.

Coexists with rtps_host.py (participant 10), rtps_brightness.py (11) and
rtps_adc_plot.py (12) by using participant id 13.
"""

from __future__ import annotations

import argparse
import os
import random
import socket
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import rammp_rtps as spec  # noqa: E402  (path setup must run first)
import rtps_host  # noqa: E402
import rtps_drive_game  # noqa: E402
import rtps_net  # noqa: E402


#: Full stick deflection takes the emulated speed from 0 to max in roughly
#: this many seconds. Deliberately unhurried so the number is readable as it
#: moves rather than snapping to an end stop.
SPEED_FULL_TRAVEL_SECONDS = 3.0
#: Fraction of full deflection below which the stick counts as centred. The
#: firmware has its own, tighter deadzone; this one only has to stop a resting
#: stick from drifting the number.
SPEED_DEADZONE = 0.15
#: Samples averaged after connecting to establish where the stick actually
#: rests. About half a second at the firmware's 30 Hz publish rate — long
#: enough to average out ADC noise, short enough that it is over before anyone
#: has touched the stick.
CENTER_SAMPLE_COUNT = 15


class McbStatusPublisher(rtps_host.RtpsHostHarness):
    """Harness whose periodic publish sends a rammp_mcb_status_t."""

    def __init__(self, args: argparse.Namespace,
                 car: "rtps_drive_game.CarModel | None" = None) -> None:
        super().__init__(args)
        self.drive_status = spec.DRIVE_STATUS_INACTIVE
        self.system_state = spec.STATE_OK
        self.flags = 0
        # label overrides; empty means "let the HMI use the enum's own name"
        self.drive_text = ""
        self.state_text = ""
        self.error_text = ""
        self.error_footer = ""
        # Emulated chair speed in tenths. The GUI integrates the joystick into
        # this; the CLI just leaves it at zero.
        self.speed_tenths = 0
        # Latest joystick sample: (x, y, twist, buttons, drive_mode), or None.
        self.joystick: tuple[int, int, int, int, int] | None = None
        # The chair being simulated. It is the single source of speed: what the
        # Tab5 displays is read straight off it, so the number on the screen and
        # the car in the drive view cannot disagree. Accepted from the caller so
        # a GUI can own one that outlives any single connection, and keep its
        # drive window open across a disconnect.
        self.car = car if car is not None else rtps_drive_game.CarModel()
        # Emulated speed kept as a float so slow stick movements accumulate
        # instead of being lost to integer rounding every step.
        self._speed = 0.0
        self._last_speed_step = time.monotonic()
        # Measured resting position of the vertical axis. The spec's nominal
        # centre assumes an ideal divider; a real stick sits somewhere near it
        # (1506 mV against a nominal 1650 on the bench board). Left uncorrected
        # that standing offset makes the deadzone lopsided — pulling back would
        # trip far sooner than pushing forward — so take the first samples after
        # connecting, while the stick is at rest, as the true zero.
        self._center_y: float | None = None
        self._center_samples = 0
        self.seq = 0
        self.announced_targets = -1
        # Stop publishing without tearing the participant down, so the HMI's
        # stale path can be exercised and then recovered from without a fresh
        # discovery round confusing the picture.
        self.paused = False

        # ---- actuators -------------------------------------------------
        # The MCB owns every actuator position; the HMI only ever asks. These
        # are the values it asks about, seeded to the middle of each range so a
        # fresh bench session can step in both directions.
        self.actuator_values = [
            (a.min_value + a.max_value) // 2 for a in spec.ACTUATORS
        ]
        # Per-actuator override for the next request: None accepts it, an
        # RAMMP_ACTUATOR_RESULT_* value refuses it with that reason. This is
        # the whole point of the bench tool — the HMI's refusal paths are hard
        # to reach on a real chair without driving something into a hard stop.
        self.actuator_reject: list[int | None] = [None] * len(spec.ACTUATORS)
        self.actuator_req_id = 0
        self.actuator_result = spec.ACTUATOR_RESULT_OK
        self.actuator_seq = 0
        # Set by apply_actuator_command so the next run-loop tick publishes
        # immediately rather than waiting out the period: a reply that took up
        # to half a second would make every press feel broken.
        self.actuator_dirty = True

        # A second writer and a second reader, appended rather than passed
        # through argparse: RtpsHostHarness builds one writer from
        # --publish-topic and gives every --subscribe-topic the same type name,
        # and these two need their own topic/type pairs. Everything downstream
        # (SEDP announcement, target discovery) already loops over these lists.
        self.local_writers.append(rtps_host.WriterConfig(
            topic_name=spec.TOPIC_ACTUATOR_STATE,
            type_name=spec.TYPE_ACTUATOR_STATE,
            reliable=False,
            entity_index=len(self.local_writers),
        ))
        self.local_readers.append(rtps_host.ReaderConfig(
            topic_name=spec.TOPIC_ACTUATOR_COMMAND,
            type_name=spec.TYPE_ACTUATOR_COMMAND,
            reliable=False,
            entity_index=len(self.local_readers),
        ))

        # ---- self test ---------------------------------------------------
        # Every run of the HMI's self test needs a peer: its RTPS checks time
        # pings against it and read McbStatus from it. Serving that here means
        # this simulator, the GUI and rtps_selftest.py all answer a run however
        # it was started - over RTPS, or from the HMI's own R&D SELF TEST row.
        self.adc_rx_count = 0
        self.selftest_ping_rx = 0
        self._selftest_last_ping_seq = -1
        # Random start: the HMI ignores a command repeating the last run id it
        # acted on, and a restarted script counting from 1 could do exactly that.
        self.selftest_run_id = random.randint(1, 255)
        #: run_id -> {(kind, index): spec.SelfTestResult}; run 0 = started on the HMI
        self.selftest_reports: dict[int, dict[tuple[int, int], spec.SelfTestResult]] = {}
        #: run_id -> time.monotonic() its FINISHED sample first arrived
        self.selftest_finished_at: dict[int, float] = {}
        # The run request and the pongs go out on the bench command topic, and
        # the pings arrive on the bench counter topic - tagged, because the
        # HMI has no RTPS readers to spare for topics of their own (see "Self
        # test" in the spec header).
        self._selftest_command_writer = rtps_host.WriterConfig(
            topic_name=spec.TOPIC_HMI_COMMAND,
            type_name=spec.TYPE_UINT32,
            reliable=False,
            entity_index=len(self.local_writers),
        )
        self.local_writers.append(self._selftest_command_writer)
        for topic, type_name in ((spec.TOPIC_HMI_COUNTER, spec.TYPE_UINT32),
                                 (spec.TOPIC_SELFTEST_REPORT, spec.TYPE_SELFTEST_REPORT)):
            self.local_readers.append(rtps_host.ReaderConfig(
                topic_name=topic,
                type_name=type_name,
                reliable=False,
                entity_index=len(self.local_readers),
            ))

    def _send_on(self, writer: rtps_host.WriterConfig, cdr_payload: bytes) -> int:
        """Publish one sample on `writer`; returns how many targets it went to."""
        payload = self.build_data_message(writer, cdr_payload)
        targets = self._build_user_targets(writer)
        for destination in targets:
            self.send_user_datagram(payload, destination)
        return len(targets)

    def send_selftest_command(self, run_id: int) -> int:
        """(Re)send the run request for `run_id`. Safe to repeat: the HMI acts once."""
        return self._send_on(self._selftest_command_writer, spec.pack_selftest_run(run_id))

    def request_selftest(self) -> int:
        """Ask the HMI for a new self-test run; returns its run id."""
        self.selftest_run_id = self.selftest_run_id % 255 + 1  # 1..255, never 0
        self.send_selftest_command(self.selftest_run_id)
        return self.selftest_run_id

    def _answer_selftest_ping(self, payload: bytes) -> None:
        value = spec.unpack_uint32(payload)
        seq = spec.selftest_ping_seq(value) if value is not None else None
        if seq is None:
            return  # the counter's own bring-up heartbeat, not a ping
        # Count what arrives so the HMI can tell pings lost on the way here
        # from pongs lost on the way back. seq 0 opens a probe run; a seq going
        # backwards means that opening ping itself was lost.
        if seq == 0 or seq <= self._selftest_last_ping_seq:
            self.selftest_ping_rx = 0
        self._selftest_last_ping_seq = seq
        self.selftest_ping_rx += 1
        self._send_on(self._selftest_command_writer,
                      spec.pack_selftest_pong(seq, self.selftest_ping_rx))

    def _note_selftest_result(self, r: spec.SelfTestResult) -> None:
        if r.kind == spec.SELFTEST_KIND_STARTED:
            # STARTED is sent once, and only at the start: a new run under the
            # same id (every HMI-started run is id 0) replaces the old one
            self.selftest_reports[r.run_id] = {}
            self.selftest_finished_at.pop(r.run_id, None)
            rtps_host.log(f"[selftest] run {r.run_id}: started on firmware {r.detail}, "
                          f"{r.count} checks")
        run = self.selftest_reports.setdefault(r.run_id, {})
        key = (r.kind, r.index)
        is_new = key not in run
        run[key] = r
        if not is_new:
            return  # the end-of-run repeat of a sample already logged
        if r.kind == spec.SELFTEST_KIND_RESULT:
            rtps_host.log(f"[selftest] {spec.format_selftest_result(r)}")
        elif r.kind == spec.SELFTEST_KIND_FINISHED:
            self.selftest_finished_at[r.run_id] = time.monotonic()
            verdict = "PASS" if r.lo == 0 else "FAIL"
            rtps_host.log(f"[selftest] run {r.run_id}: {verdict} - {r.value} pass, {r.lo} fail, "
                          f"{r.hi} skip in {r.detail}")

    def apply_actuator_command(self, req_id: int, actuator_id: int, steps: int) -> int:
        """Judge one request from the HMI and return the RESULT_* verdict.

        Clamping lives here rather than on the HMI for the same reason it lives
        on a real MCB: one board owns the position, so there is only ever one
        opinion about whether a move is allowed.
        """
        self.actuator_req_id = req_id
        self.actuator_dirty = True
        if not 0 <= actuator_id < len(spec.ACTUATORS):
            self.actuator_result = spec.ACTUATOR_RESULT_UNKNOWN_ID
            return self.actuator_result

        actuator = spec.ACTUATORS[actuator_id]
        override = self.actuator_reject[actuator_id]
        if override is not None:
            self.actuator_result = override
            return self.actuator_result

        current = self.actuator_values[actuator_id]
        target = current + steps * actuator.step
        if target < actuator.min_value:
            # Report the limit rather than moving part way. A partial move
            # would leave the HMI showing a number the user did not ask for
            # and no indication that anything was refused.
            self.actuator_result = spec.ACTUATOR_RESULT_AT_MIN
        elif target > actuator.max_value:
            self.actuator_result = spec.ACTUATOR_RESULT_AT_MAX
        else:
            self.actuator_values[actuator_id] = target
            self.actuator_result = spec.ACTUATOR_RESULT_OK
        return self.actuator_result

    def publish_actuator_state(self) -> None:
        """Send the whole actuator state, as a reply and as the heartbeat."""
        if not self.local_writers or len(self.local_writers) < 2:
            return
        writer = self.local_writers[1]
        payload = self.build_data_message(
            writer,
            spec.pack_actuator_state(self.actuator_values, self.actuator_req_id,
                                     self.actuator_result, self.actuator_seq),
        )
        for destination in self._build_user_targets(writer):
            self.send_user_datagram(payload, destination)
        self.actuator_seq = (self.actuator_seq + 1) & 0xFF
        self.actuator_dirty = False

    def handle_user_packet(self, packet: bytes, sender_ip: str, sender_port: int) -> None:
        """Capture joystick samples and actuator requests."""
        for guid_prefix, writer_id, payload, reader_id in rtps_host.parse_rtps_data_messages(
            packet
        ):
            topic = self.topic_for_sample(guid_prefix, writer_id, reader_id)
            if topic == spec.TOPIC_JOYSTICK_ADC:
                sample = spec.unpack_adc_xy_twist(payload)
                if sample is not None:
                    self.joystick = sample
                    self.adc_rx_count += 1
            elif topic == spec.TOPIC_HMI_COUNTER:
                self._answer_selftest_ping(payload)
            elif topic == spec.TOPIC_SELFTEST_REPORT:
                result = spec.unpack_selftest_report(payload)
                if result is not None:
                    self._note_selftest_result(result)
            elif topic == spec.TOPIC_ACTUATOR_COMMAND:
                command = spec.unpack_actuator_command(payload)
                if command is None:
                    continue
                req_id, actuator_id, steps = command
                result = self.apply_actuator_command(req_id, actuator_id, steps)
                name = (spec.ACTUATORS[actuator_id].short
                        if 0 <= actuator_id < len(spec.ACTUATORS) else f"#{actuator_id}")
                rtps_host.log(
                    f"[actuator] req {req_id}: {name} {steps:+d} step -> "
                    f"{spec.ACTUATOR_RESULT_NAMES.get(result, '?')}"
                )
                # Answer immediately. The periodic republish below is the
                # convergence path, not the reply path.
                self.publish_actuator_state()

    @property
    def center_y(self) -> float:
        """Measured resting position of the vertical axis, or the spec nominal
        until enough samples have arrived to establish it."""
        return self._center_y if self._center_y is not None else spec.JOYSTICK_CENTER_MV

    def step_speed(self, _dt: float = 0.0) -> None:
        """Advance the simulated chair and take its speed.

        The dt is ignored: CarModel.update() reads the clock itself, so the
        publisher ticking it twice a second and the drive window ticking it
        thirty times a second produce the same trajectory rather than
        double-integrating each other's steps.
        """
        if self.joystick is None:
            return
        x_mv, y_mv, twist_mv = self.joystick[0], self.joystick[1], self.joystick[2]
        if self.car.note_center_sample(x_mv, y_mv, twist_mv, CENTER_SAMPLE_COUNT):
            return  # still learning where the stick rests; do not drive on it yet
        drive_mode = self.joystick[4] if len(self.joystick) > 4 else None
        self.car.update(self.joystick, drive_mode)
        self.speed_tenths = self.car.speed_tenths

    def describe(self) -> str:
        return (
            f"drive={spec.DRIVE_STATUS_NAMES.get(self.drive_status, '?')} "
            f"state={spec.STATE_NAMES.get(self.system_state, '?')} "
            f"flags=0x{self.flags:02x}"
            + (f" drive_text='{self.drive_text}'" if self.drive_text else "")
            + (f" state_text='{self.state_text}'" if self.state_text else "")
            + f" speed={self.speed_tenths / 10:.1f}"
            + (" [PAUSED]" if self.paused else "")
        )

    def publish_now(self) -> None:
        """Called by the harness run loop every --period seconds."""
        if self.paused:
            return
        # Actuator state rides the same tick. Republished even when unchanged,
        # so a joystick that just booted or just reconnected learns where the
        # actuators are without the user having to press anything.
        self.publish_actuator_state()
        # Advance the emulated speed on real elapsed time. Clamped so a long
        # gap (a pause, a breakpoint) cannot lurch the number across its range
        # in a single step.
        now = time.monotonic()
        self.step_speed(min(now - self._last_speed_step, 0.5))
        self._last_speed_step = now
        writer = self.local_writers[0]
        payload = self.build_data_message(
            writer,
            spec.pack_mcb_status(self.drive_status, self.system_state, self.flags, self.seq,
                                 self.speed_tenths, self.drive_text, self.state_text,
                                 self.error_text, self.error_footer),
        )
        targets = self._build_user_targets(writer)
        for destination in targets:
            self.send_user_datagram(payload, destination)
        self.seq = (self.seq + 1) & 0xFF

        # Log only when the number of reachable subscribers changes: silence
        # here means discovery never matched, which is the failure worth
        # noticing, and a per-sample log would bury it.
        if len(targets) != self.announced_targets:
            self.announced_targets = len(targets)
            if targets:
                rtps_host.log(
                    f"[mcb] publishing {self.describe()} to {len(targets)} subscriber(s): "
                    + ", ".join(f"{ip}:{port}" for ip, port in targets)
                )
            else:
                rtps_host.log(
                    f"[mcb] no subscriber for '{writer.topic_name}' yet — waiting for the "
                    "joystick's SPDP announcement"
                )


def local_ipv4_addresses() -> list[str]:
    """This host's IPv4 addresses. Kept for callers that want bare strings."""
    return [adapter.ip for adapter in rtps_net.list_adapters()]

def resolve_endpoints(cli: argparse.Namespace) -> tuple[str | None, str | None]:
    """Work out which board to talk to and which adapter to do it from.

    Order: what was asked for on the command line, then what worked last time,
    then passive discovery. The adapter is derived from the board rather than
    chosen independently — routing to the board is the only question whose
    answer is guaranteed to be the right adapter.
    """
    config = rtps_net.load_config()
    peer = (cli.peer[0] if cli.peer else None) or config.get("peer")
    if peer and not rtps_net.probe_board(peer, timeout=2.0):
        print(f"no answer from {peer}; looking for the board...")
        peer = None
    if peer is None:
        peer = rtps_net.discover_board(config.get("peer"), progress=print)
    advertised = cli.advertised_address or (
        rtps_net.source_address_for(peer) if peer else None
    ) or config.get("advertised_address")
    return peer, advertised



def build_harness_args(cli: argparse.Namespace) -> argparse.Namespace:
    # Falls back to the route to the peer, not the route to the internet:
    # guess_local_ipv4() names the adapter that reaches 8.8.8.8, which on a
    # bench network is usually the wrong one.
    advertised = (cli.advertised_address
                  or (rtps_net.source_address_for(cli.peer[0]) if cli.peer else None)
                  or rtps_host.guess_local_ipv4())
    return argparse.Namespace(
        node_name=cli.node_name,
        domain_id=cli.domain_id,
        participant_id=cli.participant_id,
        bind_address=cli.bind_address or advertised,
        advertised_address=advertised,
        multicast_interface=cli.multicast_interface,
        multicast_group=cli.multicast_group,
        enclave="/",
        # Subscribe to the joystick stream as well as publishing status, so the
        # emulated speed can follow the stick.
        subscribe_topic=[spec.TOPIC_JOYSTICK_ADC],
        subscribe_type_name=spec.TYPE_ADC_XY_TWIST,
        publish_topic=spec.TOPIC_MCB_STATUS,
        publish_value=0,  # unused: publish_now() is overridden
        publish_interval=cli.period,
        echo_received=False,
        reliable=False,
        type_name=spec.TYPE_MCB_STATUS,
        announce_period=1.0,
        duration=0.0,
        trace_packets=cli.trace_packets,
        peer=cli.peer,
        peer_participant_ids=cli.peer_participant_ids,
    )


HELP_TEXT = """commands:
  a / active      drive status -> ACTIVE
  i / inactive    drive status -> INACTIVE
  ok              state -> OK
  e / err         state -> ERROR
  f <hex>         reserved flags byte (e.g. 'f 01')
  et <text>       error banner body ('et' alone clears it)
  ef <text>       error banner footer ('ef' alone clears it)
  dt <text>       override the DRIVE label text ('dt' alone clears it)
  st <text>       override the STATE label text ('st' alone clears it)
  p               pause publishing (HMI should go stale after
                  RAMMP_MCB_STATUS_TIMEOUT_MS: blinking orange RTPS, '---')
  r               resume publishing (HMI should go straight back to green)
  <enter>         show what is being published
  q               quit"""


def run_interactive(harness: McbStatusPublisher) -> None:
    print(HELP_TEXT)
    while True:
        try:
            command = input("mcb> ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            return
        if command in ("q", "quit", "exit"):
            return
        if command in ("a", "active"):
            harness.drive_status = spec.DRIVE_STATUS_ACTIVE
        elif command in ("i", "inactive"):
            harness.drive_status = spec.DRIVE_STATUS_INACTIVE
        elif command == "ok":
            harness.system_state = spec.STATE_OK
        elif command in ("e", "err", "error"):
            harness.system_state = spec.STATE_ERROR
        elif command == "et" or command.startswith("et "):
            harness.error_text = command[3:].strip()
        elif command == "ef" or command.startswith("ef "):
            harness.error_footer = command[3:].strip()
        elif command == "dt" or command.startswith("dt "):
            harness.drive_text = command[3:].strip()
        elif command == "st" or command.startswith("st "):
            harness.state_text = command[3:].strip()
        elif command in ("p", "pause"):
            harness.paused = True
            print(f"  {harness.describe()}")
            continue
        elif command in ("r", "resume"):
            harness.paused = False
        elif command.startswith("f "):
            try:
                harness.flags = int(command[2:].strip(), 16) & 0xFF
            except ValueError:
                print("  flags must be hex, e.g. 'f 01'")
                continue
        elif command in ("h", "help", "?"):
            print(HELP_TEXT)
            continue
        elif command:
            print(f"  unknown command '{command}' — 'h' for help")
            continue
        # publish immediately so the label follows the keystroke rather than
        # the next period; the periodic republish continues underneath
        harness.publish_now()
        print(f"  {harness.describe()}")


def run_cycle(harness: McbStatusPublisher, dwell: float) -> None:
    combinations = [
        (spec.DRIVE_STATUS_INACTIVE, spec.STATE_OK),
        (spec.DRIVE_STATUS_ACTIVE, spec.STATE_OK),
        (spec.DRIVE_STATUS_ACTIVE, spec.STATE_ERROR),
        (spec.DRIVE_STATUS_INACTIVE, spec.STATE_ERROR),
    ]
    stale_gap = max(dwell, spec.MCB_STATUS_TIMEOUT_MS / 1000.0 + 1.0)
    print(f"Cycling every {dwell:.1f}s; Ctrl-C to stop.")
    try:
        while True:
            for drive_status, system_state in combinations:
                harness.drive_status = drive_status
                harness.system_state = system_state
                harness.publish_now()
                print(f"  {harness.describe()}")
                time.sleep(dwell)
            # One gap per lap, longer than the timeout, so the link-loss path
            # gets exercised too: the HMI should blink its RTPS label orange and
            # show '---' for drive/state, then recover on the next publish.
            harness.paused = True
            print(f"  paused {stale_gap:.1f}s - HMI should go stale")
            time.sleep(stale_gap)
            harness.paused = False
    except KeyboardInterrupt:
        return


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Emulate the Main Control Board's status broadcast over RTPS."
    )
    parser.add_argument(
        "--cycle", action="store_true",
        help="Rotate through every drive-status/state combination instead of reading commands")
    parser.add_argument("--dwell", type=float, default=3.0,
                        help="Seconds per combination in --cycle mode (default 3)")
    parser.add_argument(
        "--period", type=float, default=spec.MCB_STATUS_PERIOD_MS / 1000.0,
        help="Seconds between republishes of the current status (default from the spec header's "
             f"RAMMP_MCB_STATUS_PERIOD_MS = {spec.MCB_STATUS_PERIOD_MS} ms). Anything longer than "
             f"RAMMP_MCB_STATUS_TIMEOUT_MS ({spec.MCB_STATUS_TIMEOUT_MS} ms) makes the HMI declare "
             "the link stale between perfectly good samples.")
    parser.add_argument("--list-interfaces", action="store_true",
                        help="Print this host's IPv4 addresses and exit")
    parser.add_argument("--node-name", default="mcb_sim", help="Local participant name")
    parser.add_argument("--domain-id", type=int, default=0,
                        help="RTPS domain id (must match the Tab5)")
    parser.add_argument(
        "--participant-id", type=int, default=13,
        help="Local participant id; keep distinct from the other scripts (10, 11, 12)",
    )
    parser.add_argument("--bind-address", default=None, help="Local bind address")
    parser.add_argument(
        "--advertised-address", default=None,
        help="IPv4 address to advertise; set this when the automatic pick lands on a "
             "VirtualBox/Tailscale/WSL adapter (see --list-interfaces)")
    parser.add_argument(
        "--multicast-interface", default=None,
        help="IPv4 interface for the multicast join/send, if it differs from --advertised-address")
    parser.add_argument("--multicast-group", default="239.255.0.1",
                        help="RTPS metatraffic multicast group")
    parser.add_argument(
        "--peer", action="append", default=None, metavar="HOST",
        help="Hostname or IP of the joystick, to reach it without multicast discovery "
             "(repeatable). Use this when it is routed rather than on-link — over Tailscale, a "
             "VPN or another subnet — since none of those carry multicast. The Tab5 is usually "
             "'espressif'.")
    parser.add_argument(
        "--peer-participant-ids", type=rtps_host.parse_participant_id_range, default="0-3",
        metavar="IDS",
        help="Participant ids to try on each --peer, as '0-3' or '0,1,2' (default 0-3)")
    parser.add_argument("--trace-packets", action="store_true",
                        help="Log every received UDP packet and its RTPS submessage headers")
    cli = parser.parse_args()

    if cli.list_interfaces:
        print("network adapters (pass an address as --advertised-address):")
        for adapter in rtps_net.list_adapters():
            print(f"  {adapter.label()}")
        return 0

    peer, advertised = resolve_endpoints(cli)
    if peer is None:
        print("Could not find the board. Pass --peer, or run rtps_mcb_gui.py and use Scan.")
        return 1
    cli.peer = [peer]
    cli.advertised_address = advertised
    print(f"board {peer} via {advertised}")
    if cli.period <= 0:
        parser.error("--period must be > 0")

    args = build_harness_args(cli)
    print(f"spec header: {spec.HEADER_PATH}")
    print(f"advertised address: {args.advertised_address} "
          "(--list-interfaces shows the alternatives)")
    harness = McbStatusPublisher(args)
    network = threading.Thread(target=harness.run, daemon=True)
    network.start()
    rtps_net.save_config({"peer": peer, "advertised_address": args.advertised_address,
                          "period": cli.period})

    print(f"Publishing '{spec.TOPIC_MCB_STATUS}' [{spec.TYPE_MCB_STATUS}] "
          f"every {cli.period:.2f}s\n")
    if cli.cycle:
        run_cycle(harness, cli.dwell)
    else:
        run_interactive(harness)
    return 0


if __name__ == "__main__":
    sys.exit(main())
