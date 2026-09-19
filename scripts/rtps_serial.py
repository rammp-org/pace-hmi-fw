#!/usr/bin/env python3
"""The HMI's serial console, over RTPS, as a serial port on this PC.

  python rtps_serial.py                 # serves localhost:4000 (RFC 2217) and :4001 (raw)
      idf.py -p rfc2217://localhost:4000 monitor
      idf.py -p socket://localhost:4001 flash      (the app goes over as an OTA update)
  python rtps_serial.py --com auto      # a COM port Windows lists (needs com0com)
  python rtps_serial.py --term          # a terminal right here (Ctrl-] leaves)

RFC 2217 carries DTR/RTS (so --allow-reset works) and anything pyserial opens
takes it; raw TCP carries bytes only, and is what to flash through: esptool
renegotiates an RFC 2217 port's settings around every block it sends (~0.2 s
each), which makes a 5 MB app take 20 s there and 1 s over raw TCP.

The protocol is "Serial over RTPS" in main/hmi_rtps_spec.hpp. What reaches the
port is what the board's USB port shows: the lines the device still holds when a
program opens the port, then everything it prints. What is typed goes back, and
the device echoes it; a line is a console command ('help').

A COM port needs a virtual null-modem driver, since Windows has none of its own:
com0com (a signed build is 2.2.2.0) makes pairs such as COM20 <-> COM21. This
bridge opens one end and anything else opens the other. With --com auto it picks
a com0com pair and says which end to use.

Flashing: esptool gets an ESP32-P4 ROM loader and flasher stub played by this
bridge, so `idf.py flash` (or esptool write-flash) runs as usual against the
port. What it writes stays here; once it is done, the app image goes to the
device with the OTA update of rtps_ota.py (signed with --key), and the device
reboots into it. The bootloader, partition table and otadata are not sent: they
are the device's own, and this only ever replaces the app.

Resets: the device is not rebooted when a program opens the port or toggles
RTS, unless --allow-reset (then an RTS pulse reboots it, as on USB; the device
refuses while the chair drives).

Needs pyserial for anything but --term (the ESP-IDF Python environment has it).
Exit code: 0 on a clean stop, 2 if no device answered.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import random
import socket
import struct
import sys
import threading
import time
import zlib
from typing import Callable, Dict, List, Optional, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import rammp_rtps as spec  # noqa: E402  (path setup must run first)
import rtps_host  # noqa: E402
import rtps_net  # noqa: E402
import rtps_ota  # noqa: E402

PARTICIPANT_ID = 17  # clear of the other tools (10-16) and rtps_net's probe (19)
EXIT_OK, EXIT_NO_DEVICE = 0, 2

HELLO_S = spec.SERIAL_HELLO_PERIOD_MS / 1000.0  # noqa: F821  (scraped)
CHUNK = spec.SERIAL_CHUNK  # noqa: F821
#: a device unheard this long is rebooting or gone: discovery with it starts over
GONE_S = 4.0
#: how long to hold live output back while the history it follows is on its way
HISTORY_WAIT_S = 3.0
#: esptool silent this long is done (it compresses each file before sending it:
#: seconds, for the app)
LOADER_IDLE_S = 20.0


def note(text: str) -> bytes:
    """A line of the bridge's own, as it appears in the port's stream."""
    return f"\r\n[rtps_serial] {text}\r\n".encode()


# ---------------------------------------------------------------- the RTPS side


class SerialLink(rtps_ota.OtaHost):
    """One device's serial over RTPS, and its OTA commands (for flashing).

    Readers: OtaDeviceInfo (who is out there) and kSerialTx. Writers: OtaCommand,
    first, as OtaHost sends it, then kSerialRx.
    """

    def __init__(self, args: argparse.Namespace, want_mac: Optional[str],
                 on_output: Callable[[bytes], None], say: Callable[[str], None]) -> None:
        super().__init__(args)
        self.local_readers.append(rtps_host.ReaderConfig(
            topic_name=spec.TOPIC_SERIAL_TX, type_name=spec.TYPE_SERIAL_DATA, reliable=False,
            entity_index=len(self.local_readers)))
        self.serial_writer = rtps_host.WriterConfig(
            topic_name=spec.TOPIC_SERIAL_RX, type_name=spec.TYPE_SERIAL_DATA, reliable=False,
            entity_index=len(self.local_writers))
        self.local_writers.append(self.serial_writer)
        self.on_output = on_output
        self.say = say
        self.mac = want_mac  # the device, once chosen
        self.lock = threading.Lock()
        self.session = 0
        self.tx_seq = 0
        self.device_session: Optional[int] = None
        self.expected_seq = 0
        self.waiting_history_since: Optional[float] = None
        self.held: List[bytes] = []
        self.lost = 0
        self.attached = False  # some port is open: the device is asked to send
        self.new_session()

    # -- what arrives
    def handle_user_packet(self, packet: bytes, sender_ip: str, sender_port: int) -> None:
        for guid_prefix, writer_id, payload, reader_id in rtps_host.parse_rtps_data_messages(packet):
            topic = self.topic_for_sample(guid_prefix, writer_id, reader_id)
            if topic == spec.TOPIC_OTA_DEVICE_INFO:
                info = spec.unpack_ota_device_info(payload)
                if info is not None and info.mac:
                    with self.changed:
                        self.devices[info.mac] = info
                        self.heard_at[info.mac] = time.monotonic()
                        self.changed.notify_all()
            elif topic == spec.TOPIC_SERIAL_TX:
                message = spec.unpack_serial_data(payload)
                if message is not None and self.mac and message.mac == self.mac:
                    self.heard_at[self.mac] = time.monotonic()
                    self._on_serial(message)

    def _on_serial(self, m: spec.SerialData) -> None:
        out: List[bytes] = []
        with self.lock:
            if m.session != self.device_session:
                if self.device_session is not None:
                    out.append(note("the device restarted"))
                self.device_session = m.session
                self.expected_seq = m.seq
                self.waiting_history_since = time.monotonic()
            if m.seq < self.expected_seq:
                return  # a duplicate, or too late to place
            if m.seq > self.expected_seq:
                self.lost += m.seq - self.expected_seq
                out.append(note(f"{m.seq - self.expected_seq} messages lost"))
            self.expected_seq = m.seq + 1
            if m.kind == spec.SERIAL_KIND_HISTORY:  # noqa: F821
                if m.to != self.session:
                    return  # another host's
                if m.data:
                    out.append(m.data)
                else:  # the end of it: what was held back follows
                    out.extend(self.held)
                    self.held = []
                    self.waiting_history_since = None
            elif m.kind == spec.SERIAL_KIND_DATA:  # noqa: F821
                if self.waiting_history_since is not None:
                    self.held.append(m.data)
                else:
                    out.append(m.data)
        for data in out:
            self.on_output(data)

    # -- what goes out
    def _send(self, kind: int, data: bytes = b"", session: Optional[int] = None) -> None:
        with self.lock:
            self.tx_seq += 1
            message = spec.SerialData(session or self.session, self.tx_seq, kind, 0,
                                      self.mac or "", data)
        payload = self.build_data_message(self.serial_writer, spec.pack_serial_data(message))
        for target in self._build_user_targets(self.serial_writer):
            self.send_user_datagram(payload, target)

    def new_session(self) -> None:
        """Start over as a new host: the device sends what it holds again."""
        old = self.session
        with self.lock:
            self.session = random.randint(1, 0xFFFFFFFF)
            self.waiting_history_since = time.monotonic()
            self.held = []
        if old and self.mac:
            self._send(spec.SERIAL_KIND_BYE, session=old)  # noqa: F821
        if self.mac and self.attached:
            self._send(spec.SERIAL_KIND_HELLO)  # noqa: F821

    def type(self, data: bytes) -> None:
        for at in range(0, len(data), CHUNK):
            self._send(spec.SERIAL_KIND_DATA, data[at:at + CHUNK])  # noqa: F821

    def reset(self) -> None:
        self._send(spec.SERIAL_KIND_RESET)  # noqa: F821

    def bye(self) -> None:
        if self.mac:
            self._send(spec.SERIAL_KIND_BYE)  # noqa: F821

    # -- keeping it attached
    def tend(self) -> None:
        """Every HELLO_S: stay attached, notice a silent device, release held output."""
        if not self.mac:
            return
        if self.attached:
            self._send(spec.SERIAL_KIND_HELLO)  # noqa: F821
        info = self.devices.get(self.mac)
        if info is not None and time.monotonic() - self.heard_at.get(self.mac, 0.0) > GONE_S:
            self.forget(info.ip)  # rebooting: discovery with it has to start over
        flush: List[bytes] = []
        with self.lock:
            since = self.waiting_history_since
            if since is not None and time.monotonic() - since > HISTORY_WAIT_S:
                flush, self.held = self.held, []
                self.waiting_history_since = None
        for data in flush:
            self.on_output(data)


# ---------------------------------------------------------------- esptool's side


def slip(packet: bytes) -> bytes:
    return b"\xc0" + packet.replace(b"\xdb", b"\xdb\xdd").replace(b"\xc0", b"\xdb\xdc") + b"\xc0"


class SlipReader:
    """Frames out of a byte stream; bytes outside a frame come back as `outside`."""

    def __init__(self) -> None:
        self.frame: Optional[bytearray] = None
        self.escaped = False

    def feed(self, data: bytes) -> Tuple[List[bytes], bytes]:
        frames: List[bytes] = []
        outside = bytearray()
        for b in data:
            if self.frame is None:
                if b == 0xC0:
                    self.frame = bytearray()
                else:
                    outside.append(b)
            elif self.escaped:
                self.escaped = False
                self.frame.append({0xDC: 0xC0, 0xDD: 0xDB}.get(b, b))
            elif b == 0xDB:
                self.escaped = True
            elif b == 0xC0:
                if self.frame:
                    frames.append(bytes(self.frame))
                    self.frame = None
                # else: back-to-back delimiters, still at a frame start
            else:
                self.frame.append(b)
        return frames, bytes(outside)


def esp_image_length(data: bytes) -> Optional[int]:
    """Length of the ESP-IDF image at the start of `data` (None if it is not one):
    the header, the segments, the checksum padded to 16, and the SHA-256 if appended."""
    if len(data) < 24 or data[0] != 0xE9:
        return None
    count = data[1]
    pos = 24
    for _ in range(count):
        if pos + 8 > len(data):
            return None
        _addr, length = struct.unpack_from("<II", data, pos)
        pos += 8 + length
    pos += 16 - pos % 16  # the checksum byte ends a 16-byte block
    if data[23]:  # hash_appended
        pos += 32
    return pos if pos <= len(data) else None


def app_partitions(table: bytes) -> List[int]:
    """Offsets of the app partitions in an ESP partition table (0xAA50 entries)."""
    offsets = []
    for at in range(0, len(table) - 31, 32):
        magic, ptype, _sub, offset, _size = struct.unpack_from("<HBBII", table, at)
        if magic != 0x50AA:
            break
        if ptype == 0x00:
            offsets.append(offset)
    return offsets


class LoaderEmulator:
    """esptool's counterpart: the ESP32-P4 ROM loader, then its flasher stub.

    Writes are kept in `flash` (offset -> bytes); nothing reaches a device. Reads of
    registers answer as a v1.3 chip with this device's MAC and a 16 MB flash would.
    """

    CHIP_ID = 18  # ESP32-P4
    EFUSE_BLOCK1 = 0x5012D044
    SPI_BASE = 0x5008D000
    SPI_CMD_USR = 1 << 18
    FLASH_ID = 0x1840C8  # GigaDevice, 16 MB

    OP_NAMES = {0x02: "FLASH_BEGIN", 0x03: "FLASH_DATA", 0x04: "FLASH_END", 0x05: "MEM_BEGIN",
                0x06: "MEM_END", 0x07: "MEM_DATA", 0x08: "SYNC", 0x09: "WRITE_REG",
                0x0A: "READ_REG", 0x0B: "SPI_SET_PARAMS", 0x0D: "SPI_ATTACH",
                0x0F: "CHANGE_BAUDRATE", 0x10: "FLASH_DEFL_BEGIN", 0x11: "FLASH_DEFL_DATA",
                0x12: "FLASH_DEFL_END", 0x13: "SPI_FLASH_MD5", 0x14: "GET_SECURITY_INFO",
                0xD3: "RUN_USER_CODE"}

    def __init__(self, mac: str, send: Callable[[bytes], None]) -> None:
        self.send = send
        self.stub = False
        self.flash: Dict[int, bytearray] = {}
        self.regs: Dict[int, int] = {}
        mac_bytes = bytes.fromhex(mac.replace(":", "")) if mac else b"\0" * 6
        self.regs[self.EFUSE_BLOCK1] = struct.unpack(">I", mac_bytes[2:])[0]
        self.regs[self.EFUSE_BLOCK1 + 4] = struct.unpack(">H", mac_bytes[:2])[0]
        self.regs[self.EFUSE_BLOCK1 + 8] = (1 << 4) | 3  # chip v1.3, package 0
        self.write_at: Optional[int] = None  # FLASH_*: where the next block goes
        self.block_size = 0
        self.inflate: Optional["zlib._Decompress"] = None
        self.finished = False  # esptool said it is done (FLASH_END with reboot, ...)
        self.last_command = time.monotonic()

    # -- the wire
    def reply(self, op: int, value: int = 0, data: bytes = b"", status: int = 0,
              error: int = 0) -> None:
        body = data + bytes([status, error]) + (b"" if self.stub else b"\0\0")
        self.send(slip(struct.pack("<BBHI", 1, op, len(body), value) + body))

    def handle(self, frame: bytes) -> None:
        if len(frame) < 8 or frame[0] != 0:
            return
        _, op, length, _checksum = struct.unpack_from("<BBHI", frame)
        data = frame[8:8 + length]
        self.last_command = time.monotonic()
        method = getattr(self, "op_" + self.OP_NAMES.get(op, "").lower(), None)
        if method is None:
            self.reply(op, status=1, error=0x05)  # esptool: UnsupportedCommandError
            return
        method(op, data)

    # -- the flash, as written
    def write(self, offset: int, data: bytes) -> None:
        for start, region in self.flash.items():
            if start <= offset <= start + len(region):
                end = offset - start + len(data)
                if end > len(region):
                    region.extend(b"\xff" * (end - len(region)))
                region[offset - start:end] = data
                return
        self.flash[offset] = bytearray(data)

    def read(self, offset: int, size: int) -> bytes:
        out = bytearray(b"\xff" * size)
        for start, region in self.flash.items():
            lo, hi = max(offset, start), min(offset + size, start + len(region))
            if lo < hi:
                out[lo - offset:hi - offset] = region[lo - start:hi - start]
        return bytes(out)

    def app_image(self) -> Optional[Tuple[int, bytes]]:
        """(offset, image) of the app esptool wrote, if it wrote one."""
        apps = app_partitions(self.read(0x8000, 0xC00)) if 0x8000 in self.flash else []
        for offset in sorted(self.flash):
            if apps and offset not in apps:
                continue
            data = bytes(self.flash[offset])
            length = esp_image_length(data)
            if length is None or data[32:36] != struct.pack("<I", rtps_ota.APP_DESC_MAGIC):
                continue
            if data[length:].strip(b"\xff"):
                return offset, data  # something after the image (a signature): keep it all
            return offset, data[:length]  # esptool's padding off
        return None

    # -- the commands
    def op_sync(self, op: int, data: bytes) -> None:
        # The ROM answers eight times; the stub's value is 0, the ROM's is not.
        for _ in range(8):
            self.reply(op, value=0 if self.stub else 0x20120707)

    def op_get_security_info(self, op: int, data: bytes) -> None:
        self.reply(op, data=struct.pack("<IBBBBBBBBII", 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                        self.CHIP_ID, 0))

    def op_read_reg(self, op: int, data: bytes) -> None:
        (addr,) = struct.unpack_from("<I", data)
        self.reply(op, value=self.regs.get(addr, 0))

    def op_write_reg(self, op: int, data: bytes) -> None:
        for at in range(0, len(data) - 15, 16):
            addr, value, mask, _delay = struct.unpack_from("<IIII", data, at)
            self.regs[addr] = (self.regs.get(addr, 0) & ~mask) | (value & mask)
            if addr == self.SPI_BASE and value & self.SPI_CMD_USR:
                # a user SPI command: RDID reads the flash ID, anything else reads 0
                command = self.regs.get(self.SPI_BASE + 0x20, 0) & 0xFF
                self.regs[self.SPI_BASE + 0x58] = self.FLASH_ID if command == 0x9F else 0
                self.regs[self.SPI_BASE] = 0  # done at once
        self.reply(op)

    def op_mem_begin(self, op: int, data: bytes) -> None:
        self.reply(op)

    def op_mem_data(self, op: int, data: bytes) -> None:
        self.reply(op)

    def op_mem_end(self, op: int, data: bytes) -> None:
        self.reply(op)
        _no_entry, entry = struct.unpack_from("<II", data)
        if entry:  # the stub, starting: it says hello
            self.stub = True
            self.send(slip(b"OHAI"))

    def op_spi_attach(self, op: int, data: bytes) -> None:
        self.reply(op)

    def op_spi_set_params(self, op: int, data: bytes) -> None:
        self.reply(op)

    def op_change_baudrate(self, op: int, data: bytes) -> None:
        self.reply(op)

    def op_flash_begin(self, op: int, data: bytes) -> None:
        _size, _blocks, self.block_size, self.write_at = struct.unpack_from("<IIII", data)
        self.inflate = None
        self.reply(op)

    def op_flash_data(self, op: int, data: bytes) -> None:
        length, seq, _, _ = struct.unpack_from("<IIII", data)
        if self.write_at is None:
            self.reply(op, status=1, error=0x06)
            return
        self.write(self.write_at + seq * self.block_size, data[16:16 + length])
        self.reply(op)

    def op_flash_defl_begin(self, op: int, data: bytes) -> None:
        _size, _blocks, self.block_size, self.write_at = struct.unpack_from("<IIII", data)
        self.inflate = zlib.decompressobj()
        self.reply(op)

    def op_flash_defl_data(self, op: int, data: bytes) -> None:
        length = struct.unpack_from("<I", data)[0]
        if self.write_at is None or self.inflate is None:
            self.reply(op, status=1, error=0x06)
            return
        try:
            out = self.inflate.decompress(data[16:16 + length])
        except zlib.error:
            self.reply(op, status=1, error=0x0B)
            return
        self.write(self.write_at, out)
        self.write_at += len(out)
        self.reply(op)

    def _flash_end(self, op: int, data: bytes) -> None:
        self.reply(op)
        if data and struct.unpack_from("<I", data)[0] == 0:  # 0 = reboot
            self.finished = True

    def op_flash_end(self, op: int, data: bytes) -> None:
        self._flash_end(op, data)

    def op_flash_defl_end(self, op: int, data: bytes) -> None:
        self._flash_end(op, data)

    def op_spi_flash_md5(self, op: int, data: bytes) -> None:
        addr, size, _, _ = struct.unpack_from("<IIII", data)
        digest = hashlib.md5(self.read(addr, size))
        self.reply(op, data=digest.digest() if self.stub else digest.hexdigest().encode())

    def op_run_user_code(self, op: int, data: bytes) -> None:
        self.finished = True  # the stub does not answer this one


# ---------------------------------------------------------------- the ports


class FakeSerial:
    """What serial.rfc2217.PortManager drives: a port with no wire behind it."""

    def __init__(self, on_lines: Callable[[bool, bool], None]) -> None:
        self._on_lines = on_lines
        self._dtr = self._rts = False
        self.baudrate, self.bytesize, self.parity, self.stopbits = 115200, 8, "N", 1
        self.xonxoff = self.rtscts = self.break_condition = False
        self.cts = self.dsr = self.cd = True
        self.ri = False
        self.name = "rtps"

    @property
    def dtr(self) -> bool:
        return self._dtr

    @dtr.setter
    def dtr(self, value: bool) -> None:
        self._dtr = bool(value)
        self._on_lines(self._dtr, self._rts)

    @property
    def rts(self) -> bool:
        return self._rts

    @rts.setter
    def rts(self, value: bool) -> None:
        self._rts = bool(value)
        self._on_lines(self._dtr, self._rts)

    def reset_input_buffer(self) -> None:
        pass

    def reset_output_buffer(self) -> None:
        pass


class End:
    """One way in for programs on this PC. The Bridge calls write(); the end calls
    bridge.opened() / typed() / lines() / closed()."""

    name = "?"

    def write(self, data: bytes) -> None:
        raise NotImplementedError


class TcpEnd(End):
    """A TCP server, one client at a time: raw bytes, or RFC 2217 (telnet with
    serial-port control, which carries the baud rate and DTR/RTS)."""

    def __init__(self, bridge: "Bridge", port: int, rfc2217: bool, bind: str) -> None:
        self.bridge, self.port, self.rfc2217, self.bind = bridge, port, rfc2217, bind
        self.name = f"{'rfc2217' if rfc2217 else 'socket'}://localhost:{port}"
        self.client: Optional[socket.socket] = None
        self.manager = None
        self.send_lock = threading.Lock()
        if rfc2217:
            import serial.rfc2217  # noqa: F401  (fail at start, not at the first client)
        self.listener = socket.create_server((bind, port), reuse_port=False)
        threading.Thread(target=self._serve, daemon=True, name=self.name).start()

    def write(self, data: bytes) -> None:
        client = self.client
        if client is None:
            return
        if self.manager is not None:
            data = b"".join(self.manager.escape(data))
        try:
            with self.send_lock:
                client.sendall(data)
        except OSError:
            pass

    def _serve(self) -> None:
        while True:
            client, _peer = self.listener.accept()
            client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            if self.client is not None:  # one at a time: the newest wins
                try:
                    self.client.close()
                except OSError:
                    pass
            self.client = client
            threading.Thread(target=self._session, args=(client,), daemon=True).start()

    def _session(self, client: socket.socket) -> None:
        manager = None
        if self.rfc2217:
            import serial.rfc2217

            class Connection:
                def write(_self, data: bytes) -> None:  # noqa: N805
                    with self.send_lock:
                        client.sendall(data)

            manager = serial.rfc2217.PortManager(
                FakeSerial(lambda dtr, rts: self.bridge.lines(self, dtr, rts)), Connection())
        self.manager = manager
        self.bridge.opened(self)
        try:
            while True:
                data = client.recv(65536)
                if not data:
                    break
                if manager is not None:
                    data = b"".join(manager.filter(data))
                if data:
                    self.bridge.typed(self, data)
        except OSError:
            pass
        finally:
            if self.client is client:
                self.client = None
                self.manager = None
            try:
                client.close()
            except OSError:
                pass
            self.bridge.closed(self)


def com0com_pairs() -> List[Tuple[str, str]]:
    """(this bridge's end, the end to open) for each com0com pair, e.g. (COM21, COM20)."""
    from serial.tools import list_ports

    ends: Dict[str, Dict[str, str]] = {}
    for port in list_ports.comports():
        hwid = (port.hwid or "").upper()
        if "COM0COM" not in hwid and "com0com" not in (port.description or ""):
            continue
        # hwid ends in the port's own name: CNCA0 / CNCB0
        for token in hwid.replace("\\", " ").split():
            if token.startswith(("CNCA", "CNCB")):
                ends.setdefault(token[4:], {})[token[3]] = port.device
    return [(pair["B"], pair["A"]) for pair in ends.values() if "A" in pair and "B" in pair]


class ComEnd(End):
    """Our end of a virtual null-modem pair: the other end is the COM port programs
    open. com0com carries the other side's RTS to our CTS and its DTR to our DSR."""

    def __init__(self, bridge: "Bridge", device: str) -> None:
        import serial

        self.bridge = bridge
        self.name = device
        self.port = serial.Serial(device, 115200, timeout=0.02)
        self.lines_seen = (self.port.dsr, self.port.cts)
        threading.Thread(target=self._pump, daemon=True, name=device).start()

    def write(self, data: bytes) -> None:
        try:
            self.port.write(data)
        except Exception:  # noqa: BLE001  (the other end may be closed: output is lost, as on a wire)
            pass

    def _pump(self) -> None:
        # Always open, as a wire is: what arrives while nothing holds the other end is
        # lost there. A program opening it mostly raises DTR or RTS; that asks the
        # device for what it holds again (idf.py monitor lowers both, so it gets the
        # live output only).
        self.bridge.opened(self)
        while True:
            data = self.port.read(65536)
            if data:
                self.bridge.typed(self, data)
            lines = (self.port.dsr, self.port.cts)
            if lines != self.lines_seen:
                if not any(self.lines_seen) and self.bridge.link is not None:
                    self.bridge.link.new_session()
                self.lines_seen = lines
                self.bridge.lines(self, *lines)


class TerminalEnd(End):
    """This console: keys go out as typed (the device echoes), Ctrl-] leaves."""

    name = "terminal"

    def __init__(self, bridge: "Bridge") -> None:
        self.bridge = bridge
        if os.name == "nt":
            os.system("")  # turns on the console's VT mode: the device's colours
        threading.Thread(target=self._keys, daemon=True, name="terminal").start()
        bridge.opened(self)

    def write(self, data: bytes) -> None:
        sys.stdout.buffer.write(data)
        sys.stdout.flush()

    def _keys(self) -> None:
        if os.name == "nt":
            import msvcrt

            while True:
                key = msvcrt.getwch()
                if key == "\x1d":  # Ctrl-]
                    self.bridge.stop.set()
                    return
                if key in ("\x00", "\xe0"):  # a function key: its second half, dropped
                    msvcrt.getwch()
                    continue
                self.bridge.typed(self, key.encode("utf-8", "ignore"))
        for line in sys.stdin:
            self.bridge.typed(self, line.rstrip("\n").encode() + b"\r")
        self.bridge.stop.set()


# ---------------------------------------------------------------- the bridge


class Bridge:
    """Between the ends and the link: routes bytes, plays esptool's loader, flashes."""

    def __init__(self, cli: argparse.Namespace) -> None:
        self.cli = cli
        self.stop = threading.Event()
        self.ends: List[End] = []
        self.lock = threading.Lock()
        self.loader: Optional[LoaderEmulator] = None
        self.loader_end: Optional[End] = None
        self.slip = SlipReader()
        self.pending = bytearray()  # a frame start held back until it is known
        self.pending_since = 0.0
        self.lines_state: Dict[End, Tuple[bool, bool]] = {}
        self.open_ends: set = set()
        self.flashing = threading.Event()
        self.link: Optional[SerialLink] = None

    def say(self, text: str) -> None:
        print(f"[rtps_serial] {text}", flush=True)

    # -- from the device
    def output(self, data: bytes) -> None:
        if self.loader is not None:
            return  # esptool is talking to us: the device's output would only confuse it
        for end in self.ends:
            end.write(data)

    def announce(self, text: str) -> None:
        """A note of our own, on the console and in the stream."""
        self.say(text)
        if self.loader is None:
            for end in self.ends:
                if not isinstance(end, TerminalEnd):
                    end.write(note(text))

    # -- from the ends
    def opened(self, end: End) -> None:
        if not isinstance(end, TerminalEnd):
            self.say(f"{end.name}: opened")
        self.open_ends.add(end)
        if self.link is not None:
            self.link.attached = True
            self.link.new_session()  # replays what the device holds, as the TCP console does

    def closed(self, end: End) -> None:
        self.say(f"{end.name}: closed")
        self.open_ends.discard(end)
        with self.lock:
            if self.loader_end is end:
                self._loader_done("the port closed")
        if self.link is not None and not self.open_ends:
            self.link.attached = False
            self.link.bye()

    def typed(self, end: End, data: bytes) -> None:
        with self.lock:
            text = bytearray()
            for i, b in enumerate(data):
                if self.loader is not None:
                    if self.loader_end is end:
                        self._to_loader(data[i:])
                    break  # (another end typing while esptool flashes: dropped)
                # Text goes to the device as typed, but a SLIP frame start (0xC0,
                # which text never has) is held until the frame is complete, to see
                # whether it is esptool asking for the loader.
                if self.pending or b == 0xC0:
                    if not self.pending:
                        self._type(bytes(text))
                        text.clear()
                        self.pending_since = time.monotonic()
                    self.pending.append(b)
                    if b == 0xC0 and len(self.pending) > 1:
                        self._frame_complete(end)
                else:
                    text.append(b)
            self._type(bytes(text))

    def _to_loader(self, data: bytes) -> None:
        frames, _ = self.slip.feed(data)
        for frame in frames:
            self.loader.handle(frame)
        if self.loader.finished:
            self._loader_done("esptool finished")

    def _type(self, data: bytes) -> None:
        if data and self.link is not None and self.link.mac:
            self.link.type(data)

    def _frame_complete(self, end: End) -> None:
        frames, _ = SlipReader().feed(bytes(self.pending))
        held, self.pending = bytes(self.pending), bytearray()
        if frames and len(frames[0]) >= 8 and frames[0][0] == 0 and frames[0][1] == 0x08:
            if self.flashing.is_set():
                return  # still updating from the last flash: esptool will time out
            mac = self.link.mac if self.link else ""
            self.say(f"{end.name}: esptool connected, playing the loader")
            self.loader = LoaderEmulator(mac or "", end.write)
            self.loader_end = end
            self.slip = SlipReader()
            self.loader.handle(frames[0])
        else:
            self._type(held)

    def lines(self, end: End, dtr: bool, rts: bool) -> None:
        before = self.lines_state.get(end, (False, False))
        self.lines_state[end] = (dtr, rts)
        if not (before[1] and not rts):
            return
        # RTS released: EN rises. With DTR held (IO0 low) that is esptool's way into
        # the loader; without, a reset.
        if dtr:
            return
        with self.lock:
            if self.loader is not None and self.loader_end is end:
                self._loader_done("esptool reset the chip")
                return
        if self.cli.allow_reset and self.link is not None and self.link.mac:
            self.announce("RTS pulse: rebooting the device")
            self.link.reset()

    def tend(self) -> None:
        """Every ~0.2 s: time out a held frame start and a quiet esptool."""
        with self.lock:
            if self.pending and time.monotonic() - self.pending_since > 0.5:
                held, self.pending = bytes(self.pending), bytearray()
                self._type(held)
            if (self.loader is not None
                    and time.monotonic() - self.loader.last_command > LOADER_IDLE_S):
                self._loader_done("esptool went quiet")

    # -- flashing
    def _loader_done(self, why: str) -> None:
        loader, self.loader, self.loader_end = self.loader, None, None
        if loader is None:
            return
        found = loader.app_image()
        written = ", ".join(f"{len(v)} B at {k:#x}" for k, v in sorted(loader.flash.items()))
        self.say(f"loader: {why}" + (f"; written: {written}" if written else ""))
        if found is None:
            if loader.flash:
                self.announce("no app image among what esptool wrote: nothing to update")
            return
        offset, image = found
        others = [k for k in loader.flash if k != offset]
        if others:
            self.say("not sent (the device keeps its own): "
                     + ", ".join(f"{k:#x}" for k in sorted(others)))
        self.flashing.set()
        threading.Thread(target=self._update, args=(image,), daemon=True, name="ota").start()

    def _update(self, data: bytes) -> None:
        try:
            link = self.link
            if link is None or not link.mac or link.mac not in link.devices:
                self.announce("no device to update")
                return
            image = rtps_ota.Image("esptool image", data)
            self.announce(f"updating {link.mac} to {image.project} {image.version} "
                          f"({len(data)} B) over Ethernet")
            ok = rtps_ota.flash_one(link, link.mac, image, self.cli.key.encode("utf-8"),
                                    self.announce, not self.cli.no_compress)
            self.announce("update done" if ok else "update FAILED")
        except SystemExit as exc:  # Image() refuses what is not an app
            self.announce(f"update FAILED: {exc}")
        except Exception as exc:  # noqa: BLE001  (the bridge stays up)
            self.announce(f"update FAILED: {exc!r}")
        finally:
            self.flashing.clear()


def harness_args(cli: argparse.Namespace, peers: List[str]) -> argparse.Namespace:
    args = rtps_ota.harness_args(cli, peers)
    args.node_name = "serial_host"
    args.participant_id = PARTICIPANT_ID
    return args


def choose_device(link: SerialLink, wanted: Optional[str], discover: float,
                  stop: threading.Event) -> Optional[str]:
    deadline = time.monotonic() + discover
    told = False
    while not stop.is_set():
        found = link.snapshot()
        if wanted:
            if wanted in found:
                return wanted
        elif len(found) == 1:
            return next(iter(found))
        elif len(found) > 1 and time.monotonic() > deadline:
            for info in sorted(found.values(), key=lambda i: i.ip):
                print("  " + rtps_ota.describe(info))
            print("several devices: pick one with --device")
            return None
        if time.monotonic() > deadline and not told:
            told = True
            print(f"waiting for {wanted or 'a device'} (OtaDeviceInfo)..." +
                  ("" if link.peer_addresses else " (try --peer <ip>)"), flush=True)
        time.sleep(0.2)
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--rfc2217", type=int, default=4000, metavar="PORT",
                        help="RFC 2217 server port (default 4000; 0 = none)")
    parser.add_argument("--tcp", type=int, default=4001, metavar="PORT",
                        help="raw TCP server port, socket:// (default 4001; 0 = none)")
    parser.add_argument("--bind", default="127.0.0.1",
                        help="address the servers listen on (default this PC only)")
    parser.add_argument("--com", metavar="COMx|auto",
                        help="also this end of a com0com pair ('auto' finds one)")
    parser.add_argument("--term", action="store_true", help="a terminal in this console")
    parser.add_argument("--device", metavar="MAC", help="the device (default: the only one)")
    parser.add_argument("--peer", action="append", default=None, metavar="HOST",
                        help="a device's address, for links with no multicast (repeatable)")
    parser.add_argument("--allow-reset", action="store_true",
                        help="an RTS pulse reboots the device, as on USB")
    parser.add_argument("--key", default=os.environ.get("RAMMP_OTA_KEY", rtps_ota.DEFAULT_KEY),
                        help="the update key for flashing (default $RAMMP_OTA_KEY, or the bench key)")
    parser.add_argument("--no-compress", action="store_true", help="flash: send the image as is")
    parser.add_argument("--discover", type=float, default=5.0, metavar="S")
    parser.add_argument("--advertised-address", default=None)
    parser.add_argument("--domain-id", type=int, default=0)
    parser.add_argument("--verbose", action="store_true", help="show the RTPS harness log")
    cli = parser.parse_args()
    if not cli.verbose:
        rtps_host.log = lambda message: None

    bridge = Bridge(cli)
    peers = cli.peer or [p for p in [rtps_net.load_config().get("peer")] if p]
    link = SerialLink(harness_args(cli, peers), cli.device.lower() if cli.device else None,
                      bridge.output, bridge.say)
    network = threading.Thread(target=link.run, daemon=True, name="rtps")
    network.start()
    try:
        mac = choose_device(link, cli.device.lower() if cli.device else None, cli.discover,
                            bridge.stop)
        if mac is None:
            return EXIT_NO_DEVICE
        link.mac = mac
        bridge.link = link
        bridge.say(rtps_ota.describe(link.devices[mac]))

        if cli.rfc2217:
            bridge.ends.append(TcpEnd(bridge, cli.rfc2217, True, cli.bind))
        if cli.tcp:
            bridge.ends.append(TcpEnd(bridge, cli.tcp, False, cli.bind))
        if cli.com:
            device = cli.com
            if device.lower() == "auto":
                pairs = com0com_pairs()
                if not pairs:
                    print("no com0com pair found: install com0com, or use --rfc2217/--tcp")
                    return EXIT_NO_DEVICE
                device = pairs[0][0]
                bridge.say(f"open {pairs[0][1]} (the bridge holds {device})")
            bridge.ends.append(ComEnd(bridge, device))
        for end in bridge.ends:
            bridge.say(f"serving {end.name}")
        if cli.term:
            bridge.ends.append(TerminalEnd(bridge))

        next_hello = 0.0
        while not bridge.stop.is_set():
            now = time.monotonic()
            if now >= next_hello:
                link.tend()
                next_hello = now + HELLO_S
            bridge.tend()
            bridge.stop.wait(0.2)
        return EXIT_OK
    except KeyboardInterrupt:
        return EXIT_OK
    finally:
        link.bye()
        link.stop()
        network.join(timeout=2.0)


if __name__ == "__main__":
    sys.exit(main())
