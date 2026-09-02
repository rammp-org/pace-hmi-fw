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
import socket
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import rammp_rtps as spec  # noqa: E402  (path setup must run first)
import rtps_host  # noqa: E402


class McbStatusPublisher(rtps_host.RtpsHostHarness):
    """Harness whose periodic publish sends a rammp_mcb_status_t."""

    def __init__(self, args: argparse.Namespace) -> None:
        super().__init__(args)
        self.drive_status = spec.DRIVE_STATUS_INACTIVE
        self.system_state = spec.STATE_OK
        self.flags = 0
        # label overrides; empty means "let the HMI use the enum's own name"
        self.drive_text = ""
        self.state_text = ""
        self.seq = 0
        self.announced_targets = -1
        # Stop publishing without tearing the participant down, so the HMI's
        # stale path can be exercised and then recovered from without a fresh
        # discovery round confusing the picture.
        self.paused = False

    def describe(self) -> str:
        return (
            f"drive={spec.DRIVE_STATUS_NAMES.get(self.drive_status, '?')} "
            f"state={spec.STATE_NAMES.get(self.system_state, '?')} "
            f"flags=0x{self.flags:02x}"
            + (f" drive_text='{self.drive_text}'" if self.drive_text else "")
            + (f" state_text='{self.state_text}'" if self.state_text else "")
            + (" [PAUSED]" if self.paused else "")
        )

    def publish_now(self) -> None:
        """Called by the harness run loop every --period seconds."""
        if self.paused:
            return
        writer = self.local_writers[0]
        payload = self.build_data_message(
            writer,
            spec.pack_mcb_status(self.drive_status, self.system_state, self.flags, self.seq,
                                 self.drive_text, self.state_text),
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
    """Best-effort list of this host's IPv4 addresses, stdlib only."""
    addresses = []
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            address = info[4][0]
            if address not in addresses:
                addresses.append(address)
    except socket.gaierror:
        pass
    guess = rtps_host.guess_local_ipv4()
    if guess not in addresses:
        addresses.insert(0, guess)
    return addresses


def build_harness_args(cli: argparse.Namespace) -> argparse.Namespace:
    advertised = cli.advertised_address or rtps_host.guess_local_ipv4()
    return argparse.Namespace(
        node_name=cli.node_name,
        domain_id=cli.domain_id,
        participant_id=cli.participant_id,
        bind_address=cli.bind_address or advertised,
        advertised_address=advertised,
        multicast_interface=cli.multicast_interface,
        multicast_group=cli.multicast_group,
        enclave="/",
        subscribe_topic=[],  # publish-only participant
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
        print("local IPv4 addresses (pass one as --advertised-address):")
        for address in local_ipv4_addresses():
            print(f"  {address}")
        return 0
    if cli.period <= 0:
        parser.error("--period must be > 0")

    args = build_harness_args(cli)
    print(f"spec header: {spec.HEADER_PATH}")
    print(f"advertised address: {args.advertised_address} "
          "(--list-interfaces shows the alternatives)")
    harness = McbStatusPublisher(args)
    network = threading.Thread(target=harness.run, daemon=True)
    network.start()

    print(f"Publishing '{spec.TOPIC_MCB_STATUS}' [{spec.TYPE_MCB_STATUS}] "
          f"every {cli.period:.2f}s\n")
    if cli.cycle:
        run_cycle(harness, cli.dwell)
    else:
        run_interactive(harness)
    return 0


if __name__ == "__main__":
    sys.exit(main())
