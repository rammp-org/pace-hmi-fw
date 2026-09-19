#!/usr/bin/env python3
"""End-to-end test of rtps_serial.py, with a stand-in for the HMI: no board needed.

  python rtps_serial_test.py            # exit 0 = pass, 1 = fail

A FakeHmi plays the device's side of "Serial over RTPS" and of the firmware update
(hmi_rtps_spec.hpp), on this PC's own RTPS. The bridge runs as it would for a
person, and this drives it as they would:

  1. attach to the port: the history arrives, then the live output
  2. type a command: it is echoed and answered
  3. esptool through the RFC 2217 port (DTR/RTS resets and all): it connects,
     and the loader session ends cleanly without an update
  4. esptool write-flash of build/ (flash_args), through the raw port: the app
     must reach the FakeHmi as an OTA update, byte for byte, and the device come
     back running it

Run it with the ESP-IDF Python (it has pyserial and esptool), after `idf.py build`.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import random
import socket
import subprocess
import sys
import threading
import time
import zlib
from typing import List, Optional

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import rammp_rtps as spec  # noqa: E402
import rtps_host  # noqa: E402
import rtps_net  # noqa: E402
import rtps_ota  # noqa: E402

sys.path.insert(0, rtps_ota.ESPP_OTA_PYTHON)
from espp_ota import frame as F  # noqa: E402
from espp_ota import protocol as P  # noqa: E402
from espp_ota.protocol import MessageType  # noqa: E402

FAKE_MAC = "02:00:00:5e:1a:01"
FAKE_PARTICIPANT = 3  # among the ids a bridge probes a --peer on (0-3)


class FakeHmi(rtps_host.RtpsHostHarness):
    """The device: OtaDeviceInfo every second, the serial console, and updates."""

    def __init__(self, address: str) -> None:
        super().__init__(argparse.Namespace(
            node_name="fake_hmi", domain_id=0, participant_id=FAKE_PARTICIPANT,
            bind_address=address, advertised_address=address, multicast_interface=None,
            multicast_group="239.255.0.1", enclave="/",
            subscribe_topic=[spec.TOPIC_OTA_COMMAND], subscribe_type_name=spec.TYPE_OTA_COMMAND,
            publish_topic=spec.TOPIC_OTA_DEVICE_INFO, type_name=spec.TYPE_OTA_DEVICE_INFO,
            publish_value=0, publish_interval=1.0, echo_received=False, reliable=False,
            announce_period=1.0, duration=0.0, trace_packets=False, peer=None,
            peer_participant_ids=[], quiet=True))
        self.local_readers.append(rtps_host.ReaderConfig(
            topic_name=spec.TOPIC_SERIAL_RX, type_name=spec.TYPE_SERIAL_DATA, reliable=False,
            entity_index=len(self.local_readers)))
        self.serial_writer = rtps_host.WriterConfig(
            topic_name=spec.TOPIC_SERIAL_TX, type_name=spec.TYPE_SERIAL_DATA, reliable=False,
            entity_index=len(self.local_writers))
        self.local_writers.append(self.serial_writer)
        self.address = address
        self.lock = threading.RLock()
        self.version = "v0.0.1-fake"
        self.slot = "ota_0"
        self.image_state = spec.OTA_IMAGE_STATE_UNKNOWN  # noqa: F821
        self.state = spec.OTA_STATE_IDLE  # noqa: F821
        self.nonce = random.randint(1, 0xFFFFFFFF)
        self.ota_port = 0
        self.progress = 0
        self.last_error = ""
        self.silent_until = 0.0
        self.received_image: Optional[bytes] = None
        self.boot()

    # -- a boot: new session, fresh log
    def boot(self) -> None:
        with self.lock:
            self.session = random.randint(1, 0xFFFFFFFF)
            self.seq = 0
            self.hosts: dict = {}
            self.lines: List[str] = []
            self.line = ""
        self.log(f"[ota/I][0.1]: Running {self.version} from {self.slot}")
        self.log("[rtps_comms/I][9.9]: RTPS up on " + self.address)

    def log(self, text: str) -> None:
        """A line printed: kept for the history, and sent live to attached hosts."""
        with self.lock:
            self.lines = (self.lines + [text])[-50:]
            live = bool(self.hosts)
        if live:
            self.send(spec.SERIAL_KIND_DATA, (text + "\n").encode())  # noqa: F821

    def send(self, kind: int, data: bytes, to: int = 0) -> None:
        if time.monotonic() < self.silent_until:
            return
        with self.lock:
            self.seq += 1
            message = spec.SerialData(self.session, self.seq, kind, to, FAKE_MAC, data)
        payload = self.build_data_message(self.serial_writer, spec.pack_serial_data(message))
        for target in self._build_user_targets(self.serial_writer):
            self.send_user_datagram(payload, target)

    def publish_now(self) -> None:
        if time.monotonic() < self.silent_until:
            return
        info = spec.OtaDeviceInfo(
            seq=self.seq & 0xFF, state=self.state, image_state=self.image_state,
            progress_pct=self.progress, ota_port=self.ota_port, nonce=self.nonce, mac=FAKE_MAC,
            ip=self.address, project="rammp-hmi-p4", version=self.version, hw_rev="fake",
            slot=self.slot, last_error=self.last_error,
            features=spec.OTA_FEATURE_ZLIB)  # noqa: F821
        payload = self.build_data_message(self.local_writers[0], spec.pack_ota_device_info(info))
        for target in self._build_user_targets(self.local_writers[0]):
            self.send_user_datagram(payload, target)

    # -- what arrives
    def handle_user_packet(self, packet: bytes, sender_ip: str, sender_port: int) -> None:
        if time.monotonic() < self.silent_until:
            return
        for guid_prefix, writer_id, payload, reader_id in rtps_host.parse_rtps_data_messages(packet):
            topic = self.topic_for_sample(guid_prefix, writer_id, reader_id)
            if topic == spec.TOPIC_SERIAL_RX:
                m = spec.unpack_serial_data(payload)
                if m is not None and m.mac in ("", FAKE_MAC):
                    self.on_serial(m)
            elif topic == spec.TOPIC_OTA_COMMAND:
                c = spec.unpack_ota_command(payload)
                if c is not None and c.mac == FAKE_MAC:
                    self.on_ota(c)

    def on_serial(self, m: spec.SerialData) -> None:
        if m.kind == spec.SERIAL_KIND_HELLO:  # noqa: F821
            with self.lock:
                new = m.session not in self.hosts
                self.hosts[m.session] = time.monotonic()
                lines = list(self.lines)
            if new:
                text = "".join(line + "\n" for line in ["--- serial over RTPS: fake ---"] + lines)
                for at in range(0, len(text), 1024):
                    self.send(spec.SERIAL_KIND_HISTORY, text[at:at + 1024].encode(), m.session)  # noqa: F821
                self.send(spec.SERIAL_KIND_HISTORY, b"", m.session)  # noqa: F821
        elif m.kind == spec.SERIAL_KIND_BYE:  # noqa: F821
            with self.lock:
                self.hosts.pop(m.session, None)
        elif m.kind == spec.SERIAL_KIND_DATA:  # noqa: F821
            for ch in m.data.decode("ascii", "ignore"):
                if ch in "\r\n":
                    self.send(spec.SERIAL_KIND_DATA, b"\r\n")  # noqa: F821
                    line, self.line = self.line.strip(), ""
                    if line == "help":
                        self.log("commands: info, mem, selftest, reboot")
                    elif line:
                        self.log(f"unknown command '{line}' (help lists them)")
                else:
                    self.line += ch
                    self.send(spec.SERIAL_KIND_DATA, ch.encode())  # noqa: F821

    def on_ota(self, c: spec.OtaCommand) -> None:
        signed = spec.sign_ota_command(c, rtps_ota.DEFAULT_KEY.encode())
        if c.nonce != self.nonce or c.auth != signed.auth:
            return
        self.nonce = random.randint(1, 0xFFFFFFFF)
        if c.action == spec.OTA_ACTION_START:  # noqa: F821
            listener = socket.create_server((self.address, 0))
            self.ota_port = listener.getsockname()[1]
            self.state = spec.OTA_STATE_LISTENING  # noqa: F821
            threading.Thread(target=self.receive, args=(listener, c), daemon=True).start()
        self.publish_now()

    def receive(self, listener: socket.socket, start: spec.OtaCommand) -> None:
        listener.settimeout(30)
        client, _ = listener.accept()
        listener.close()
        self.state = spec.OTA_STATE_RECEIVING  # noqa: F821
        parser = F.StreamParser()
        inflate = zlib.decompressobj() if start.encoding == spec.OTA_ENCODING_ZLIB else None  # noqa: F821
        image = bytearray()
        received = 0  # on the wire: what the host counts its progress in
        done = False
        with client:
            while not done:
                data = client.recv(65536)
                if not data:
                    break
                for fr in parser.feed(data):
                    if fr.type == MessageType.BEGIN:
                        client.sendall(P._build(MessageType.OK, (0).to_bytes(4, "little")))
                    elif fr.type == MessageType.DATA:
                        image += inflate.decompress(fr.payload) if inflate else fr.payload
                        received += len(fr.payload)
                        self.progress = len(image) * 100 // max(start.image_size, 1)
                        client.sendall(P._build(MessageType.OK, received.to_bytes(4, "little")))
                    elif fr.type == MessageType.FINISH:
                        ok = (len(image) == start.image_size
                              and hashlib.sha256(image).hexdigest() == start.sha256)
                        if not ok:
                            client.sendall(P._build(MessageType.ERROR, (22).to_bytes(4, "little")
                                                    + b"sha256 mismatch"))
                            self.state = spec.OTA_STATE_FAILED  # noqa: F821
                            return
                        client.sendall(P._build(MessageType.OK, received.to_bytes(4, "little")))
                        done = True
        self.received_image = bytes(image)
        # the reboot: silent for a while, then the new image, confirmed
        self.state = spec.OTA_STATE_REBOOTING  # noqa: F821
        self.log("[ota/I][60.0]: Image written, rebooting")
        time.sleep(0.5)
        self.silent_until = float("inf")
        time.sleep(6.0)
        self.version, self.slot = start.version, "ota_1" if self.slot == "ota_0" else "ota_0"
        self.image_state = spec.OTA_IMAGE_STATE_CONFIRMED  # noqa: F821
        self.state, self.ota_port, self.progress = spec.OTA_STATE_IDLE, 0, 0  # noqa: F821
        self.silent_until = 0.0  # back, as the new image
        self.boot()


# ---------------------------------------------------------------- the test


class Checks:
    def __init__(self) -> None:
        self.failed = 0

    def check(self, name: str, ok: bool, detail: str = "") -> bool:
        print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"  ({detail})" if detail else ""),
              flush=True)
        self.failed += 0 if ok else 1
        return ok


def read_until(sock: socket.socket, needle: bytes, timeout: float, got: bytearray) -> bool:
    deadline = time.monotonic() + timeout
    while needle not in got and time.monotonic() < deadline:
        sock.settimeout(max(0.05, deadline - time.monotonic()))
        try:
            data = sock.recv(65536)
        except socket.timeout:
            break
        if not data:
            break
        got += data
    return needle in got


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--build", default=os.path.join(REPO, "build"))
    parser.add_argument("--rfc2217", type=int, default=4400)
    parser.add_argument("--tcp", type=int, default=4401)
    parser.add_argument("--keep-log", action="store_true", help="print the bridge's output")
    cli = parser.parse_args()
    rtps_host.log = lambda message: None

    # The address this PC reaches itself on, as a LAN peer would see it.
    address = rtps_net.source_address_for("192.0.2.1") or rtps_host.guess_local_ipv4()
    hmi = FakeHmi(address)
    threading.Thread(target=hmi.run, daemon=True).start()

    bridge = subprocess.Popen(
        [sys.executable, "-u", os.path.join(HERE, "rtps_serial.py"), "--device", FAKE_MAC,
         "--peer", address, "--advertised-address", address, "--rfc2217", str(cli.rfc2217),
         "--tcp", str(cli.tcp), "--discover", "15"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8",
        errors="replace")
    bridge_log: List[str] = []
    threading.Thread(target=lambda: bridge_log.extend(iter(bridge.stdout.readline, "")),
                     daemon=True).start()
    t = Checks()
    try:
        # 1. attach: the history, then live output
        deadline = time.monotonic() + 20
        sock = None
        while sock is None and time.monotonic() < deadline:
            try:
                sock = socket.create_connection(("127.0.0.1", cli.tcp), timeout=1)
            except OSError:
                time.sleep(0.3)
        if not t.check("bridge serving", sock is not None, "".join(bridge_log[-5:]).strip()):
            return 1
        got = bytearray()
        t.check("history on attach", read_until(sock, b"RTPS up on", 10, got),
                got[-200:].decode(errors="replace"))
        hmi.log("[rtps_comms/I][12.3]: a live line")
        t.check("live output", read_until(sock, b"a live line", 5, got))

        # 2. a command, echoed and answered
        sock.sendall(b"help\r")
        t.check("command echoed and answered", read_until(sock, b"commands: info", 5, got),
                got[-120:].decode(errors="replace"))
        sock.close()

        # 3. esptool over RFC 2217: resets, the loader, nothing written
        mac = subprocess.run(
            [sys.executable, "-m", "esptool", "--chip", "esp32p4", "-p",
             f"rfc2217://127.0.0.1:{cli.rfc2217}", "read-mac"],
            capture_output=True, text=True, timeout=120)
        t.check("esptool read-mac over RFC 2217", mac.returncode == 0 and FAKE_MAC in mac.stdout,
                " | ".join(line for line in mac.stdout.splitlines() if "MAC" in line))
        time.sleep(1)
        t.check("loader session ended", any("loader: esptool reset the chip" in line
                                            for line in bridge_log))

        # 4. esptool write-flash through the raw port
        flash_args = os.path.join(cli.build, "flash_args")
        app = os.path.join(cli.build, "rammp-hmi-p4.bin")
        if not t.check("build present", os.path.exists(flash_args) and os.path.exists(app),
                       cli.build):
            return 1
        before = time.monotonic()
        esptool = subprocess.run(
            [sys.executable, "-m", "esptool", "--chip", "esp32p4", "-p",
             f"socket://127.0.0.1:{cli.tcp}", "-b", "460800", "--before", "default-reset",
             "--after", "hard-reset", "write-flash", "@flash_args"],
            cwd=cli.build, capture_output=True, text=True, timeout=300)
        tail = [line for line in (esptool.stdout + esptool.stderr).splitlines()
                if line.startswith(("Wrote", "Hash", "Stub", "Changed", "Connecting"))]
        t.check("esptool write-flash", esptool.returncode == 0,
                f"{time.monotonic() - before:.1f} s; " + " | ".join(tail))

        with open(app, "rb") as handle:
            expected = handle.read()
        deadline = time.monotonic() + 90
        while hmi.version == "v0.0.1-fake" and time.monotonic() < deadline:
            time.sleep(0.5)
        t.check("app reached the device", hmi.received_image == expected,
                f"{len(hmi.received_image or b'')} of {len(expected)} B")
        image = rtps_ota.Image(app)
        t.check("device runs it", hmi.version == image.version and
                hmi.image_state == spec.OTA_IMAGE_STATE_CONFIRMED,  # noqa: F821
                f"{hmi.version} on {hmi.slot}")
        deadline = time.monotonic() + 60
        while not any("update done" in line or "FAILED" in line for line in bridge_log) \
                and time.monotonic() < deadline:
            time.sleep(0.5)
        t.check("bridge reports the update", any("update done" in line for line in bridge_log))
    finally:
        bridge.terminate()
        try:
            bridge.wait(5)
        except subprocess.TimeoutExpired:
            bridge.kill()
        hmi.stop()
        if cli.keep_log or t.failed:
            print("\n--- bridge output ---\n" + "".join(bridge_log))
    print(f"\n{'PASS' if not t.failed else 'FAIL'}: {t.failed} failed")
    return 1 if t.failed else 0


if __name__ == "__main__":
    sys.exit(main())
