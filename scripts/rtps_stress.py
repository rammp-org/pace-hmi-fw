#!/usr/bin/env python3
"""Stress test the HMI's RTPS against a RAMMP-sized network of stand-in participants.

The network is stress/network.toml: the MIB stand-in (rtps_mcb_sim's
SystemStatePublisher) plus PACE RACER and ROS 2 stand-ins, each one a real RTPS
participant with that role's discovery footprint. Needs the HMI built with
stress/sdkconfig.stress (CONFIG_HMI_RTPS_STRESS): it counts what each stand-in
delivers on rt/rammp/stress/to_hmi and prints a STRESS line a second on USB serial,
which this script reads.

  # the sweep: participant counts x stress rate, a table at the end
  python rtps_stress.py sweep --peer 192.168.137.21 --advertised-address 192.168.137.2 \\
      --serial COM9 --counts 0,2,4,6,8,12,16,24 --rates 1,5,10,20,50,100

  # just hold a network up (poke at the HMI by hand, watch the serial yourself)
  python rtps_stress.py hold --peer 192.168.137.21 --advertised-address 192.168.137.2 \\
      --count 12 --rate 10

A step passes when every stand-in the HMI should hear is heard (>= 99% of its stress
samples, by sequence gaps), every joystick subscriber gets the stick (>= 95% of what
the MIB stand-in gets), MibStatus loss <= 5% (it is 2 Hz) and the HMI did not reset. Per count the
rate goes up until a step fails; the last pass is that count's maximum bandwidth.
"""

from __future__ import annotations

import argparse
import csv
import datetime
import os
import re
import select
import struct
import sys
import threading
import time
import tomllib
from dataclasses import dataclass, field

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import rammp_rtps as spec  # noqa: E402  (path setup must run first)
import rtps_host  # noqa: E402
import rtps_mcb_sim  # noqa: E402
import rtps_selftest  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_NETWORK = os.path.join(HERE, "..", "stress", "network.toml")

STRESS_TOPIC = "rt/rammp/stress/to_hmi"
STRESS_TYPE = "std_msgs::msg::dds_::UInt8MultiArray_"
FIRST_STANDIN_ID = 20  # participant ids: HMI 0-3, the other bench scripts 10-15
MAX_STANDIN_ID = 119   # RTPS unicast ports run out past this on domain 0

# Pass limits (see the module docstring).
STRESS_DELIVERY_MIN = 0.99
JOY_DELIVERY_MIN = 0.95
MIB_LOSS_MAX = 0.05  # MibStatus is 2 Hz: one lost in a 30 s step is already 1.7%
RTT_P99_MAX_US = 100_000

# The parameter services every rclpy node announces (Jazzy), as (service, srv type).
ROS_PARAM_SERVICES = [
    ("describe_parameters", "DescribeParameters"),
    ("get_parameter_types", "GetParameterTypes"),
    ("get_parameters", "GetParameters"),
    ("list_parameters", "ListParameters"),
    ("set_parameters", "SetParameters"),
    ("set_parameters_atomically", "SetParametersAtomically"),
]


def quiet_harness_log(verbose: bool) -> None:
    if not verbose:
        rtps_host.log = lambda message: (
            print(message, flush=True)
            if "fail" in message.lower() and not message.startswith("[publish]") else None)


def stress_payload(sender: int, seq: int, size: int) -> bytes:
    """std_msgs/UInt8MultiArray, XCDR1 LE: no dims, data_offset 0, `size` bytes of data."""
    data = struct.pack("<II", sender, seq).ljust(max(size, 8), b"\0")
    return b"\x00\x01\x00\x00" + struct.pack("<III", 0, 0, len(data)) + data


###############################################################################
# Stand-ins


@dataclass
class Role:
    name: str
    role: str
    joystick: bool
    namespace: str = ""
    axis: str = ""


def load_network(path: str) -> tuple[dict, list[Role], dict]:
    with open(path, "rb") as handle:
        net = tomllib.load(handle)
    defaults = net.get("defaults", {})
    roles = [Role(name=p["name"], role=p["role"],
                  joystick=p.get("joystick", defaults.get("joystick", True)),
                  namespace=p.get("namespace", ""), axis=p.get("axis", ""))
             for p in net.get("participant", [])]
    return defaults, roles, net.get("filler", {})


def role_for(index: int, roles: list[Role], filler: dict, defaults: dict) -> Role:
    if index < len(roles):
        return roles[index]
    i = index - len(roles)
    return Role(name=filler.get("name", "filler_{i}").format(i=i), role=filler.get("role", "ros_node"),
                joystick=filler.get("joystick", defaults.get("joystick", True)),
                namespace=filler.get("namespace", ""))


def harness_args(cli: argparse.Namespace, name: str, participant_id: int) -> argparse.Namespace:
    return argparse.Namespace(
        node_name=name, domain_id=cli.domain_id, participant_id=participant_id,
        bind_address=cli.bind_address or cli.advertised_address,
        advertised_address=cli.advertised_address, multicast_interface=None,
        multicast_group="239.255.0.1", enclave="/", subscribe_topic=[],
        subscribe_type_name=None, publish_topic=None, publish_value=0, publish_interval=0.0,
        echo_received=False, reliable=False, type_name="", announce_period=cli.announce_period,
        duration=0.0, trace_packets=False, peer=[cli.peer], peer_participant_ids=[0, 1, 2, 3],
        quiet=True)


class StandIn(rtps_host.RtpsHostHarness):
    """One stand-in participant: its role's endpoints, the stress stream, the stick."""

    def __init__(self, cli: argparse.Namespace, role: Role, participant_id: int,
                 stress_size: int) -> None:
        super().__init__(harness_args(cli, role.name, participant_id))
        self.role = role
        self.participant_id = participant_id
        self.stress_size = stress_size
        self.rate_hz = 0.0  # set from the sweep thread
        self.stress_sent = 0
        self.joy_rx = 0
        writers: list[tuple[str, str]] = [(STRESS_TOPIC, STRESS_TYPE)]
        readers: list[tuple[str, str]] = []
        if role.joystick:
            readers.append((spec.ROS_TOPIC_JOYSTICK_XY_TWIST, spec.ROS_TYPE_XY_TWIST))
        if role.role == "racer":
            writers.append((f"rammp/{role.axis}/motor_state", "rammp/msg/MotorState"))
            readers += [(f"rammp/mib/motor_command/{role.axis}", "rammp/msg/MotorCommand"),
                        (f"rammp/mib/motor_tuning/{role.axis}", "rammp/msg/MotorTuning")]
        elif role.role == "ros_node":
            node = f"{role.namespace.rstrip('/')}/{role.name}".lstrip("/")
            writers += [("rt/rosout", "rcl_interfaces::msg::dds_::Log_"),
                        ("rt/parameter_events", "rcl_interfaces::msg::dds_::ParameterEvent_")]
            for service, srv in ROS_PARAM_SERVICES:
                writers.append((f"rr/{node}/{service}Reply",
                                f"rcl_interfaces::srv::dds_::{srv}_Response_"))
                readers.append((f"rq/{node}/{service}Request",
                                f"rcl_interfaces::srv::dds_::{srv}_Request_"))
            readers.append(("rt/parameter_events", "rcl_interfaces::msg::dds_::ParameterEvent_"))
        else:
            raise ValueError(f"{role.name}: unknown role '{role.role}'")
        self.local_writers = [rtps_host.WriterConfig(t, ty, False, i)
                              for i, (t, ty) in enumerate(writers)]
        self.local_readers = [rtps_host.ReaderConfig(t, ty, False, i)
                              for i, (t, ty) in enumerate(readers)]
        self.stress_writer = self.local_writers[0]

    def matched_targets(self, topic: str) -> list[tuple[str, int]]:
        """Unicast locators of the readers on `topic`, and only those: the harness's own
        fallback sprays every discovered participant, which would load the HMI with data
        no reader of it asked for."""
        targets = []
        for reader in list(self.discovered_readers.values()):
            if reader.topic_name == topic and reader.unicast_port > 0 and reader.unicast_address:
                target = (reader.unicast_address, reader.unicast_port)
                if target not in targets:
                    targets.append(target)
        return targets

    def send_stress(self) -> None:
        self.stress_sent += 1
        payload = self.build_data_message(
            self.stress_writer, stress_payload(self.participant_id, self.stress_sent, self.stress_size))
        for target in self.matched_targets(STRESS_TOPIC):
            self.send_user_datagram(payload, target)

    def handle_user_packet(self, packet: bytes, sender_ip: str, sender_port: int) -> None:
        for guid_prefix, writer_id, _payload, reader_id in rtps_host.parse_rtps_data_messages(packet):
            if self.topic_for_sample(guid_prefix, writer_id, reader_id) == spec.ROS_TOPIC_JOYSTICK_XY_TWIST:
                self.joy_rx += 1

    def run(self) -> None:
        sockets = [self.metatraffic_multicast_sock, self.metatraffic_unicast_sock,
                   self.user_unicast_sock, self.user_multicast_sock]
        next_discovery = next_stress = time.monotonic()
        try:
            while not self._stop_event.is_set():
                now = time.monotonic()
                if now >= next_discovery:
                    self.send_discovery_now()
                    next_discovery = now + self.args.announce_period
                rate = self.rate_hz
                if rate > 0 and now >= next_stress:
                    self.send_stress()
                    # catch up after a stall, but never burst more than half a second's worth
                    next_stress = max(next_stress + 1.0 / rate, now - 0.5)
                elif rate <= 0:
                    next_stress = now
                wake = min(next_discovery, next_stress if rate > 0 else next_discovery)
                timeout = min(max(wake - time.monotonic(), 0.0), 0.05)
                readable, _, _ = select.select(sockets, [], [], timeout)
                for sock in readable:
                    try:
                        packet, sender = sock.recvfrom(4096)
                    except (ConnectionResetError, OSError):
                        continue
                    if sock is self.user_unicast_sock or sock is self.user_multicast_sock:
                        self.handle_user_packet(packet, sender[0], sender[1])
                    else:
                        self.handle_metatraffic_packet(packet, sender[0])
        finally:
            self.close()


###############################################################################
# The HMI's console


@dataclass
class HmiSnapshot:
    stats: dict = field(default_factory=dict)            # the latest STRESS line
    senders: dict = field(default_factory=dict)          # id -> (rx, lost), latest STRESS_SENDERS
    senders_seq: int = 0                                 # STRESS_SENDERS lines seen
    resets: int = 0


class HmiConsole:
    """Reads the HMI's USB serial: STRESS lines and resets."""

    STRESS_RE = re.compile(r"STRESS t=(\d+)((?: \w+=-?\d+)*)")
    SENDERS_RE = re.compile(r"STRESS_SENDERS((?: \d+:\d+/\d+)*)")
    RESET_RE = re.compile(r"ESP-ROM:|rst:0x|Guru Meditation|abort\(\)|Backtrace:")

    def __init__(self, port: str, log_path: str | None) -> None:
        import serial  # pyserial
        self.serial = serial.Serial()
        self.serial.port, self.serial.baudrate, self.serial.timeout = port, 115200, 0.2
        self.serial.dtr = False  # opening must not reset the board
        self.serial.rts = False
        self.serial.open()
        self.log = open(log_path, "a", encoding="utf-8", errors="replace") if log_path else None
        self.lock = threading.Lock()
        self.snap = HmiSnapshot()
        self._stop = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _run(self) -> None:
        buffer = b""
        while not self._stop.is_set():
            buffer += self.serial.read(4096)
            *lines, buffer = buffer.split(b"\n")
            for raw in lines:
                line = raw.decode("utf-8", errors="replace").rstrip()
                if self.log:
                    self.log.write(line + "\n")
                self._parse(line)

    def _parse(self, line: str) -> None:
        if self.RESET_RE.search(line):
            with self.lock:
                self.snap.resets += 1
            print(f"!! HMI console: {line}", flush=True)
            return
        match = self.SENDERS_RE.search(line)
        if match:
            senders = {}
            for item in match.group(1).split():
                sid, counts = item.split(":")
                rx, lost = counts.split("/")
                senders[int(sid)] = (int(rx), int(lost))
            with self.lock:
                self.snap.senders = senders
                self.snap.senders_seq += 1
            return
        match = self.STRESS_RE.search(line)
        if match:
            stats = {"t": int(match.group(1))}
            for item in match.group(2).split():
                key, value = item.split("=")
                stats[key] = int(value)
            with self.lock:
                self.snap.stats = stats

    def snapshot(self) -> HmiSnapshot:
        with self.lock:
            return HmiSnapshot(dict(self.snap.stats), dict(self.snap.senders),
                               self.snap.senders_seq, self.snap.resets)

    def wait_senders_line(self, timeout: float = 12.0) -> HmiSnapshot:
        """The next STRESS_SENDERS line (one every 5 s), so a window starts on fresh counts."""
        seen = self.snapshot().senders_seq
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            snap = self.snapshot()
            if snap.senders_seq != seen:
                return snap
            time.sleep(0.05)
        return self.snapshot()

    def close(self) -> None:
        self._stop.set()
        self.thread.join(timeout=1.0)
        self.serial.close()
        if self.log:
            self.log.close()


###############################################################################
# The network


class Network:
    def __init__(self, cli: argparse.Namespace) -> None:
        self.cli = cli
        self.defaults, self.roles, self.filler = load_network(cli.network)
        self.stress_size = cli.size or int(self.defaults.get("stress_size", 256))
        cli.announce_period = float(self.defaults.get("announce_period", 1.0))
        self.standins: list[StandIn] = []
        self.threads: list[threading.Thread] = []
        mib_cli = argparse.Namespace(
            node_name="mib_standin", domain_id=cli.domain_id, participant_id=15,
            bind_address=cli.bind_address, advertised_address=cli.advertised_address,
            multicast_interface=None, multicast_group="239.255.0.1",
            period=spec.MIB_STATUS_PERIOD_MS / 1000.0, trace_packets=False,  # noqa: F821
            peer=[cli.peer], peer_participant_ids=[0, 1, 2, 3])
        self.mib = rtps_mcb_sim.SystemStatePublisher(rtps_mcb_sim.build_harness_args(mib_cli))
        self.mib_thread = threading.Thread(target=self.mib.run, daemon=True)
        self.mib_thread.start()

    def resize(self, count: int) -> None:
        while len(self.standins) < count:
            index = len(self.standins)
            pid = FIRST_STANDIN_ID + index
            if pid > MAX_STANDIN_ID:
                raise SystemExit(f"at most {MAX_STANDIN_ID - FIRST_STANDIN_ID + 1} stand-ins")
            standin = StandIn(self.cli, role_for(index, self.roles, self.filler, self.defaults),
                              pid, self.stress_size)
            thread = threading.Thread(target=standin.run, daemon=True, name=standin.role.name)
            thread.start()
            self.standins.append(standin)
            self.threads.append(thread)
        while len(self.standins) > count:
            standin, thread = self.standins.pop(), self.threads.pop()
            standin.stop()
            thread.join(timeout=2.0)

    def set_rate(self, rate_hz: float) -> None:
        for standin in self.standins:
            standin.rate_hz = rate_hz

    def stop(self) -> None:
        self.resize(0)
        self.mib.stop()
        self.mib_thread.join(timeout=2.0)


###############################################################################
# The sweep


@dataclass
class Step:
    count: int
    rate_hz: float
    kbps: float          # stress data per stand-in, kB/s on the wire (RTPS DATA bytes)
    verdict: str         # PASS, PARTIAL (some peers never matched; the rest fine), FAIL (load)
    why: str             # the load failure
    unmatched: str       # the discovery failure: stand-ins the HMI never exchanged data with
    delivering: int      # stand-ins whose stress stream reaches the HMI
    joy_delivering: int  # joystick subscribers (the MIB included) the stick reaches
    joy_expected: int
    m_stress: int        # the HMI's matched_writers(stress topic)
    m_joy: int           # the HMI's matched_readers(joystick)
    heap_min: int
    mib_loss_pct: float
    rtt_p50_us: int | None = None
    rtt_p99_us: int | None = None


def wire_bytes(size: int) -> int:
    # RTPS header 20 + DATA submessage header 4 + 20 + CDR 16 + data
    return 20 + 4 + 20 + 16 + max(size, 8)


def names(items: list[str], limit: int = 3) -> str:
    return ", ".join(items[:limit]) + ("..." if len(items) > limit else "")


def measure(net: Network, hmi: HmiConsole, count: int, rate: float, soak: float,
            never: dict[str, set[str]]) -> Step:
    """One step. `never` holds, per stream kind, the stand-ins that delivered nothing in this
    count's first step: those are a discovery failure, reported but not held against the
    load. A stand-in that delivered before and stops now is a load failure."""
    standins = net.standins
    start = hmi.wait_senders_line()
    start_joy = {s.participant_id: s.joy_rx for s in standins}
    start_mib_joy = net.mib.adc_rx_count
    time.sleep(max(soak - 5.0, 0.0))
    end = hmi.wait_senders_line()
    end_joy = {s.participant_id: s.joy_rx for s in standins}
    mib_joy = net.mib.adc_rx_count - start_mib_joy
    stats = end.stats
    first = not never
    load: list[str] = []
    if end.resets != start.resets:
        load.append("HMI reset")

    silent, short, delivering = [], [], 0
    if rate > 0:
        for s in standins:
            rx0, lost0 = start.senders.get(s.participant_id, (0, 0))
            rx1, lost1 = end.senders.get(s.participant_id, (0, 0))
            rx, lost = rx1 - rx0, lost1 - lost0
            if rx == 0:
                silent.append(s.role.name)
                continue
            delivering += 1
            if rx / (rx + lost) < STRESS_DELIVERY_MIN:
                short.append(f"{s.role.name} {100 * rx / (rx + lost):.0f}%")
    joy_subs = [s for s in standins if s.role.joystick]
    joy_silent = [s.role.name for s in joy_subs
                  if end_joy[s.participant_id] == start_joy[s.participant_id]]
    joy_short = [s.role.name for s in joy_subs if mib_joy > 0 and s.role.name not in joy_silent
                 and end_joy[s.participant_id] - start_joy[s.participant_id] < JOY_DELIVERY_MIN * mib_joy]
    if first:
        never["stress"] = set(silent)
        never["joy"] = set(joy_silent)
        never["set"] = {"yes"}
    newly_silent = [n for n in silent if n not in never.get("stress", set())]
    newly_joy_silent = [n for n in joy_silent if n not in never.get("joy", set())]
    if newly_silent:
        load.append(f"stress stopped from {len(newly_silent)} ({names(newly_silent)})")
    if short:
        load.append(f"stress loss: {names(short)}")
    if mib_joy == 0:
        load.append("MIB stand-in got no joystick")
    if newly_joy_silent:
        load.append(f"joystick stopped at {len(newly_joy_silent)} ({names(newly_joy_silent)})")
    if joy_short:
        load.append(f"joystick short at {len(joy_short)} ({names(joy_short)})")
    mib_rx = stats.get("mib_rx", 0) - start.stats.get("mib_rx", 0)
    mib_lost = stats.get("mib_lost", 0) - start.stats.get("mib_lost", 0)
    mib_loss = 100.0 * mib_lost / (mib_rx + mib_lost) if mib_rx + mib_lost else 100.0
    if mib_rx + mib_lost == 0:
        load.append("no MibStatus at the HMI")
    elif mib_loss > 100 * MIB_LOSS_MAX:
        load.append(f"MibStatus loss {mib_loss:.1f}%")

    unmatched = []
    if never.get("stress"):
        unmatched.append(f"no stress from {len(never['stress'])}/{count} "
                         f"({names(sorted(never['stress']))})")
    if never.get("joy"):
        unmatched.append(f"no joystick at {len(never['joy'])}/{len(joy_subs)} subscribers "
                         f"({names(sorted(never['joy']))})")
    verdict = "FAIL" if load else ("PARTIAL" if unmatched else "PASS")
    return Step(count=count, rate_hz=rate, kbps=rate * wire_bytes(net.stress_size) / 1000.0,
                verdict=verdict, why="; ".join(load), unmatched="; ".join(unmatched),
                delivering=delivering,
                joy_delivering=len(joy_subs) - len(joy_silent) + (1 if mib_joy > 0 else 0),
                joy_expected=len(joy_subs) + 1, m_stress=stats.get("m_stress", -1),
                m_joy=stats.get("m_joy", -1), heap_min=stats.get("heap_min", -1),
                mib_loss_pct=round(mib_loss, 1))


def rtt_probe(net: Network, timeout: float = 90.0) -> tuple[int | None, int | None]:
    """The HMI's self test, for its ping round trip. It loads the HMI on purpose (redraws)."""
    run_id = net.mib.request_selftest()
    got = rtps_selftest.collect(net.mib, run_id, timeout, resend=True)
    values = {r.name: r.value for (kind, _i), r in got["run"].items()
              if kind == spec.SELFTEST_KIND_RESULT}
    return values.get("rtps.rtt_p50"), values.get("rtps.rtt_p99")


def print_step(step: Step) -> None:
    detail = "; ".join(x for x in (step.why, step.unmatched) if x)
    print(f"  N={step.count:<3} {step.rate_hz:>6g} Hz {step.kbps:>7.1f} kB/s  {step.verdict:<7} "
          f"stress {step.delivering}/{step.count} (matched {step.m_stress})  joystick "
          f"{step.joy_delivering}/{step.joy_expected} (matched {step.m_joy})  heap_min "
          f"{step.heap_min}  MibStatus loss {step.mib_loss_pct}%"
          f"{'  -- ' + detail if detail else ''}", flush=True)


def summary_rows(steps: list[Step], counts: list[int]) -> list[dict]:
    rows = []
    for count in counts:
        mine = [s for s in steps if s.count == count]
        if not mine:
            continue
        passes = [s for s in mine if s.verdict != "FAIL" and s.rate_hz > 0]
        fails = [s for s in mine if s.verdict == "FAIL"]
        best = max(passes, key=lambda s: s.rate_hz) if passes else None
        probe = next((s for s in mine if s.rtt_p99_us is not None), None)
        ref = best or mine[0]
        if count == 0:
            limit = "n/a (MIB only)"
        elif best:
            limit = f"{best.kbps:.1f} ({best.rate_hz:g} Hz)"
        else:
            limit = "below the lowest rate"
        rows.append({
            "participants": count,
            "max_kBps": limit,
            "total_kBps": f"{best.kbps * best.delivering:.0f}" if best else "-",
            "delivering": f"{ref.delivering}/{count}",
            "joystick": f"{ref.joy_delivering}/{ref.joy_expected}",
            "first_failure": fails[0].why if fails else
            ("none up to the top rate" if count else "-"),
            "unmatched": mine[0].unmatched or "-",
            "heap_min": min(s.heap_min for s in mine),
            "rtt_p99_ms": f"{probe.rtt_p99_us / 1000:.0f}" if probe and probe.rtt_p99_us else "-",
        })
    return rows


def write_reports(out_dir: str, steps: list[Step], counts: list[int], header: str) -> str:
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "steps.csv"), "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(Step.__dataclass_fields__))
        writer.writeheader()
        for step in steps:
            writer.writerow(step.__dict__)
    lines = [header, "",
             "| Participants | Max kB/s per participant | Total kB/s | Stress delivered | "
             "Joystick delivered | First load failure | Never matched | Lowest free heap | "
             "RTT p99 ms |",
             "|---|---|---|---|---|---|---|---|---|"]
    for r in summary_rows(steps, counts):
        lines.append(f"| {r['participants']} | {r['max_kBps']} | {r['total_kBps']} | "
                     f"{r['delivering']} | {r['joystick']} | {r['first_failure']} | "
                     f"{r['unmatched']} | {r['heap_min']} | {r['rtt_p99_ms']} |")
    text = "\n".join(lines) + "\n"
    with open(os.path.join(out_dir, "summary.md"), "w", encoding="utf-8") as handle:
        handle.write(text)
    return text


def sweep(cli: argparse.Namespace) -> int:
    counts = [int(c) for c in cli.counts.split(",")]
    rates = [float(r) for r in cli.rates.split(",")]
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = cli.out or os.path.join(HERE, "..", "stress", "results", f"{stamp}-{cli.label}")
    os.makedirs(out_dir, exist_ok=True)
    hmi = HmiConsole(cli.serial, os.path.join(out_dir, "console.log"))
    net = Network(cli)
    steps: list[Step] = []
    try:
        print(f"waiting for the HMI's STRESS lines on {cli.serial}...", flush=True)
        hmi.wait_senders_line(timeout=15.0)
        if not hmi.snapshot().stats:
            print("no STRESS lines: is the HMI running a stress build (stress/sdkconfig.stress)?")
            return 2
        for count in counts:
            net.set_rate(0)
            net.resize(count)
            print(f"N={count}: {net.standins[-1].role.name if net.standins else 'MIB only'} "
                  f"last in; {cli.settle:g} s for discovery", flush=True)
            time.sleep(cli.settle)
            last_pass: Step | None = None
            never: dict[str, set[str]] = {}
            for rate in ([0.0] if count == 0 else rates):
                net.set_rate(rate)
                step = measure(net, hmi, count, rate, cli.soak, never)
                steps.append(step)
                print_step(step)
                if step.verdict == "FAIL":
                    break
                last_pass = step
            if cli.rtt and last_pass is not None:
                net.set_rate(last_pass.rate_hz)
                last_pass.rtt_p50_us, last_pass.rtt_p99_us = rtt_probe(net)
                print(f"  RTT at {last_pass.rate_hz:g} Hz: p50 {last_pass.rtt_p50_us} us, "
                      f"p99 {last_pass.rtt_p99_us} us", flush=True)
            write_reports(out_dir, steps, counts[:counts.index(count) + 1], "(partial)")
    finally:
        net.stop()
        hmi.close()
    header = (f"RTPS stress {stamp} ({cli.label}): {net.stress_size} B samples "
              f"({wire_bytes(net.stress_size)} B on the wire), soak {cli.soak:g} s, network "
              f"{os.path.relpath(cli.network)}")
    print("\n" + write_reports(out_dir, steps, counts, header))
    print(f"results in {os.path.abspath(out_dir)}")
    return 0


def hold(cli: argparse.Namespace) -> int:
    net = Network(cli)
    net.resize(cli.count)
    net.set_rate(cli.rate)
    print(f"holding {cli.count} stand-ins + the MIB at {cli.rate:g} Hz; Ctrl-C to stop", flush=True)
    try:
        while True:
            time.sleep(5)
            joy = " ".join(f"{s.role.name}:{s.joy_rx}" for s in net.standins if s.role.joystick)
            print(f"MIB joystick {net.mib.adc_rx_count}; stand-ins {joy}", flush=True)
    except KeyboardInterrupt:
        return 0
    finally:
        net.stop()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("sweep", "hold"):
        p = sub.add_parser(name)
        p.add_argument("--peer", required=True, help="The HMI's IP")
        p.add_argument("--advertised-address", required=True,
                       help="This PC's address on the HMI's network")
        p.add_argument("--bind-address", default=None, help="Defaults to --advertised-address")
        p.add_argument("--domain-id", type=int, default=0)
        p.add_argument("--network", default=DEFAULT_NETWORK, help="The network definition (TOML)")
        p.add_argument("--size", type=int, default=0, help="Stress sample data bytes (default: TOML)")
        p.add_argument("--verbose", action="store_true", help="The RTPS harness log too")
    s = sub.choices["sweep"]
    s.add_argument("--serial", required=True, help="The HMI's USB serial port (COM9)")
    s.add_argument("--counts", default="0,2,4,6,8,10,12,16,20,24")
    s.add_argument("--rates", default="1,5,10,20,50,100", help="Stress Hz per stand-in, ascending")
    s.add_argument("--settle", type=float, default=20.0, help="s for discovery after a count change")
    s.add_argument("--soak", type=float, default=30.0, help="s per step")
    s.add_argument("--rtt", action="store_true",
                   help="Run the HMI's self test at each count's top passing rate, for RTT")
    s.add_argument("--label", default="run", help="Names the results folder (before, tuned...)")
    s.add_argument("--out", default=None, help="Results folder")
    h = sub.choices["hold"]
    h.add_argument("--count", type=int, required=True)
    h.add_argument("--rate", type=float, default=0.0)
    cli = parser.parse_args()
    quiet_harness_log(cli.verbose)
    return sweep(cli) if cli.command == "sweep" else hold(cli)


if __name__ == "__main__":
    sys.exit(main())
