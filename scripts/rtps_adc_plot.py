#!/usr/bin/env python3
"""Live plot of the Tab5 joystick ADC stream (X/Y position + twist bar).

The firmware publishes one sample per ADC cycle (30 Hz by default) on the
joystick ADC topic: three little-endian uint32 millivolt values (x, y, twist)
behind the standard 4-byte CDR encapsulation header. The topic and type names
and the payload decoder all come from ``rammp_rtps.py``, which scrapes
``main/rammp_rtps_spec.h`` — the same wire spec the firmware builds against.

This script reuses the RTPS machinery from ``rtps_host.py`` (same directory)
for discovery and reception, and matplotlib for display:

  left  — X/Y position dot with a fading trail (both axes in mV)
  right — horizontal twist bar + numeric readouts + measured sample rate

Usage:
  python rtps_adc_plot.py                 # defaults: 30 fps, 30-sample trail
  python rtps_adc_plot.py --fps 60 --trail 90 --full-scale 3300

Requires matplotlib (``pip install matplotlib``); the RTPS side is stdlib-only.
Coexists with rtps_host.py (participant 10) and rtps_brightness.py (11) by
using participant id 12.
"""

from __future__ import annotations

import argparse
import collections
import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import rtps_host  # noqa: E402  (path setup must run first)

ADC_TOPIC = "espp/rtps_example/adc"
ADC_TYPE_NAME = "rammp/msg/AdcXYTwist"


def deserialize_adc_cdr(payload: bytes) -> tuple[int, int, int] | None:
    """Decode x/y/twist mV from a CDR-encapsulated sample.

    Layout: 4-byte encapsulation header (0x00 0x01 = CDR_LE) + 3 * uint32 LE.
    """
    if len(payload) < 16 or payload[:2] != b"\x00\x01":
        return None
    return struct.unpack_from("<III", payload, 4)


class AdcPlotHarness(rtps_host.RtpsHostHarness):
    """Harness whose user-data path decodes AdcXYTwist samples into a deque."""

    def __init__(self, args: argparse.Namespace, samples: collections.deque) -> None:
        super().__init__(args)
        self.samples = samples

    def handle_user_packet(self, packet: bytes, sender_ip: str, sender_port: int) -> None:
        # Same writer-GUID -> topic routing as the base class, but with the
        # AdcXYTwist payload decoder instead of the single-uint32 one.
        for guid_prefix, writer_id, serialized_payload, reader_id in (
            rtps_host.parse_rtps_data_messages(packet)
        ):
            if self.topic_for_sample(guid_prefix, writer_id, reader_id) != ADC_TOPIC:
                continue
            values = spec.unpack_adc_xy_twist(serialized_payload)
            if values is None:
                continue
            self.samples.append((time.monotonic(), *values))


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
        subscribe_topic=[ADC_TOPIC],
        publish_topic=None,  # subscribe-only participant
        publish_value=0,
        publish_interval=0.0,
        echo_received=False,
        reliable=False,
        type_name=ADC_TYPE_NAME,
        announce_period=1.0,
        duration=0.0,
        trace_packets=cli.trace_packets,
        peer=cli.peer,
        peer_participant_ids=cli.peer_participant_ids,
    )


def run_plot(cli: argparse.Namespace, samples: collections.deque) -> None:
    import matplotlib.pyplot as plt
    from matplotlib.animation import FuncAnimation

    full_scale = cli.full_scale
    fig, (ax_xy, ax_twist) = plt.subplots(
        1, 2, figsize=(10, 6), gridspec_kw={"width_ratios": [3, 1]}
    )
    fig.canvas.manager.set_window_title("Tab5 joystick (RTPS)")

    # X/Y position plot
    ax_xy.set_xlim(0, full_scale)
    ax_xy.set_ylim(0, full_scale)
    ax_xy.set_aspect("equal")
    ax_xy.set_xlabel("X (mV)")
    ax_xy.set_ylabel("Y (mV)")
    ax_xy.set_title("X/Y position")
    ax_xy.grid(True, alpha=0.3)
    (trail_line,) = ax_xy.plot([], [], "-", color="tab:blue", alpha=0.4, linewidth=1.5)
    (dot,) = ax_xy.plot([], [], "o", color="tab:blue", markersize=12)

    # Twist bar
    ax_twist.set_xlim(0, 1)
    ax_twist.set_ylim(0, full_scale)
    ax_twist.set_xticks([])
    ax_twist.set_ylabel("Twist (mV)")
    ax_twist.set_title("Twist")
    ax_twist.grid(True, axis="y", alpha=0.3)
    twist_bar = ax_twist.bar([0.5], [0], width=0.6, color="tab:orange")[0]

    readout = fig.text(0.5, 0.02, "waiting for samples...", ha="center", family="monospace")

    def update(_frame):
        if not samples:
            return trail_line, dot, twist_bar, readout
        now = time.monotonic()
        recent = list(samples)
        t_last, x, y, twist = recent[-1]
        stale = (now - t_last) > 1.0

        trail = recent[-cli.trail:]
        trail_line.set_data([s[1] for s in trail], [s[2] for s in trail])
        dot.set_data([x], [y])
        color = "gray" if stale else "tab:blue"
        dot.set_color(color)
        trail_line.set_color(color)
        twist_bar.set_height(twist)
        twist_bar.set_color("gray" if stale else "tab:orange")

        # measured incoming rate over the last second
        window_start = now - 1.0
        rate = sum(1 for s in recent if s[0] >= window_start)
        status = "STALE" if stale else f"{rate:3d} Hz"
        readout.set_text(
            f"X {x:4d} mV   Y {y:4d} mV   Twist {twist:4d} mV   [{status}]"
        )
        return trail_line, dot, twist_bar, readout

    _anim = FuncAnimation(fig, update, interval=max(1, int(1000 / cli.fps)), blit=False)
    plt.tight_layout(rect=(0, 0.05, 1, 1))
    plt.show()  # blocks until the window is closed


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Live plot of the Tab5 joystick ADC stream over RTPS."
    )
    parser.add_argument("--fps", type=float, default=30.0, help="Plot refresh rate (default 30)")
    parser.add_argument("--trail", type=int, default=30,
                        help="Number of samples in the X/Y trail (default 30, ~1s at 30 Hz)")
    parser.add_argument("--full-scale", type=int, default=3300,
                        help="Axis/bar range in mV (default 3300)")
    parser.add_argument("--node-name", default="adc_plot", help="Local participant name")
    parser.add_argument("--domain-id", type=int, default=0, help="RTPS domain id (must match the Tab5)")
    parser.add_argument(
        "--participant-id", type=int, default=12,
        help="Local participant id; keep distinct from rtps_host.py (10) and rtps_brightness.py (11)",
    )
    parser.add_argument("--bind-address", default=None, help="Local bind address")
    parser.add_argument("--advertised-address", default=None,
                        help="IPv4 address to advertise (defaults to best-effort local IPv4)")
    parser.add_argument("--multicast-interface", default=None,
                        help="IPv4 interface for multicast join/send")
    parser.add_argument("--multicast-group", default="239.255.0.1",
                        help="RTPS metatraffic multicast group")
    parser.add_argument(
        "--peer", action="append", default=None, metavar="HOST",
        help="Hostname or IP of the Tab5, to reach it without multicast discovery (repeatable). "
             "Needed when it is routed rather than on-link (Tailscale, VPN, another subnet). "
             "Usually 'espressif'.")
    parser.add_argument(
        "--peer-participant-ids", type=rtps_host.parse_participant_id_range, default="0-3",
        metavar="IDS",
        help="Participant ids to try on each --peer, as '0-3' or '0,1,2' (default 0-3)")
    parser.add_argument("--trace-packets", action="store_true",
                        help="Log every received UDP packet and its RTPS submessage headers")
    cli = parser.parse_args()
    if cli.fps <= 0:
        parser.error("--fps must be > 0")
    if cli.trail < 1:
        parser.error("--trail must be >= 1")

    # keep a bit more history than the trail needs, for the rate estimate
    samples: collections.deque = collections.deque(maxlen=max(cli.trail, 300))
    harness = AdcPlotHarness(build_harness_args(cli), samples)
    network = threading.Thread(target=harness.run, daemon=True)
    network.start()

    print(f"Subscribed to '{ADC_TOPIC}' [{ADC_TYPE_NAME}]; waiting for the Tab5...")
    run_plot(cli, samples)
    return 0


if __name__ == "__main__":
    sys.exit(main())
