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
from tkinter import simpledialog, ttk

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import rammp_rtps as spec  # noqa: E402  (path setup must run first)
import rtps_host  # noqa: E402
import rtps_drive_game  # noqa: E402
import rtps_mcb_sim  # noqa: E402
import rtps_net  # noqa: E402

LOG_MAX_LINES = 500
TICK_MS = 100
#: How many times to try finding and connecting to the board on startup
#: before giving up and leaving it to Detect/Scan.
AUTOCONNECT_ATTEMPTS = 3
#: Grace after connecting before deciding an attempt worked. Discovery has
#: to complete and the first publish has to find a target inside this.
AUTOCONNECT_VERIFY_MS = 4000
#: Pause between a failed attempt and the next one.
AUTOCONNECT_RETRY_MS = 1500

#: Prefilled error banner. Only shown on the HMI while STATE is not OK, so this
#: is there to make flipping to ERROR immediately show something realistic
#: rather than an empty red panel.
DEFAULT_ERROR_TEXT = "MOTOR CONTROLLER OVERTEMPERATURE"
DEFAULT_ERROR_FOOTER = "REDUCE SPEED AND PULL OVER"

#: "Via" entry meaning "work it out from whichever adapter reaches the peer".
AUTO_ADAPTER = "Auto (follow the route to the board)"


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
        # Worker threads hand results back here; only _tick touches widgets.
        self.result_queue: queue.Queue[tuple] = queue.Queue()
        self.adapters: list = []

        # Owned here rather than by the harness so the drive window can be
        # opened before connecting and survive a disconnect; the harness borrows
        # it and reads the speed off it.
        self.car = rtps_drive_game.CarModel()
        self.drive_view = None
        # Auto-connect bookkeeping. _auto_active is cleared by success or by any
        # manual Connect/Detect/Scan: once the user takes over, the retries must
        # not reach in behind them.
        self._auto_active = True
        self._auto_attempt = 0
        self._auto_pending = False
        self.cycling = False
        self.cycle_job: str | None = None
        self.cycle_index = 0
        self._rate_seq = 0
        self._rate_time = time.monotonic()
        self._rate = 0.0

        self.config = rtps_net.load_config()

        root.title("RAMMP MCB simulator")
        self._build_connection()
        self._build_status_controls()
        self._build_error_banner()
        self._build_joystick()
        self._build_cycle()
        self._build_log()
        root.after(TICK_MS, self._tick)
        root.protocol("WM_DELETE_WINDOW", self._on_close)
        # Passive only, and on a worker thread: opening the window must not
        # block, and nothing scans the network unless Scan is pressed.
        root.after(200, self._auto_step)

    # ---------------------------------------------------------------- widgets

    def _build_connection(self) -> None:
        frame = ttk.LabelFrame(self.root, text="Connection", padding=8)
        frame.pack(fill="x", padx=8, pady=(8, 4))

        ttk.Label(frame, text="Board").grid(row=0, column=0, sticky="w")
        self.peer_var = tk.StringVar(
            value=(self.cli.peer[0] if self.cli.peer else self.config.get("peer", ""))
        )
        ttk.Entry(frame, textvariable=self.peer_var, width=18).grid(row=0, column=1, padx=(4, 4))
        self.detect_button = ttk.Button(frame, text="Detect", command=self._on_detect)
        self.detect_button.grid(row=0, column=2)
        self.scan_button = ttk.Button(frame, text="Scan...", command=self._on_scan)
        self.scan_button.grid(row=0, column=3, padx=(4, 12))

        ttk.Label(frame, text="Via").grid(row=1, column=0, sticky="w", pady=(6, 0))
        self.address_var = tk.StringVar(
            value=self.cli.advertised_address or self.config.get("advertised_address")
            or AUTO_ADAPTER
        )
        self.address_combo = ttk.Combobox(frame, textvariable=self.address_var, width=46)
        self.address_combo.grid(row=1, column=1, columnspan=3, sticky="w", padx=(4, 12),
                                pady=(6, 0))
        self._refresh_adapters()

        self.connect_button = ttk.Button(frame, text="Connect", command=self._toggle_connection)
        self.connect_button.grid(row=0, column=4)
        # tk.Button rather than ttk: on Windows the default "vista" ttk theme
        # draws buttons from a native bitmap and silently ignores `background`,
        # so a ttk style would store the colour and change nothing. The classic
        # widget honours it.
        self.drive_button = tk.Button(
            frame, text="Drive view", command=self._toggle_drive_view,
            bg="#1a6dd4", fg="white", activebackground="#2f82ea", activeforeground="white",
            relief="raised", borderwidth=1, padx=10, cursor="hand2")
        self.drive_button.grid(row=0, column=5, padx=(8, 0))

        self.connection_label = ttk.Label(frame, text="not connected")
        self.connection_label.grid(row=1, column=4, columnspan=2, sticky="w", pady=(6, 0))

        ttk.Label(
            frame,
            text="Via defaults to Auto, which picks the adapter that actually routes to the "
                 "board \u2014 not the one that reaches the internet.",
            foreground="#666666",
        ).grid(row=2, column=0, columnspan=6, sticky="w", pady=(6, 0))

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

    def _build_error_banner(self) -> None:
        frame = ttk.LabelFrame(self.root, text="Error banner", padding=8)
        frame.pack(fill="x", padx=8, pady=4)

        self.error_text_var = tk.StringVar(value=DEFAULT_ERROR_TEXT)
        self.error_footer_var = tk.StringVar(value=DEFAULT_ERROR_FOOTER)
        for row, (label, var, limit) in enumerate((
            ("body", self.error_text_var, spec.ERROR_TEXT_LEN),
            ("footer", self.error_footer_var, spec.ERROR_FOOTER_LEN),
        )):
            ttk.Label(frame, text=label).grid(row=row, column=0, sticky="w", pady=2)
            entry = ttk.Entry(frame, textvariable=var, width=46)
            entry.grid(row=row, column=1, padx=4, pady=2)
            entry.bind("<Return>", lambda _event: self._apply_error_text())
            ttk.Label(frame, text=f"max {limit - 1}", foreground="#666666").grid(
                row=row, column=2, sticky="w"
            )
        ttk.Button(frame, text="Set", command=self._apply_error_text).grid(row=0, column=3, padx=6)
        ttk.Button(
            frame, text="Clear",
            command=lambda: (self.error_text_var.set(""), self.error_footer_var.set(""),
                             self._apply_error_text()),
        ).grid(row=1, column=3, padx=6)
        ttk.Label(
            frame,
            text="The panel only shows on the HMI while STATE is not OK.",
            foreground="#666666",
        ).grid(row=2, column=0, columnspan=4, sticky="w", pady=(6, 0))

    def _build_joystick(self) -> None:
        """Read-only view of what the joystick is publishing back to us."""
        frame = ttk.LabelFrame(self.root, text="Joystick (from the HMI)", padding=8)
        frame.pack(fill="x", padx=8, pady=4)

        self.axis_bars = {}
        self.axis_labels = {}
        for row, axis in enumerate(("X", "Y", "Twist")):
            ttk.Label(frame, text=axis, width=6).grid(row=row, column=0, sticky="w")
            # Deflection as 0-100 with centre at 50, so a resting stick sits
            # mid-bar and either direction is visible.
            bar = ttk.Progressbar(frame, orient="horizontal", length=320, maximum=100)
            bar.grid(row=row, column=1, padx=4, pady=1)
            value = ttk.Label(frame, text="-", width=22)
            value.grid(row=row, column=2, sticky="w")
            self.axis_bars[axis] = bar
            self.axis_labels[axis] = value

        self.button_label = ttk.Label(frame, text="button: -", width=22)
        self.button_label.grid(row=3, column=0, columnspan=2, sticky="w", pady=(6, 0))
        self.speed_label = ttk.Label(frame, text="emulated speed: 0.0")
        self.speed_label.grid(row=3, column=2, sticky="w", pady=(6, 0))

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

    def _toggle_drive_view(self) -> None:
        """Open (or close) the car window.

        Deliberately works while disconnected: the window opening and saying so
        is far better feedback than a button that appears to do nothing. The car
        simply sits still until joystick samples start arriving.
        """
        if self.drive_view is not None:
            self.drive_view.close()
            return
        self.drive_view = rtps_drive_game.DriveView(
            self.root, self.car, status_fn=self._drive_status,
            on_close=self._drive_view_closed)
        self.drive_button.configure(text="Close drive view")

    def _drive_status(self) -> str:
        """One line for the drive window about where its input is coming from."""
        if self.harness is None:
            return "not connected - press Connect on the MCB panel to drive"
        if self.harness.joystick is None:
            return "connected, waiting for joystick samples"
        return "set the drive mode on the Tab5"

    def _drive_view_closed(self) -> None:
        self.drive_view = None
        self.drive_button.configure(text="Drive view")

    def _build_log(self) -> None:
        frame = ttk.LabelFrame(self.root, text="Log", padding=4)
        frame.pack(fill="both", expand=True, padx=8, pady=(4, 8))
        self.log_text = tk.Text(frame, height=12, width=90, wrap="none", state="disabled")
        self.log_text.pack(side="left", fill="both", expand=True)
        scroll = ttk.Scrollbar(frame, command=self.log_text.yview)
        scroll.pack(side="right", fill="y")
        self.log_text.configure(yscrollcommand=scroll.set)

    # ------------------------------------------------------------ connection

    def _refresh_adapters(self) -> None:
        """Populate the Via dropdown with named adapters, Auto first."""
        self.adapters = rtps_net.list_adapters()
        values = [AUTO_ADAPTER]
        # Live adapters first; a down one is almost never the answer but is
        # still worth offering rather than hiding.
        values += [a.label() for a in self.adapters if a.is_up]
        values += [a.label() for a in self.adapters if not a.is_up]
        self.address_combo["values"] = values

    def _selected_address(self) -> str | None:
        """The advertised address to use, or None for Auto."""
        chosen = self.address_var.get().strip()
        if not chosen or chosen == AUTO_ADAPTER:
            return None
        for adapter in self.adapters:
            if chosen == adapter.label():
                return adapter.ip
        return chosen  # a hand-typed address

    def _auto_step(self) -> None:
        """One attempt at finding the board and connecting to it.

        Runs on open so the common case — board on, address remembered — needs
        no clicks at all. Retries because discovery is a network operation and
        one miss is not evidence the board is absent; gives up after a few so a
        genuinely absent board does not leave the panel looping forever.
        """
        if not self._auto_active or self.harness is not None:
            return
        self._auto_attempt += 1
        if self._auto_attempt > AUTOCONNECT_ATTEMPTS:
            self._auto_active = False
            self.connection_label.configure(
                text=f"auto-connect gave up after {AUTOCONNECT_ATTEMPTS} tries")
            self._append_log("[gui] auto-connect gave up - use Detect, or Scan for a subnet")
            return
        self._append_log(f"[gui] auto-connect attempt {self._auto_attempt}"
                         f"/{AUTOCONNECT_ATTEMPTS}")
        self._auto_pending = True
        self._start_discovery()

    def _auto_verify(self) -> None:
        """Did the attempt actually reach the board, or only open a socket?"""
        if not self._auto_active:
            return
        if self.harness is not None and self.harness.announced_targets > 0:
            self._auto_active = False
            self._append_log("[gui] auto-connect succeeded")
            return
        self._append_log("[gui] connected but nothing answered; retrying")
        if self.harness is not None:
            self._disconnect()
        self.root.after(AUTOCONNECT_RETRY_MS, self._auto_step)

    def _cancel_autoconnect(self) -> None:
        """A manual action takes precedence over the retry sequence."""
        if self._auto_active:
            self._auto_active = False

    def _start_discovery(self) -> None:
        """Find the board on a worker thread; never blocks the window."""
        if self.harness is not None:
            self._append_log("[gui] disconnect before detecting")
            return
        saved = self.peer_var.get().strip() or self.config.get("peer")
        self.detect_button.configure(state="disabled")
        self.connection_label.configure(text="looking for the board...")

        def work() -> None:
            found = rtps_net.discover_board(saved, progress=self.log_queue.put)
            self.result_queue.put(("detect", found))

        threading.Thread(target=work, daemon=True).start()

    def _on_detect(self) -> None:
        self._cancel_autoconnect()
        self._start_discovery()

    def _on_scan(self) -> None:
        """Sweep a subnet, after showing which one."""
        self._cancel_autoconnect()
        if self.harness is not None:
            self._append_log("[gui] disconnect before scanning")
            return
        default = rtps_net.subnet_guess(self.peer_var.get().strip()
                                        or self.config.get("peer"))
        subnet = simpledialog.askstring(
            "Scan for the board",
            "Subnet to sweep with RTPS discovery probes:",
            initialvalue=default, parent=self.root)
        if not subnet:
            return
        self.scan_button.configure(state="disabled")
        self.connection_label.configure(text=f"scanning {subnet}...")

        def work() -> None:
            try:
                found = rtps_net.scan_subnet(subnet, progress=self.log_queue.put)
            except ValueError as exc:
                self.log_queue.put(f"[gui] {subnet} is not a valid subnet: {exc}")
                found = []
            self.result_queue.put(("scan", found[0] if found else None))

        threading.Thread(target=work, daemon=True).start()

    def _discovery_finished(self, kind: str, found: str | None) -> None:
        self.detect_button.configure(state="normal")
        self.scan_button.configure(state="normal")
        auto, self._auto_pending = self._auto_pending, False
        if found:
            self.peer_var.set(found)
            adapter = rtps_net.adapter_for(found)
            via = adapter.label() if adapter else "unknown"
            self.connection_label.configure(text=f"found {found} via {via}")
            self._append_log(f"[gui] board at {found}, reachable via {via}")
            if auto and self._auto_active and self.harness is None:
                self._connect()
                self.root.after(AUTOCONNECT_VERIFY_MS, self._auto_verify)
            return
        hint = "try Scan" if kind == "detect" else "check the subnet, and that the board is on"
        self.connection_label.configure(text=f"board not found - {hint}")
        if auto and self._auto_active:
            self.root.after(AUTOCONNECT_RETRY_MS, self._auto_step)

    def _toggle_connection(self) -> None:
        self._cancel_autoconnect()
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
                advertised_address=self._selected_address()
                or rtps_net.source_address_for(self.peer_var.get().strip()),
                multicast_interface=self.cli.multicast_interface,
                multicast_group=self.cli.multicast_group,
                period=self.cli.period,
                trace_packets=self.cli.trace_packets,
                peer=[self.peer_var.get().strip()] if self.peer_var.get().strip() else None,
                peer_participant_ids=self.cli.peer_participant_ids,
            )
        )
        try:
            self.harness = rtps_mcb_sim.McbStatusPublisher(args, car=self.car)
        except OSError as exc:
            # a bad address or a port already taken by another script
            self._append_log(f"[gui] connect failed: {exc}")
            self.connection_label.configure(text="connect failed")
            self.harness = None
            return
        self.thread = threading.Thread(target=self.harness.run, daemon=True)
        self.thread.start()
        # A fresh publisher starts INACTIVE, but the interesting state to be in
        # on connecting is a chair that is actually driveable - otherwise the
        # HMI bars entry to the drive screen and the first thing anyone does is
        # click ACTIVE by hand.
        self.harness.drive_status = spec.DRIVE_STATUS_ACTIVE
        self.drive_raw_var.set(str(spec.DRIVE_STATUS_ACTIVE))
        self._apply_overrides()
        self._apply_error_text()
        self.connect_button.configure(text="Disconnect")
        self.connection_label.configure(text=f"publishing as {args.advertised_address}")
        # Only remember settings that actually stood up a participant.
        rtps_net.save_config({
            "peer": self.peer_var.get().strip() or None,
            "advertised_address": args.advertised_address,
            "period": self.cli.period,
        })

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

    def _apply_error_text(self) -> None:
        if self.harness is None:
            self._append_log("[gui] not connected")
            return
        self.harness.error_text = self.error_text_var.get()
        self.harness.error_footer = self.error_footer_var.get()
        self._publish()

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
        while True:
            try:
                kind, payload = self.result_queue.get_nowait()
            except queue.Empty:
                break
            if kind in ("detect", "scan"):
                self._discovery_finished(kind, payload)

        if self.harness is not None:
            now = time.monotonic()
            elapsed = now - self._rate_time
            if elapsed >= 1.0:
                sent = (self.harness.seq - self._rate_seq) & 0xFF
                self._rate = sent / elapsed
                self._rate_seq = self.harness.seq
                self._rate_time = now
            # Also steps the car; update() takes its own dt, so this and the
            # publisher's own tick cannot double-integrate.
            self.harness.step_speed()
            self._update_joystick()
            targets = max(0, self.harness.announced_targets)
            paused = " PAUSED" if self.harness.paused else ""
            self.status_label.configure(
                text=f"seq {self.harness.seq} - {self._rate:.1f} Hz - {targets} target(s){paused}"
            )
        self.root.after(TICK_MS, self._tick)

    def _update_joystick(self) -> None:
        sample = self.harness.joystick if self.harness is not None else None
        if sample is None:
            for axis in self.axis_bars:
                self.axis_bars[axis]["value"] = 50
                self.axis_labels[axis].configure(text="no data")
            self.button_label.configure(text="button: no data")
            return
        x_mv, y_mv, twist_mv, buttons = sample[0], sample[1], sample[2], sample[3]
        half_scale = spec.JOYSTICK_FULL_SCALE_MV / 2.0
        # Shown against the spec's nominal centre, not the measured one: the
        # standing offset of a real stick is worth seeing rather than hiding.
        # Only the speed emulation corrects for it.
        for axis, mv in (("X", x_mv), ("Y", y_mv), ("Twist", twist_mv)):
            deflection = (mv - spec.JOYSTICK_CENTER_MV) / half_scale
            self.axis_bars[axis]["value"] = max(0, min(100, 50 + deflection * 50))
            self.axis_labels[axis].configure(text=f"{mv:4d} mV  ({deflection:+.2f})")
        mode = spec.DRIVE_MODE_NAMES.get(sample[4] if len(sample) > 4 else None, "?")
        pressed = bool(buttons & spec.BUTTON_JOYSTICK)
        self.button_label.configure(
            text=f"button: {'PRESSED' if pressed else 'released'}   mode: {mode}")
        self.speed_label.configure(
            text=f"emulated speed: {self.harness.speed_tenths / 10:.1f}"
                 f"   (Y rest {self.harness.center_y:.0f} mV)"
        )

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
