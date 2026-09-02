#!/usr/bin/env python3
"""Desktop panel for driving the joystick HMI's status labels over RTPS.

The window equivalent of ``rtps_mcb_sim.py``: preset buttons for the states the
firmware knows, a raw spinbox for values it does not, a free-text override for
each label, and a cycle that walks the lot hands-free. It publishes
``rammp/mcb/status`` exactly as that script does — it imports the same publisher
— so anything learned here applies to the CLI tool and vice versa.

Usage:
  python rtps_mcb_gui.py
  python rtps_mcb_gui.py --peer 10.0.0.133 --advertised-address 100.92.133.114

Both settings are editable in the window; the flags only prefill them. The
"Via" dropdown lists this PC's IPv4 addresses, which matters on a machine with
VirtualBox/Tailscale/VPN adapters: RTPS leaves by exactly one interface, and the
automatic pick is often a virtual one.

tkinter only, no third-party dependency, matching the stdlib-only RTPS side.
"""

from __future__ import annotations

import argparse
import os
import queue
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import rammp_rtps as spec  # noqa: E402  (path setup must run first)
import rtps_host  # noqa: E402
import rtps_mcb_sim  # noqa: E402

LOG_MAX_LINES = 500
TICK_MS = 100


class McbPanel:
    """The window, plus the harness thread it starts and stops."""

    def __init__(self, root: tk.Tk, cli: argparse.Namespace) -> None:
        self.root = root
        self.cli = cli
        self.harness: rtps_mcb_sim.McbStatusPublisher | None = None
        self.thread: threading.Thread | None = None

        # The harness runs on its own thread and tkinter is not thread-safe, so
        # log lines cross over through a queue that _tick drains on the UI
        # thread. rtps_host.log calls this from the network thread.
        self.log_queue: queue.Queue[str] = queue.Queue()
        rtps_host.LOG_SINK = self.log_queue.put

        self.cycling = False
        self.cycle_job: str | None = None
        self.cycle_index = 0
        self._rate_seq = 0
        self._rate_time = time.monotonic()
        self._rate = 0.0

        root.title("RAMMP MCB simulator")
        self._build_connection()
        self._build_status_controls()
        self._build_cycle()
        self._build_log()
        root.after(TICK_MS, self._tick)
        root.protocol("WM_DELETE_WINDOW", self._on_close)

    # ---------------------------------------------------------------- widgets

    def _build_connection(self) -> None:
        frame = ttk.LabelFrame(self.root, text="Connection", padding=8)
        frame.pack(fill="x", padx=8, pady=(8, 4))

        ttk.Label(frame, text="Peer").grid(row=0, column=0, sticky="w")
        self.peer_var = tk.StringVar(value=(self.cli.peer[0] if self.cli.peer else ""))
        ttk.Entry(frame, textvariable=self.peer_var, width=18).grid(row=0, column=1, padx=(4, 12))

        ttk.Label(frame, text="Via").grid(row=0, column=2, sticky="w")
        addresses = rtps_mcb_sim.local_ipv4_addresses()
        self.address_var = tk.StringVar(
            value=self.cli.advertised_address or rtps_host.guess_local_ipv4()
        )
        ttk.Combobox(frame, textvariable=self.address_var, values=addresses, width=18).grid(
            row=0, column=3, padx=(4, 12)
        )

        self.connect_button = ttk.Button(frame, text="Connect", command=self._toggle_connection)
        self.connect_button.grid(row=0, column=4)

        self.connection_label = ttk.Label(frame, text="not connected")
        self.connection_label.grid(row=0, column=5, padx=(12, 0), sticky="w")

        ttk.Label(
            frame,
            text="Peer is only needed where multicast cannot reach the board "
                 "(Tailscale, VPN, another subnet).",
            foreground="#666666",
        ).grid(row=1, column=0, columnspan=6, sticky="w", pady=(6, 0))

    def _build_status_controls(self) -> None:
        self.drive_text_var = tk.StringVar()
        self.state_text_var = tk.StringVar()
        self.drive_raw_var = tk.StringVar(value=str(spec.DRIVE_STATUS_INACTIVE))
        self.state_raw_var = tk.StringVar(value=str(spec.STATE_OK))

        self._build_one_status(
            "Drive status", spec.DRIVE_STATUS_NAMES, self.drive_raw_var, self.drive_text_var,
            self._set_drive, self._set_drive_raw,
        )
        self._build_one_status(
            "State", spec.STATE_NAMES, self.state_raw_var, self.state_text_var,
            self._set_state, self._set_state_raw,
        )

    def _build_one_status(self, title, names, raw_var, text_var, on_preset, on_raw) -> None:
        frame = ttk.LabelFrame(self.root, text=title, padding=8)
        frame.pack(fill="x", padx=8, pady=4)

        column = 0
        # presets come from the spec's enum names, so a new enum value in the
        # header turns into a button here without touching this file
        for value in sorted(names):
            ttk.Button(
                frame, text=names[value], width=10,
                command=lambda v=value: on_preset(v),
            ).grid(row=0, column=column, padx=(0, 4))
            column += 1

        ttk.Label(frame, text="raw").grid(row=0, column=column, padx=(12, 4))
        ttk.Spinbox(frame, from_=0, to=255, width=5, textvariable=raw_var).grid(
            row=0, column=column + 1
        )
        ttk.Button(frame, text="Send", command=on_raw).grid(row=0, column=column + 2, padx=4)

        ttk.Label(frame, text="label").grid(row=1, column=0, sticky="w", pady=(8, 0))
        entry = ttk.Entry(frame, textvariable=text_var, width=20)
        entry.grid(row=1, column=1, columnspan=2, sticky="w", pady=(8, 0))
        entry.bind("<Return>", lambda _event: self._apply_overrides())
        ttk.Button(frame, text="Set", command=self._apply_overrides).grid(
            row=1, column=column, padx=(12, 4), pady=(8, 0)
        )
        ttk.Button(
            frame, text="Clear",
            command=lambda v=text_var: (v.set(""), self._apply_overrides()),
        ).grid(row=1, column=column + 1, pady=(8, 0))
        ttk.Label(
            frame, text=f"max {spec.MCB_TEXT_LEN - 1} chars, ASCII", foreground="#666666"
        ).grid(row=1, column=column + 2, padx=(8, 0), pady=(8, 0), sticky="w")

    def _build_cycle(self) -> None:
        frame = ttk.Frame(self.root, padding=(8, 4))
        frame.pack(fill="x")

        self.cycle_button = ttk.Button(frame, text="Start cycle", command=self._toggle_cycle)
        self.cycle_button.pack(side="left")

        ttk.Label(frame, text="dwell").pack(side="left", padx=(12, 4))
        self.dwell_var = tk.StringVar(value="3.0")
        ttk.Spinbox(frame, from_=0.5, to=30.0, increment=0.5, width=5,
                    textvariable=self.dwell_var).pack(side="left")

        self.status_label = ttk.Label(frame, text="idle")
        self.status_label.pack(side="right")

    def _build_log(self) -> None:
        frame = ttk.LabelFrame(self.root, text="Log", padding=4)
        frame.pack(fill="both", expand=True, padx=8, pady=(4, 8))
        self.log_text = tk.Text(frame, height=12, width=90, wrap="none", state="disabled")
        self.log_text.pack(side="left", fill="both", expand=True)
        scroll = ttk.Scrollbar(frame, command=self.log_text.yview)
        scroll.pack(side="right", fill="y")
        self.log_text.configure(yscrollcommand=scroll.set)

    # ------------------------------------------------------------ connection

    def _toggle_connection(self) -> None:
        if self.harness is None:
            self._connect()
        else:
            self._disconnect()

    def _connect(self) -> None:
        args = rtps_mcb_sim.build_harness_args(
            argparse.Namespace(
                node_name=self.cli.node_name,
                domain_id=self.cli.domain_id,
                participant_id=self.cli.participant_id,
                bind_address=None,
                advertised_address=self.address_var.get().strip() or None,
                multicast_interface=self.cli.multicast_interface,
                multicast_group=self.cli.multicast_group,
                period=self.cli.period,
                trace_packets=self.cli.trace_packets,
                peer=[self.peer_var.get().strip()] if self.peer_var.get().strip() else None,
                peer_participant_ids=self.cli.peer_participant_ids,
            )
        )
        try:
            self.harness = rtps_mcb_sim.McbStatusPublisher(args)
        except OSError as exc:
            # a bad address or a port already taken by another script
            self._append_log(f"[gui] connect failed: {exc}")
            self.connection_label.configure(text="connect failed")
            self.harness = None
            return
        self.thread = threading.Thread(target=self.harness.run, daemon=True)
        self.thread.start()
        self._apply_overrides()
        self.connect_button.configure(text="Disconnect")
        self.connection_label.configure(text=f"publishing as {args.advertised_address}")

    def _disconnect(self) -> None:
        self._stop_cycle()
        if self.harness is not None:
            self.harness.stop()  # run() returns and closes the sockets
        if self.thread is not None:
            self.thread.join(timeout=2.0)
        self.harness = None
        self.thread = None
        self.connect_button.configure(text="Connect")
        self.connection_label.configure(text="not connected")
        self.status_label.configure(text="idle")

    # -------------------------------------------------------------- sending

    def _publish(self) -> None:
        """Send immediately so the panel follows the click, not the next period.

        Shares the UDP socket with the harness thread's periodic publish, which
        is fine: sendto is atomic per datagram, and a torn seq counter is only a
        diagnostic.
        """
        if self.harness is None:
            self._append_log("[gui] not connected")
            return
        self.harness.publish_now()

    def _set_drive(self, value: int) -> None:
        self.drive_raw_var.set(str(value))
        self._set_drive_raw()

    def _set_state(self, value: int) -> None:
        self.state_raw_var.set(str(value))
        self._set_state_raw()

    def _set_drive_raw(self) -> None:
        if self.harness is None:
            self._append_log("[gui] not connected")
            return
        self.harness.drive_status = self._raw(self.drive_raw_var)
        self._publish()

    def _set_state_raw(self) -> None:
        if self.harness is None:
            self._append_log("[gui] not connected")
            return
        self.harness.system_state = self._raw(self.state_raw_var)
        self._publish()

    def _raw(self, var: tk.StringVar) -> int:
        try:
            return int(var.get()) & 0xFF
        except ValueError:
            self._append_log(f"[gui] '{var.get()}' is not a number; sending 0")
            return 0

    def _apply_overrides(self) -> None:
        if self.harness is None:
            return
        self.harness.drive_text = self.drive_text_var.get()
        self.harness.state_text = self.state_text_var.get()
        self._publish()

    # ---------------------------------------------------------------- cycle

    def _cycle_steps(self):
        """(drive, state, paused) triples: the four combinations, then a gap.

        The gap is longer than the HMI's staleness timeout, so one lap also
        exercises link loss and recovery.
        """
        return [
            (spec.DRIVE_STATUS_INACTIVE, spec.STATE_OK, False),
            (spec.DRIVE_STATUS_ACTIVE, spec.STATE_OK, False),
            (spec.DRIVE_STATUS_ACTIVE, spec.STATE_ERROR, False),
            (spec.DRIVE_STATUS_INACTIVE, spec.STATE_ERROR, False),
            (spec.DRIVE_STATUS_INACTIVE, spec.STATE_OK, True),
        ]

    def _toggle_cycle(self) -> None:
        if self.cycling:
            self._stop_cycle()
        elif self.harness is None:
            self._append_log("[gui] connect before starting the cycle")
        else:
            self.cycling = True
            self.cycle_index = 0
            self.cycle_button.configure(text="Stop cycle")
            self._cycle_step()

    def _stop_cycle(self) -> None:
        self.cycling = False
        if self.cycle_job is not None:
            self.root.after_cancel(self.cycle_job)
            self.cycle_job = None
        if self.harness is not None:
            self.harness.paused = False
        self.cycle_button.configure(text="Start cycle")

    def _cycle_step(self) -> None:
        # Driven by root.after rather than a thread, so it cannot race the
        # harness state the buttons also write.
        if not self.cycling or self.harness is None:
            return
        try:
            dwell = max(0.5, float(self.dwell_var.get()))
        except ValueError:
            dwell = 3.0
        drive, state, paused = self._cycle_steps()[self.cycle_index]
        self.cycle_index = (self.cycle_index + 1) % len(self._cycle_steps())

        self.harness.paused = paused
        if paused:
            delay = max(dwell, spec.MCB_STATUS_TIMEOUT_MS / 1000.0 + 1.0)
            self._append_log(f"[cycle] paused {delay:.1f}s - HMI should go stale")
        else:
            delay = dwell
            self.harness.drive_status = drive
            self.harness.system_state = state
            self.drive_raw_var.set(str(drive))
            self.state_raw_var.set(str(state))
            self.harness.publish_now()
        self.cycle_job = self.root.after(int(delay * 1000), self._cycle_step)

    # ----------------------------------------------------------------- tick

    def _append_log(self, message: str) -> None:
        self.log_text.configure(state="normal")
        self.log_text.insert("end", message + "\n")
        # keep the widget from growing without bound over a long session
        line_count = int(self.log_text.index("end-1c").split(".")[0])
        if line_count > LOG_MAX_LINES:
            self.log_text.delete("1.0", f"{line_count - LOG_MAX_LINES}.0")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _tick(self) -> None:
        while True:
            try:
                self._append_log(self.log_queue.get_nowait())
            except queue.Empty:
                break

        if self.harness is not None:
            now = time.monotonic()
            elapsed = now - self._rate_time
            if elapsed >= 1.0:
                sent = (self.harness.seq - self._rate_seq) & 0xFF
                self._rate = sent / elapsed
                self._rate_seq = self.harness.seq
                self._rate_time = now
            targets = max(0, self.harness.announced_targets)
            paused = " PAUSED" if self.harness.paused else ""
            self.status_label.configure(
                text=f"seq {self.harness.seq} - {self._rate:.1f} Hz - {targets} target(s){paused}"
            )
        self.root.after(TICK_MS, self._tick)

    def _on_close(self) -> None:
        self._disconnect()
        rtps_host.LOG_SINK = None
        self.root.destroy()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Window for driving the joystick HMI's status labels over RTPS."
    )
    parser.add_argument("--peer", action="append", default=None, metavar="HOST",
                        help="Prefill the peer field (hostname or IP)")
    parser.add_argument("--advertised-address", default=None,
                        help="Prefill the 'Via' field with this local IPv4 address")
    parser.add_argument("--node-name", default="mcb_gui", help="Local participant name")
    parser.add_argument("--domain-id", type=int, default=0, help="RTPS domain id")
    parser.add_argument("--participant-id", type=int, default=14,
                        help="Local participant id; distinct from the other scripts (10-13)")
    parser.add_argument("--multicast-interface", default=None,
                        help="IPv4 interface for the multicast join/send, if it differs")
    parser.add_argument("--multicast-group", default="239.255.0.1",
                        help="RTPS metatraffic multicast group")
    parser.add_argument("--period", type=float, default=spec.MCB_STATUS_PERIOD_MS / 1000.0,
                        help="Seconds between republishes of the current status")
    parser.add_argument("--peer-participant-ids", type=rtps_host.parse_participant_id_range,
                        default="0-3", metavar="IDS",
                        help="Participant ids to try on each peer, as '0-3' or '0,1,2'")
    parser.add_argument("--trace-packets", action="store_true",
                        help="Log every received UDP packet and its RTPS submessage headers")
    cli = parser.parse_args()

    root = tk.Tk()
    McbPanel(root, cli)
    root.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
