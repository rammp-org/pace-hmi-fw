#!/usr/bin/env python3
"""One real ROS 2 stand-in (rclpy, whatever RMW the environment selects) for the stress
bench: a filler node with rclpy's own discovery footprint, plus

- /rammp/stress/to_hmi (std_msgs/UInt8MultiArray) at the rate in the control file, data
  starting <sender id, seq> (little-endian u32s) as the HMI's stress reader expects;
- /rammp/joystick/xy_twist (rammp_peripheral_interfaces/XYTwist), counted.

rtps_stress.py rossweep starts these, sets the rate through --control (JSON
{"rate_hz": x}) and reads --status (JSON {"sent": n, "joy_rx": n}), so steering the
bench adds no DDS traffic of its own.
"""

import argparse
import json
import os
import struct
import time

import rclpy
from rclpy.node import Node
from rclpy.qos import HistoryPolicy, QoSProfile, ReliabilityPolicy
from std_msgs.msg import UInt8MultiArray

try:
    from rammp_peripheral_interfaces.msg import XYTwist
except ImportError:  # the workspace overlay is not sourced: no joystick, still a stand-in
    XYTwist = None

BEST_EFFORT = QoSProfile(reliability=ReliabilityPolicy.BEST_EFFORT,
                         history=HistoryPolicy.KEEP_LAST, depth=5)


class StandIn(Node):
    def __init__(self, cli: argparse.Namespace) -> None:
        super().__init__(cli.name, namespace=cli.namespace)
        self.cli = cli
        self.sent = 0
        self.joy_rx = 0
        self.rate = 0.0
        self.pad = bytes(max(cli.size, 8) - 8)
        self.pub = self.create_publisher(UInt8MultiArray, "/rammp/stress/to_hmi", BEST_EFFORT)
        if cli.joystick and XYTwist is not None:
            self.create_subscription(XYTwist, "/rammp/joystick/xy_twist", self.on_joy, BEST_EFFORT)
        self.stress_timer = None
        self.create_timer(0.5, self.poll)

    def on_joy(self, _msg) -> None:
        self.joy_rx += 1

    def send(self) -> None:
        self.sent += 1
        msg = UInt8MultiArray()
        msg.data = struct.pack("<II", self.cli.sender_id, self.sent) + self.pad
        self.pub.publish(msg)

    def poll(self) -> None:
        try:
            with open(self.cli.control, encoding="utf-8") as handle:
                rate = float(json.load(handle).get("rate_hz", 0.0))
        except (OSError, ValueError):
            rate = self.rate
        if rate != self.rate:
            self.rate = rate
            if self.stress_timer is not None:
                self.destroy_timer(self.stress_timer)
                self.stress_timer = None
            if rate > 0:
                self.stress_timer = self.create_timer(1.0 / rate, self.send)
        tmp = self.cli.status + ".tmp"
        try:
            with open(tmp, "w", encoding="utf-8") as handle:
                json.dump({"sent": self.sent, "joy_rx": self.joy_rx, "t": time.time()}, handle)
            os.replace(tmp, self.cli.status)
        except OSError:
            pass  # Windows: the sweep is reading the file right now; the next poll writes it


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--name", required=True)
    parser.add_argument("--namespace", default="/rammp/stress")
    parser.add_argument("--sender-id", type=int, required=True)
    parser.add_argument("--size", type=int, default=256)
    parser.add_argument("--joystick", action="store_true")
    parser.add_argument("--control", required=True)
    parser.add_argument("--status", required=True)
    cli, ros_args = parser.parse_known_args()
    rclpy.init(args=ros_args)
    node = StandIn(cli)
    try:
        rclpy.spin(node)
    except (KeyboardInterrupt, rclpy.executors.ExternalShutdownException):
        pass


if __name__ == "__main__":
    main()
