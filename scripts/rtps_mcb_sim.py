#!/usr/bin/env python3
"""Stand-in for the Main Control Board: drives the joystick's status labels.

The joystick HMI is a slave of the MCB: the DRIVE and STATE labels on its
StatusPanel show whatever arrives on ``rammp/mib/status``, driving starts
only once the MCB says it has, and the seat moves only when the MCB says it
moved. This script plays the MCB from a laptop so all of that can be exercised
without the real board — it answers DriveCommand and SeatCommand the way the
board is meant to, and can refuse either on demand.

Topics, type names, enum values and the wire layout all come from
``rammp_rtps.py``, which scrapes the RTPS spec headers — the same header the
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
import datetime
import math
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


#: Why the bench refused a seat request. Local to this script: MibStatus has no
#: per-request verdict, so a refusal never goes on the wire - on the HMI it shows up
#: as an axis that simply did not move.
SEAT_RESULT_OK = 0
SEAT_RESULTS = {0: "OK", 1: "AT_MIN", 2: "AT_MAX", 3: "INHIBITED", 4: "UNKNOWN_AXIS"}

#: Full stick deflection takes the emulated speed from 0 to max in roughly
#: this many seconds. Deliberately unhurried so the number is readable as it
#: moves rather than snapping to an end stop.
SPEED_FULL_TRAVEL_SECONDS = 3.0
#: Fraction of full deflection below which the stick counts as centred. The
#: firmware has its own, tighter deadzone; this one only has to stop a resting
#: stick from drifting the number.
SPEED_DEADZONE = 0.15


class SystemStatePublisher(rtps_host.RtpsHostHarness):
    """Plays the MCB: publishes SystemState, and answers the joystick's commands."""

    def __init__(self, args: argparse.Namespace,
                 car: "rtps_drive_game.CarModel | None" = None) -> None:
        super().__init__(args)
        # One field where there were two: MibSystemState says both whether the chair
        # drives (ENABLED) and whether it is faulted (ERROR).
        self.system_state = spec.MIB_SYSTEM_STATE_IDLE
        # What the joystick last asked for, and whether this MCB plays along.
        # Refusing is the interesting case: the HMI has to give up on its own
        # timeout rather than sit on a screen that pretends the chair drives.
        self.drive_request = spec.DRIVE_REQUEST_DISABLE
        self.refuse_drive = False
        # The other half: a MIB that will not stop. Leaving the drive screen is a
        # request too, so this is how the bench makes that one fail.
        self.refuse_stop = False
        # The profile rides DriveCommand; the HMI waits to see it come back in
        # SystemState before it believes the change took. Two of them, because a
        # real MCB is allowed to disagree: `requested_profile` is what the
        # joystick asked for and `profile` is what SystemState reports. With
        # follow_profile cleared the bench owns the second one, which is the
        # only way to see what the HMI does when a profile change is not granted.
        self.profile = spec.DRIVE_PROFILE_NORMAL
        self.requested_profile = spec.DRIVE_PROFILE_NORMAL
        self.follow_profile = True
        # label override; empty means "let the HMI use the enum's own name"
        self.status_text = ""
        self.error_message = ""
        self.error_footer = ""
        # Emulated chair speed in tenths. The GUI integrates the joystick into
        # this; the CLI just leaves it at zero.
        self.speed_tenths = 0
        # Latest joystick sample: (x, y, twist, buttons), or None. Axes are
        # -1..+1, already calibrated by the HMI. The profile is not in here: it
        # arrives on DriveCommand, which is the only thing that changes it.
        self.joystick: tuple[float, float, float, int] | None = None
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
        self.seq = 0
        self.announced_targets = -1
        # Stop publishing without tearing the participant down, so the HMI's
        # stale path can be exercised and then recovered from without a fresh
        # discovery round confusing the picture.
        self.paused = False

        # ---- seat -------------------------------------------------------
        # The MCB owns every seat position; the HMI only ever asks, with an
        # absolute target. Seeded to the middle of each range so a fresh bench
        # session can move in both directions.
        self.seat_values = [
            (a.min_value + a.max_value) // 2 for a in spec.SEAT_AXES
        ]
        # Per-axis override for the next request: None judges it normally, a
        # SEAT_RESULT_* value refuses it with that reason. This is the whole
        # point of the bench tool — the HMI's refusal paths are hard to reach
        # on a real chair without driving something into a hard stop.
        self.seat_reject: list[int | None] = [None] * len(spec.SEAT_AXES)
        self.seat_result = SEAT_RESULT_OK
        self.seat_last_axis = 0
        self.seat_seq = 0
        #: (axis_id, target) of the last SeatCommand, for the bench to show
        self.last_seat_command: tuple[int, int] | None = None
        # Set by apply_seat_command so the next run-loop tick publishes
        # immediately rather than waiting out the period: a reply that took up
        # to half a second would make every press feel broken.
        self.seat_dirty = True

        # A second writer and a second reader, appended rather than passed
        # through argparse: RtpsHostHarness builds one writer from
        # --publish-topic and gives every --subscribe-topic the same type name,
        # and these two need their own topic/type pairs. Everything downstream
        # (SEDP announcement, target discovery) already loops over these lists.
        # No seat writer any more: the seat rides MibStatus, which the harness's own
        # periodic publish sends.
        for topic, type_name in ((spec.TOPIC_JOYSTICK_SEAT_COMMAND, spec.TYPE_SEAT_COMMAND),
                                 (spec.TOPIC_JOYSTICK_DRIVE_COMMAND, spec.TYPE_DRIVE_COMMAND)):
            self.local_readers.append(rtps_host.ReaderConfig(
                topic_name=topic,
                type_name=type_name,
                reliable=False,
                entity_index=len(self.local_readers),
            ))

        # Diagnostics: fake, slowly changing readings for every item in the
        # spec's RAMMP_DIAG_TABLE, sent with each status tick. Turning them off
        # (the GUI's checkbox) is how the HMI's stale display gets tested.
        self.diagnostics_enabled = True
        self.diagnostics_seq = 0
        self.diagnostics_values: list[list[int]] = []
        self._diagnostics_writer = rtps_host.WriterConfig(
            topic_name=spec.TOPIC_MCB_DIAGNOSTICS,
            type_name=spec.TYPE_DIAGNOSTICS,
            reliable=False,
            entity_index=len(self.local_writers),
        )
        self.local_writers.append(self._diagnostics_writer)

        # ---- self test ---------------------------------------------------
        # Every run of the HMI's self test needs a peer: its RTPS checks time
        # pings against it and read SystemState from it. Serving that here means
        # this simulator, the GUI and rtps_selftest.py all answer a run however
        # it was started - over RTPS, or from the HMI's own SELF TEST row.
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

    def apply_drive_command(self, request: int, profile: int) -> None:
        """Take one DriveCommand from the joystick.

        The MCB is what decides whether the chair drives — the joystick asks
        and waits. With refuse_drive set, the request is recorded and ignored,
        which is what a chair inhibited by a fault or a raised seat does.
        """
        self.drive_request = request
        self.requested_profile = profile
        if self.follow_profile:
            self.profile = profile
        if request == spec.DRIVE_REQUEST_ENABLE and self.refuse_drive:
            return
        if request == spec.DRIVE_REQUEST_DISABLE and self.refuse_stop:
            return
        # ERROR and INITIALIZING are not ours to leave: a drive request neither clears a
        # fault nor finishes booting.
        if self.system_state in (spec.MIB_SYSTEM_STATE_IDLE, spec.MIB_SYSTEM_STATE_ENABLED):
            self.system_state = (spec.MIB_SYSTEM_STATE_ENABLED
                                 if request == spec.DRIVE_REQUEST_ENABLE
                                 else spec.MIB_SYSTEM_STATE_IDLE)

    def apply_seat_command(self, axis_id: int, target: float) -> int:
        """Judge one seat request from the HMI, return the SEAT_RESULT_* verdict.

        Targets are absolute, which is what makes a preset button and a step
        button the same message. Clamping lives here rather than on the HMI for
        the same reason it lives on a real MCB: one board owns the position, so
        there is only ever one opinion about where the seat is.
        """
        self.seat_last_axis = axis_id
        self.seat_dirty = True
        if not 0 <= axis_id < len(spec.SEAT_AXES):
            self.seat_result = 4
            return self.seat_result

        axis = spec.SEAT_AXES[axis_id]
        override = self.seat_reject[axis_id]
        if override is not None:
            self.seat_result = override
            return self.seat_result

        # The wire carries whole units; the model keeps the table's raw integers.
        target = round(target * 10 ** axis.decimals)
        current = self.seat_values[axis_id]
        clamped = max(axis.min_value, min(axis.max_value, target))
        self.seat_values[axis_id] = clamped
        if clamped > target or (clamped == current == axis.min_value):
            # Asked for less than the axis can do, or asked again while already
            # at the stop: either way the answer is "that is as low as it goes".
            self.seat_result = 1
        elif clamped < target or (clamped == current == axis.max_value):
            self.seat_result = 2
        else:
            self.seat_result = SEAT_RESULT_OK
        return self.seat_result

    def handle_user_packet(self, packet: bytes, sender_ip: str, sender_port: int) -> None:
        """Capture joystick samples, seat requests and drive requests."""
        for guid_prefix, writer_id, payload, reader_id in rtps_host.parse_rtps_data_messages(
            packet
        ):
            topic = self.topic_for_sample(guid_prefix, writer_id, reader_id)
            if topic == spec.TOPIC_JOYSTICK_XY_TWIST:
                sample = spec.unpack_xy_twist(payload)
                if sample is not None:
                    self.joystick = sample
                    self.adc_rx_count += 1
            elif topic == spec.TOPIC_HMI_COUNTER:
                self._answer_selftest_ping(payload)
            elif topic == spec.TOPIC_SELFTEST_REPORT:
                result = spec.unpack_selftest_report(payload)
                if result is not None:
                    self._note_selftest_result(result)
            elif topic == spec.TOPIC_JOYSTICK_SEAT_COMMAND:
                command = spec.unpack_seat_command(payload)
                if command is None:
                    continue
                axis_id, target = command
                self.last_seat_command = (axis_id, target)
                result = self.apply_seat_command(axis_id, target)
                known = 0 <= axis_id < len(spec.SEAT_AXES)
                name = spec.SEAT_AXES[axis_id].short if known else f"#{axis_id}"
                shown = f"{target:g} {spec.SEAT_AXES[axis_id].unit}" if known else str(target)
                rtps_host.log(
                    f"[seat] {name} -> {shown} : {SEAT_RESULTS.get(result, '?')}"
                )
                # Answer immediately. The periodic republish below is the
                # convergence path, not the reply path.
                self.publish_now()
            elif topic == spec.TOPIC_JOYSTICK_DRIVE_COMMAND:
                command = spec.unpack_drive_command(payload)
                if command is None:
                    continue
                request, profile = command
                self.apply_drive_command(request, profile)
                rtps_host.log(
                    f"[drive] {spec.DRIVE_REQUEST_NAMES.get(request, '?')} "
                    f"profile={spec.DRIVE_PROFILE_NAMES.get(profile, '?')} -> "
                    f"{spec.MIB_SYSTEM_STATE_NAMES.get(self.system_state, '?')}"
                    + (" (refusing)" if request == spec.DRIVE_REQUEST_ENABLE
                       and self.refuse_drive else "")
                )
                # The HMI is waiting on this to open its drive screen.
                self.publish_now()

    def step_speed(self, _dt: float = 0.0) -> None:
        """Advance the simulated chair and take its speed.

        The dt is ignored: CarModel.update() reads the clock itself, so the
        publisher ticking it twice a second and the drive window ticking it
        thirty times a second produce the same trajectory rather than
        double-integrating each other's steps.
        """
        if self.joystick is None:
            return
        self.car.update(self.joystick, self.profile)
        self.speed_tenths = self.car.speed_tenths

    def describe(self) -> str:
        return (
            f"state={spec.MIB_SYSTEM_STATE_NAMES.get(self.system_state, '?')} "
            f"profile={spec.DRIVE_PROFILE_NAMES.get(self.profile, '?')}"
            + (f" status_text='{self.status_text}'" if self.status_text else "")
            + f" speed={self.speed_tenths / 10:.1f}"
            + (" [PAUSED]" if self.paused else "")
        )

    def seat_units(self) -> tuple:
        """The seat model in whole units, in MIB::seatState field order.

        The table's rows are that struct's fields in that order, so zip is the whole
        mapping - one row added to the table is one field more here.
        """
        return tuple(value / 10 ** axis.decimals
                     for value, axis in zip(self.seat_values, spec.SEAT_AXES))

    def publish_diagnostics(self) -> None:
        """Fake readings for every diagnostics item: slow sine waves, offset
        per item so the rows do not move in step."""
        if not self.diagnostics_enabled:
            return
        t = time.monotonic()
        values = []
        for item in spec.DIAGNOSTICS:
            readings = (30.0 + 5.0 * math.sin(t / 7.0 + item.id),       # temperature
                        1.5 + math.sin(t / 2.0 + item.id),               # current
                        45.0 * math.sin(t / 5.0 + 2.0 * item.id))        # position
            values.append([round(v * 10 ** places)
                           for v, places in zip(readings, item.decimals)])
        self.diagnostics_values = values
        self._send_on(self._diagnostics_writer,
                      spec.pack_diagnostics(values, self.diagnostics_seq))
        self.diagnostics_seq = (self.diagnostics_seq + 1) & 0xFF

    def publish_now(self) -> None:
        """Called by the harness run loop every --period seconds."""
        if self.paused:
            return
        self.publish_diagnostics()
        # Advance the emulated speed on real elapsed time. Clamped so a long
        # gap (a pause, a breakpoint) cannot lurch the number across its range
        # in a single step.
        now = time.monotonic()
        self.step_speed(min(now - self._last_speed_step, 0.5))
        self._last_speed_step = now
        writer = self.local_writers[0]
        payload = self.build_data_message(
            writer,
            spec.pack_mib_status(self.system_state, self.profile, self.seat_units(),
                                 self.seq, self.speed_tenths, self.status_text,
                                 self.error_message, self.error_footer,
                                 clock=datetime.datetime.now()),  # sets the HMI's clock
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
        subscribe_topic=[spec.TOPIC_JOYSTICK_XY_TWIST],
        subscribe_type_name=spec.TYPE_XY_TWIST,
        publish_topic=spec.TOPIC_MIB_STATUS,
        publish_value=0,  # unused: publish_now() is overridden
        publish_interval=cli.period,
        echo_received=False,
        reliable=False,
        type_name=spec.TYPE_MIB_STATUS,
        announce_period=1.0,
        duration=0.0,
        trace_packets=cli.trace_packets,
        peer=cli.peer,
        peer_participant_ids=cli.peer_participant_ids,
    )


HELP_TEXT = """commands:
  a / active      state -> ENABLED (the next DriveCommand overrides it)
  i / inactive    state -> IDLE
  z               state -> INITIALIZING
  x               toggle refusing DriveCommand ENABLE (HMI should time out)
  s               toggle refusing DriveCommand DISABLE (HMI cannot leave driving)
  ok              state -> IDLE
  e / err         state -> ERROR
  et <text>       error banner body ('et' alone clears it)
  ef <text>       error banner footer ('ef' alone clears it)
  st <text>       override the STATE label text ('st' alone clears it)
  p               pause publishing (HMI should go stale after
                  MIB_STATUS_TIMEOUT_MS: blinking orange RTPS, '---')
  r               resume publishing (HMI should go straight back to green)
  <enter>         show what is being published
  q               quit"""


def run_interactive(harness: SystemStatePublisher) -> None:
    print(HELP_TEXT)
    while True:
        try:
            command = input("mcb> ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            return
        if command in ("q", "quit", "exit"):
            return
        if command in ("a", "active"):
            harness.system_state = spec.MIB_SYSTEM_STATE_ENABLED
        elif command in ("i", "inactive"):
            harness.system_state = spec.MIB_SYSTEM_STATE_IDLE
        elif command == "z":
            harness.system_state = spec.MIB_SYSTEM_STATE_INITIALIZING
        elif command == "x":
            harness.refuse_drive = not harness.refuse_drive
            print(f"  refusing drive requests: {harness.refuse_drive}")
            continue
        elif command == "s":
            harness.refuse_stop = not harness.refuse_stop
            print(f"  refusing stop requests: {harness.refuse_stop}")
            continue
        elif command == "ok":
            harness.system_state = spec.MIB_SYSTEM_STATE_IDLE
        elif command in ("e", "err", "error"):
            harness.system_state = spec.MIB_SYSTEM_STATE_ERROR
        elif command == "et" or command.startswith("et "):
            harness.error_message = command[3:].strip()
        elif command == "ef" or command.startswith("ef "):
            harness.error_footer = command[3:].strip()
        elif command == "st" or command.startswith("st "):
            harness.status_text = command[3:].strip()
        elif command in ("p", "pause"):
            harness.paused = True
            print(f"  {harness.describe()}")
            continue
        elif command in ("r", "resume"):
            harness.paused = False
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


def run_cycle(harness: SystemStatePublisher, dwell: float) -> None:
    combinations = [
        spec.MIB_SYSTEM_STATE_INITIALIZING,
        spec.MIB_SYSTEM_STATE_IDLE,
        spec.MIB_SYSTEM_STATE_ENABLED,
        spec.MIB_SYSTEM_STATE_ERROR,
    ]
    stale_gap = max(dwell, spec.MIB_STATUS_TIMEOUT_MS / 1000.0 + 1.0)
    print(f"Cycling every {dwell:.1f}s; Ctrl-C to stop.")
    try:
        while True:
            for state in combinations:
                harness.system_state = state
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
        "--period", type=float, default=spec.MIB_STATUS_PERIOD_MS / 1000.0,
        help="Seconds between republishes of the current status (default from the spec header's "
             f"MIB_STATUS_PERIOD_MS = {spec.MIB_STATUS_PERIOD_MS} ms). Anything longer than "
             f"MIB_STATUS_TIMEOUT_MS ({spec.MIB_STATUS_TIMEOUT_MS} ms) makes the HMI declare "
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
    harness = SystemStatePublisher(args)
    network = threading.Thread(target=harness.run, daemon=True)
    network.start()
    rtps_net.save_config({"peer": peer, "advertised_address": args.advertised_address,
                          "period": cli.period})

    print(f"Publishing '{spec.TOPIC_MIB_STATUS}' [{spec.TYPE_MIB_STATUS}] "
          f"every {cli.period:.2f}s\n")
    if cli.cycle:
        run_cycle(harness, cli.dwell)
    else:
        run_interactive(harness)
    return 0


if __name__ == "__main__":
    sys.exit(main())
