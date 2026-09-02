#!/usr/bin/env python3
"""A car on a road, driven by the real joystick over RTPS.

Two pieces:

  CarModel   pure physics, no tkinter, so it can be tested headlessly and — more
             importantly — so it can be the single source of the chair's speed.
             Exactly one exists per session; the publisher borrows it and reads
             speed_tenths off it, which is what makes the number on the Tab5 and
             the car on screen the same number rather than two calculations that
             agree by luck. The GUI owns it rather than the publisher, so the
             drive window can be open before connecting and survive a
             disconnect.

  DriveView  a top-down window: camera on the car, roads scrolling underneath.
             Chosen over a behind-the-car view because both things the drive
             modes are about — sliding sideways and spinning on the spot — are
             invisible from behind.

Drive mode comes from the HMI, not from here: the user picks HOLO / Normal /
Auto on the Tab5's drive screen and it arrives with every joystick sample.
"""

from __future__ import annotations

import math
import os
import sys
import time
import tkinter as tk
from tkinter import ttk
from typing import Optional

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import rammp_rtps as spec  # noqa: E402  (path setup must run first)

#: Metres-per-second per unit of displayed speed. The speed readout is unitless
#: ("4.2"), so this only sets how far the car travels for a given number.
WORLD_UNITS_PER_SPEED = 26.0
#: Seconds of full stick to go from a standstill to the top of the range.
ACCEL_SECONDS = 3.0
#: Radians per second of heading change at full twist deflection.
TWIST_RATE = 2.2
#: Radians per second of steering at full lock, at full speed. Scaled by speed
#: so the car cannot pirouette while stopped — that is what twist is for.
STEER_RATE = 2.0
#: Fraction of full deflection below which an axis counts as centred.
DEADZONE = 0.15
#: World units between road centre lines.
ROAD_SPACING = 400.0
#: Width of a road in world units.
ROAD_WIDTH = 110.0


def _deflection(value_mv: float, center_mv: float) -> float:
    """Signed -1..+1 deflection of one axis, with the deadzone applied."""
    half_scale = spec.JOYSTICK_FULL_SCALE_MV / 2.0
    raw = (value_mv - center_mv) / half_scale
    if abs(raw) < DEADZONE:
        return 0.0
    # Rescale so the axis starts from zero at the edge of the deadzone rather
    # than jumping to DEADZONE's worth of input the moment it is crossed.
    scaled = (abs(raw) - DEADZONE) / (1.0 - DEADZONE)
    return math.copysign(min(scaled, 1.0), raw)


class CarModel:
    """Where the car is, how fast it is going, and which way it points.

    update() takes its own dt from the wall clock rather than being handed one,
    so it does not matter how many callers step it or how often: the publisher
    ticks it twice a second, the window ticks it thirty times a second, and both
    see the same trajectory.
    """

    def __init__(self) -> None:
        self.x = 0.0
        self.y = 0.0
        self.heading = 0.0  # radians, 0 = up the screen
        self.speed = 0.0  # in displayed units, 0 .. SPEED_MAX_TENTHS/10
        self.drive_mode = spec.DRIVE_MODE_NORMAL
        #: Last deflections applied, for the window to display.
        self.inputs = (0.0, 0.0, 0.0)
        self._last_update: Optional[float] = None
        # Measured resting position of each axis. A real stick does not rest at
        # the spec's nominal centre (1506 mV against 1650 on this board), and an
        # uncorrected offset makes the deadzone lopsided.
        self._center = [float(spec.JOYSTICK_CENTER_MV)] * 3
        self._center_samples = 0

    @property
    def max_speed(self) -> float:
        return spec.SPEED_MAX_TENTHS / 10.0

    @property
    def speed_tenths(self) -> int:
        """The number the HMI shows, derived from the car rather than beside it."""
        return max(0, min(int(round(self.speed * 10)), spec.SPEED_MAX_TENTHS))

    def note_center_sample(self, x_mv: int, y_mv: int, twist_mv: int, samples: int) -> bool:
        """Fold one at-rest sample into the measured centre. True while learning."""
        if self._center_samples >= samples:
            return False
        n = self._center_samples
        for index, value in enumerate((x_mv, y_mv, twist_mv)):
            self._center[index] = (self._center[index] * n + value) / (n + 1)
        self._center_samples += 1
        return True

    def update(self, sample: Optional[tuple], drive_mode: Optional[int] = None) -> None:
        """Advance the car to now, given the latest joystick sample."""
        now = time.monotonic()
        if self._last_update is None:
            self._last_update = now
            return
        # Clamped: a stalled window or a breakpoint must not teleport the car.
        dt = min(now - self._last_update, 0.25)
        self._last_update = now
        if sample is None or dt <= 0.0:
            return
        x_mv, y_mv, twist_mv = sample[0], sample[1], sample[2]
        if drive_mode is not None:
            self.drive_mode = drive_mode

        # X: right on the stick reads above centre. Y: forward reads BELOW
        # centre (see the spec header), so it is negated to make "push" positive.
        steer = _deflection(x_mv, self._center[0])
        throttle = -_deflection(y_mv, self._center[1])
        twist = _deflection(twist_mv, self._center[2])
        self.inputs = (steer, throttle, twist)

        # Twist is independent of drive mode: it always spins the chair in place.
        self.heading += twist * TWIST_RATE * dt

        if self.drive_mode == spec.DRIVE_MODE_HOLO:
            self._step_holonomic(steer, throttle, dt)
        else:
            # AUTO is selectable on the HMI but undefined, so it drives as
            # NORMAL rather than silently doing nothing.
            self._step_normal(steer, throttle, dt)

    def _step_normal(self, steer: float, throttle: float, dt: float) -> None:
        """Car-like: throttle builds speed, steering curves the heading."""
        if throttle != 0.0:
            self.speed += throttle * (self.max_speed / ACCEL_SECONDS) * dt
            self.speed = max(0.0, min(self.speed, self.max_speed))
        # Steering authority follows speed, so the car turns as it drives rather
        # than spinning on the spot when parked.
        if self.speed > 0.0:
            self.heading += steer * STEER_RATE * (self.speed / self.max_speed) * dt
        distance = self.speed * WORLD_UNITS_PER_SPEED * dt
        self.x += math.sin(self.heading) * distance
        self.y -= math.cos(self.heading) * distance

    def _step_holonomic(self, steer: float, throttle: float, dt: float) -> None:
        """Stick deflection IS the velocity, in screen axes.

        Screen frame rather than car frame: with twist spinning the chair
        independently, a car-frame mapping means the direction of travel rotates
        under the user's thumb, which is unpredictable to steer by.
        """
        magnitude = min(math.hypot(steer, throttle), 1.0)
        self.speed = magnitude * self.max_speed
        distance = self.speed * WORLD_UNITS_PER_SPEED * dt
        if magnitude > 0.0:
            self.x += (steer / magnitude) * distance
            self.y -= (throttle / magnitude) * distance


class DriveView:
    """Top-down window: the car stays centred, the roads move."""

    CANVAS_W = 640
    CANVAS_H = 460
    FRAME_MS = 33  # ~30 fps

    def __init__(self, parent: tk.Misc, car: CarModel, status_fn=None, on_close=None) -> None:
        self.car = car
        #: Optional callable returning a line about where the input stands, so
        #: an unconnected window explains itself instead of just sitting still.
        self.status_fn = status_fn
        self.on_close = on_close
        self.window = tk.Toplevel(parent)
        self.window.title("Drive view")
        self.window.protocol("WM_DELETE_WINDOW", self.close)

        self.canvas = tk.Canvas(self.window, width=self.CANVAS_W, height=self.CANVAS_H,
                                bg="#2f6b34", highlightthickness=0)
        self.canvas.pack(padx=8, pady=8)

        status = ttk.Frame(self.window, padding=(8, 0, 8, 8))
        status.pack(fill="x")
        self.mode_label = ttk.Label(status, text="mode: -")
        self.mode_label.pack(side="left")
        self.readout = ttk.Label(status, text="")
        self.readout.pack(side="right")
        ttk.Button(status, text="Recentre car", command=self.recentre).pack(side="left", padx=12)

        self._job = self.window.after(self.FRAME_MS, self._tick)

    def recentre(self) -> None:
        """Put the car back at the origin, pointing up. Does not touch speed."""
        self.car.x = self.car.y = self.car.heading = 0.0

    def close(self) -> None:
        if self._job is not None:
            self.window.after_cancel(self._job)
            self._job = None
        if self.on_close:
            self.on_close()
        self.window.destroy()

    # ------------------------------------------------------------- rendering

    def _draw_roads(self) -> None:
        """A grid of roads, drawn in world space offset by the camera.

        A grid rather than one straight road because holonomic mode moves the
        car sideways, and a single road gives nothing to judge that against.
        """
        left = self.car.x - self.CANVAS_W / 2
        top = self.car.y - self.CANVAS_H / 2
        first_col = math.floor(left / ROAD_SPACING) * ROAD_SPACING
        first_row = math.floor(top / ROAD_SPACING) * ROAD_SPACING

        world_x = first_col
        while world_x < left + self.CANVAS_W + ROAD_SPACING:
            screen_x = world_x - left
            self.canvas.create_rectangle(screen_x - ROAD_WIDTH / 2, 0,
                                         screen_x + ROAD_WIDTH / 2, self.CANVAS_H,
                                         fill="#4a4a4a", width=0)
            world_x += ROAD_SPACING

        world_y = first_row
        while world_y < top + self.CANVAS_H + ROAD_SPACING:
            screen_y = world_y - top
            self.canvas.create_rectangle(0, screen_y - ROAD_WIDTH / 2,
                                         self.CANVAS_W, screen_y + ROAD_WIDTH / 2,
                                         fill="#4a4a4a", width=0)
            world_y += ROAD_SPACING

        # Dashed centre lines, drawn after the tarmac so junctions stay clean.
        world_x = first_col
        while world_x < left + self.CANVAS_W + ROAD_SPACING:
            screen_x = world_x - left
            self.canvas.create_line(screen_x, 0, screen_x, self.CANVAS_H,
                                    fill="#e8d44d", dash=(18, 22), width=3)
            world_x += ROAD_SPACING
        world_y = first_row
        while world_y < top + self.CANVAS_H + ROAD_SPACING:
            screen_y = world_y - top
            self.canvas.create_line(0, screen_y, self.CANVAS_W, screen_y,
                                    fill="#e8d44d", dash=(18, 22), width=3)
            world_y += ROAD_SPACING

    def _draw_car(self) -> None:
        cx, cy = self.CANVAS_W / 2, self.CANVAS_H / 2
        sin_h, cos_h = math.sin(self.car.heading), math.cos(self.car.heading)

        def point(dx: float, dy: float) -> tuple[float, float]:
            # dy is negative forward, matching the world's screen-up convention
            return (cx + dx * cos_h - dy * sin_h, cy + dx * sin_h + dy * cos_h)

        body = [point(-14, -24), point(14, -24), point(16, 22), point(-16, 22)]
        self.canvas.create_polygon([c for p in body for c in p],
                                   fill="#d94b3a", outline="#2b2b2b", width=2)
        # Windscreen, so which end is the front is obvious while it spins.
        glass = [point(-10, -20), point(10, -20), point(9, -6), point(-9, -6)]
        self.canvas.create_polygon([c for p in glass for c in p],
                                   fill="#9fd8ef", outline="")
        # Nose marker: the clearest cue that twist is rotating the car.
        self.canvas.create_line(*point(0, -24), *point(0, -34), fill="#ffffff", width=3)

    def _tick(self) -> None:
        self.canvas.delete("all")
        self._draw_roads()
        self._draw_car()

        steer, throttle, twist = self.car.inputs
        mode_name = spec.DRIVE_MODE_NAMES.get(self.car.drive_mode, "?")
        status = self.status_fn() if self.status_fn else "set the drive mode on the Tab5"
        self.mode_label.configure(text=f"mode: {mode_name}   ({status})")
        self.readout.configure(
            text=f"speed {self.car.speed_tenths / 10:.1f}   "
                 f"heading {math.degrees(self.car.heading) % 360:5.0f}°   "
                 f"steer {steer:+.2f} throttle {throttle:+.2f} twist {twist:+.2f}"
        )
        self._job = self.window.after(self.FRAME_MS, self._tick)
