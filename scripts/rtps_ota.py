#!/usr/bin/env python3
"""Update firmware over Ethernet: find devices on RTPS, then flash them over TCP.

  python rtps_ota.py list                          # who is out there, running what
  python rtps_ota.py flash build/rammp-hmi-p4.bin  # every device found (one command, the fleet)
  python rtps_ota.py flash app.bin --device aa:bb:cc:dd:ee:ff   # just these (repeatable)
  python rtps_ota.py abort --device aa:bb:cc:dd:ee:ff
  python rtps_ota.py console [--device MAC]        # the network console (logs + commands)

The protocol is "Firmware update over Ethernet" in main/hmi_rtps_spec.hpp: each
device publishes OtaDeviceInfo; a START signed with the shared key (--key,
RAMMP_OTA_KEY, or the public bench key) makes it open a TCP port for one
update; the image goes over that with espp's OTA stream protocol (the espp_ota
package the espp/ota component ships, driven through TcpTransport below); the
device reboots, confirms the new image once its network is up, and this tool
reports success when it sees the new version confirmed - or the old one back,
which means the new image rolled back.

Exit code: 0 all updated, 1 any failed, 2 no device found.

Devices are found the way the other tools find the board: --peer (repeatable),
else the one that worked last time, plus multicast discovery on a LAN.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import socket
import struct
import sys
import threading
import time
import zlib
from typing import Callable, Dict, List, Optional

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import rammp_rtps as spec  # noqa: E402  (path setup must run first)
import rtps_host  # noqa: E402
import rtps_net  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
#: the espp/ota component's host package, fetched with the component
ESPP_OTA_PYTHON = os.path.join(REPO, "managed_components", "espp__ota", "python")
DEFAULT_KEY = "rammp-dev-ota-key"  # CONFIG_HMI_OTA_AUTH_KEY's default: public
PARTICIPANT_ID = 16  # clear of the other tools (10-15) and rtps_net's probe (19)

#: esp_image_header_t (24) + esp_image_segment_header_t (8), then esp_app_desc_t
APP_DESC_OFFSET = 32
APP_DESC_MAGIC = 0xABCD5432

EXIT_OK, EXIT_FAIL, EXIT_NO_DEVICE = 0, 1, 2

#: how long after the device was last heard that it counts as gone (rebooting)
GONE_S = 4 * spec.OTA_INFO_PERIOD_MS / 1000.0  # noqa: F821  (scraped)
COMMAND_RESEND_S = 1.0
COMMAND_WAIT_S = 10.0
#: FINISH -> the new image confirmed: the reboot, DHCP and RTPS, with margin
CONFIRM_WAIT_S = spec.OTA_CONFIRM_TIMEOUT_MS / 1000.0 + 60.0  # noqa: F821


def _import_espp_ota():
    sys.path.insert(0, ESPP_OTA_PYTHON)
    try:
        from espp_ota.client import OtaClient  # noqa: F401
        from espp_ota.protocol import OtaError  # noqa: F401
    except ImportError as exc:
        raise SystemExit(f"espp_ota not found at {ESPP_OTA_PYTHON}: run `idf.py build` once so the "
                         f"component manager fetches espp/ota ({exc})")
    return OtaClient, OtaError


class TcpTransport:
    """The byte-stream transport espp_ota.OtaClient drives, over TCP.

    Same interface as espp_ota.transport.UsbVendorTransport: write(), and read()
    returning b"" on a timeout.
    """

    def __init__(self, host: str, port: int, connect_timeout: float = 10.0) -> None:
        self.host, self.port = host, port
        self._sock = socket.create_connection((host, port), timeout=connect_timeout)
        self._sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def write(self, data: bytes, timeout_ms: int = 5000) -> None:
        self._sock.settimeout(timeout_ms / 1000.0)
        self._sock.sendall(data)

    def read(self, max_len: int, timeout_ms: int = 5000) -> bytes:
        self._sock.settimeout(timeout_ms / 1000.0)
        try:
            data = self._sock.recv(max_len)
        except socket.timeout:
            return b""
        if not data:
            raise ConnectionError("the device closed the connection")
        return data

    def close(self) -> None:
        self._sock.close()

    @property
    def description(self) -> str:
        return f"tcp://{self.host}:{self.port}"

    def __enter__(self) -> "TcpTransport":
        return self

    def __exit__(self, *exc) -> None:
        self.close()


class Image:
    """An app .bin and what the device will check it against."""

    def __init__(self, path: str) -> None:
        with open(path, "rb") as handle:
            self.data = handle.read()
        self.path = path
        self.sha256 = hashlib.sha256(self.data).hexdigest()
        desc = self.data[APP_DESC_OFFSET:APP_DESC_OFFSET + 256]
        if len(desc) < 256 or struct.unpack_from("<I", desc, 0)[0] != APP_DESC_MAGIC:
            raise SystemExit(f"{path}: not an ESP-IDF app image (no app description)")
        self.version = desc[16:48].split(b"\0", 1)[0].decode("ascii", "replace")
        self.project = desc[48:80].split(b"\0", 1)[0].decode("ascii", "replace")
        self._zlib: Optional[bytes] = None

    @property
    def zlib(self) -> bytes:
        """The image as one zlib stream, made once."""
        if self._zlib is None:
            self._zlib = zlib.compress(self.data, 9)
        return self._zlib


class OtaHost(rtps_host.RtpsHostHarness):
    """Hears OtaDeviceInfo from every device, sends OtaCommand on demand."""

    def __init__(self, args: argparse.Namespace) -> None:
        super().__init__(args)
        self.devices: Dict[str, spec.OtaDeviceInfo] = {}
        self.heard_at: Dict[str, float] = {}
        self.changed = threading.Condition()

    def publish_now(self) -> None:
        pass  # commands go out only when asked

    def handle_user_packet(self, packet: bytes, sender_ip: str, sender_port: int) -> None:
        for guid_prefix, writer_id, payload, reader_id in rtps_host.parse_rtps_data_messages(packet):
            if self.topic_for_sample(guid_prefix, writer_id, reader_id) != spec.TOPIC_OTA_DEVICE_INFO:
                continue
            info = spec.unpack_ota_device_info(payload)
            if info is None or not info.mac:
                continue
            with self.changed:
                self.devices[info.mac] = info
                self.heard_at[info.mac] = time.monotonic()
                self.changed.notify_all()

    def send_command(self, command: spec.OtaCommand, legacy: bool = False) -> int:
        writer = self.local_writers[0]
        payload = self.build_data_message(writer, spec.pack_ota_command(command, legacy))
        targets = self._build_user_targets(writer)
        for target in targets:
            self.send_user_datagram(payload, target)
        return len(targets)

    def forget(self, address: str) -> None:
        """A device rebooted: run discovery with it again from the start."""
        for guid, participant in list(self.discovered_participants.items()):
            if participant.address == address:
                self.discovered_participants.pop(guid, None)
                self.sedp_announce_counts.pop(guid, None)
        for target in list(self.peer_sedp_counts):
            if target[0] == address:
                self.peer_sedp_counts.pop(target, None)

    def wait_for(self, mac: str, predicate: Callable[[Optional[spec.OtaDeviceInfo], float], bool],
                 timeout: float) -> Optional[spec.OtaDeviceInfo]:
        """Until predicate(latest info, seconds since heard) holds; None on timeout."""
        deadline = time.monotonic() + timeout
        with self.changed:
            while True:
                info = self.devices.get(mac)
                age = time.monotonic() - self.heard_at.get(mac, 0.0)
                if predicate(info, age):
                    return info
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                self.changed.wait(min(remaining, 0.5))

    def snapshot(self) -> Dict[str, spec.OtaDeviceInfo]:
        with self.changed:
            return dict(self.devices)


def describe(info: spec.OtaDeviceInfo) -> str:
    image = spec.OTA_IMAGE_STATE_NAMES.get(info.image_state, "?").lower()
    state = spec.OTA_STATE_NAMES.get(info.state, "?")
    extra = f" ({info.last_error})" if info.last_error else ""
    return (f"{info.mac}  {info.ip:<15} {info.project} {info.version} on {info.slot} [{image}]  "
            f"update {state}{extra}")


def flash_one(host: OtaHost, mac: str, image: Image, key: bytes,
              say: Callable[[str], None], compress: bool = True) -> bool:
    OtaClient, OtaError = _import_espp_ota()
    before = host.devices[mac]
    if before.project != image.project:
        say(f"skipped: it runs {before.project}, the image is {image.project}")
        return False
    # A device that predates `features` takes neither zlib nor the new command layout.
    legacy = not before.features
    compress = compress and bool(before.features & spec.OTA_FEATURE_ZLIB)  # noqa: F821
    encoding = spec.OTA_ENCODING_ZLIB if compress else spec.OTA_ENCODING_RAW  # noqa: F821
    wire = image.zlib if compress else image.data

    # 1. START, resent until the device answers by moving its nonce on
    command = spec.sign_ota_command(spec.OtaCommand(
        action=spec.OTA_ACTION_START, nonce=before.nonce, image_size=len(image.data),  # noqa: F821
        mac=mac, version=image.version, sha256=image.sha256, encoding=encoding), key)
    deadline = time.monotonic() + COMMAND_WAIT_S
    answer = None
    while answer is None and time.monotonic() < deadline:
        host.send_command(command, legacy)
        answer = host.wait_for(mac, lambda i, _age: i is not None and i.nonce != command.nonce,
                               COMMAND_RESEND_S)
    if answer is None:
        say("no answer to START (is its firmware new enough to have OTA?)")
        return False
    if answer.state != spec.OTA_STATE_LISTENING or not answer.ota_port:  # noqa: F821
        say(f"START refused: {answer.last_error or spec.OTA_STATE_NAMES.get(answer.state)}")
        return False

    # 2. the image, over TCP
    say(f"sending {image.version} ({len(image.data)} B"
        + (f", {len(wire)} B compressed" if compress else "")
        + f") to {answer.ip}:{answer.ota_port}")
    shown = [-1]

    def progress(written: int, total: int) -> None:
        decile = written * 10 // max(total, 1)
        if decile != shown[0]:
            shown[0] = decile
            say(f"  {decile * 10:3d}%")

    started = time.monotonic()
    try:
        with TcpTransport(answer.ip, answer.ota_port) as transport:
            # zlib or not, BEGIN announces the image's own size
            OtaClient(transport, progress=progress).flash(wire, image_size=len(image.data))
    except (OtaError, OSError) as exc:
        say(f"transfer failed: {exc}")
        return False
    elapsed = time.monotonic() - started
    say(f"written in {elapsed:.1f} s ({len(image.data) / elapsed / 1024:.0f} KiB/s); rebooting")

    # 3. the new image has to come back and confirm itself, or it rolls back
    if host.wait_for(mac, lambda _i, age: age > GONE_S, 30.0) is None:
        say("it never went away to reboot")
        return False
    # While it is silent (booting - and again if it rolls back), discovery with it keeps
    # starting over: the harness only sends a few rounds of SEDP, which a device that
    # is not up yet misses.
    def settled(i: Optional[spec.OtaDeviceInfo], age: float) -> bool:
        return age < GONE_S and i is not None and (
            i.slot == before.slot or i.image_state == spec.OTA_IMAGE_STATE_CONFIRMED)  # noqa: F821

    deadline = time.monotonic() + CONFIRM_WAIT_S
    back = None
    pending_seen = False
    while back is None and time.monotonic() < deadline:
        latest = host.devices[mac]
        if time.monotonic() - host.heard_at[mac] > GONE_S:
            host.forget(answer.ip)
        elif not pending_seen and latest.slot != before.slot:
            pending_seen = True
            say(f"booted {latest.version} on {latest.slot}; waiting for it to confirm itself")
        back = host.wait_for(mac, settled, 3.0)
    if back is None:
        say(f"it did not come back confirmed within {CONFIRM_WAIT_S:.0f} s")
        return False
    if back.slot == before.slot:
        say(f"ROLLED BACK: running {back.version} on {back.slot} again")
        return False
    if back.version != image.version:
        say(f"now runs {back.version}, not {image.version}")
        return False
    say(f"OK: {before.version} -> {back.version} on {back.slot}, confirmed")
    return True


def abort_one(host: OtaHost, mac: str, key: bytes, say: Callable[[str], None]) -> bool:
    info = host.devices[mac]
    command = spec.sign_ota_command(spec.OtaCommand(
        action=spec.OTA_ACTION_ABORT, nonce=info.nonce, image_size=0, mac=mac,  # noqa: F821
        version="", sha256=""), key)
    deadline = time.monotonic() + COMMAND_WAIT_S
    while time.monotonic() < deadline:
        host.send_command(command, legacy=not info.features)
        if host.wait_for(mac, lambda i, _age: i is not None and i.nonce != command.nonce,
                         COMMAND_RESEND_S):
            say("abort sent")
            return True
    say("no answer to ABORT")
    return False


def console(address: str) -> int:
    """A plain TCP terminal on the network console: lines typed go to the device."""
    sock = socket.create_connection((address, spec.CONSOLE_PORT), timeout=5.0)  # noqa: F821
    sock.settimeout(None)
    print(f"connected to {address}:{spec.CONSOLE_PORT}; Ctrl-C to leave", flush=True)  # noqa: F821

    def pump() -> None:
        while True:
            data = sock.recv(4096)
            if not data:
                print("\n[the device closed the console]", flush=True)
                os._exit(0)
            sys.stdout.write(data.decode("utf-8", "replace"))
            sys.stdout.flush()

    threading.Thread(target=pump, daemon=True).start()
    try:
        for line in sys.stdin:
            sock.sendall(line.encode("utf-8"))
    except KeyboardInterrupt:
        pass
    return EXIT_OK


def harness_args(cli: argparse.Namespace, peers: List[str]) -> argparse.Namespace:
    advertised = (cli.advertised_address
                  or (rtps_net.source_address_for(peers[0]) if peers else None)
                  or rtps_host.guess_local_ipv4())
    return argparse.Namespace(
        node_name="ota_host", domain_id=cli.domain_id, participant_id=PARTICIPANT_ID,
        bind_address=advertised, advertised_address=advertised, multicast_interface=None,
        multicast_group="239.255.0.1", enclave="/",
        subscribe_topic=[spec.TOPIC_OTA_DEVICE_INFO], subscribe_type_name=spec.TYPE_OTA_DEVICE_INFO,
        publish_topic=spec.TOPIC_OTA_COMMAND, type_name=spec.TYPE_OTA_COMMAND, publish_value=0,
        publish_interval=0.0, echo_received=False, reliable=False, announce_period=1.0,
        duration=0.0, trace_packets=cli.verbose, peer=peers or None,
        peer_participant_ids=rtps_host.parse_participant_id_range("0-3"), quiet=not cli.verbose,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("command", choices=("list", "flash", "abort", "console"))
    parser.add_argument("image", nargs="?", help="flash: the app .bin (build/<project>.bin)")
    parser.add_argument("--device", action="append", default=[], metavar="MAC",
                        help="only this device (repeatable); flash/list default to all")
    parser.add_argument("--peer", action="append", default=None, metavar="HOST",
                        help="a device's address, for links with no multicast (repeatable)")
    parser.add_argument("--key", default=os.environ.get("RAMMP_OTA_KEY", DEFAULT_KEY),
                        help="the update key (CONFIG_HMI_OTA_AUTH_KEY); default $RAMMP_OTA_KEY")
    parser.add_argument("--no-compress", action="store_true",
                        help="send the image as is, even to a device that takes zlib")
    parser.add_argument("--discover", type=float, default=5.0, metavar="S",
                        help="seconds to listen for devices (default 5)")
    parser.add_argument("--advertised-address", default=None)
    parser.add_argument("--domain-id", type=int, default=0)
    parser.add_argument("--verbose", action="store_true", help="show the RTPS harness log")
    cli = parser.parse_args()

    image = None
    if cli.command == "flash":
        if not cli.image:
            parser.error("flash needs the image")
        image = Image(cli.image)
        _import_espp_ota()  # fail before touching any device
        print(f"image {image.path}: {image.project} {image.version}, {len(image.data)} B, "
              f"sha256 {image.sha256[:16]}...")
    if cli.key == DEFAULT_KEY and cli.command in ("flash", "abort"):
        print("using the public bench key (set --key or RAMMP_OTA_KEY for fielded devices)")
    if not cli.verbose:
        rtps_host.log = lambda message: None

    peers = cli.peer or [p for p in [rtps_net.load_config().get("peer")] if p]
    host = OtaHost(harness_args(cli, peers))
    network = threading.Thread(target=host.run, daemon=True)
    network.start()
    try:
        wanted = {mac.lower() for mac in cli.device}
        deadline = time.monotonic() + cli.discover
        while time.monotonic() < deadline:
            if wanted and wanted <= set(host.snapshot()):
                break
            time.sleep(0.2)
        devices = {mac: info for mac, info in host.snapshot().items()
                   if not wanted or mac in wanted}
        for mac in sorted(wanted - set(devices)):
            print(f"{mac}: not found")
        if not devices:
            print("no devices found" + ("" if peers else " (try --peer <ip>)"))
            return EXIT_NO_DEVICE
        if cli.command == "list":
            for info in sorted(devices.values(), key=lambda i: i.ip):
                print(describe(info))
            return EXIT_OK
        if cli.command == "console":
            if len(devices) > 1:
                print("several devices: pick one with --device")
                return EXIT_FAIL
            host.stop()
            return console(next(iter(devices.values())).ip)

        print_lock = threading.Lock()
        results: Dict[str, bool] = {}

        def run(mac: str) -> None:
            def say(message: str) -> None:
                with print_lock:
                    print(f"[{mac}] {message}", flush=True)
            say(describe(devices[mac]))
            key = cli.key.encode("utf-8")
            if cli.command == "flash":
                results[mac] = flash_one(host, mac, image, key, say, not cli.no_compress)
            else:
                results[mac] = abort_one(host, mac, key, say)

        workers = [threading.Thread(target=run, args=(mac,)) for mac in sorted(devices)]
        for worker in workers:
            worker.start()
        for worker in workers:
            worker.join()
        failed = sorted(mac for mac, ok in results.items() if not ok)
        print(f"\n{len(results) - len(failed)} of {len(results)} ok"
              + (f"; failed: {', '.join(failed)}" if failed else ""))
        return EXIT_FAIL if failed or len(results) < len(devices) else EXIT_OK
    finally:
        host.stop()
        network.join(timeout=2.0)


if __name__ == "__main__":
    sys.exit(main())
