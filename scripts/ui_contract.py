#!/usr/bin/env python3
"""Fail the import when the SquareLine export stops matching what main.cpp assumes.

main.cpp reaches into the export by symbol name -- ui_DriveBand4, ui_BenchKey7,
ui_Image3 and ~90 others. Most drift is caught by the compiler: rename or delete
an object and the build fails on an undefined symbol, loudly, which needs no
help from this script.

What the compiler CANNOT catch is renumbering. SquareLine names instances by
creation order across the whole project, so adding a screen can renumber the
TopBar, DriveBand, MenuKey and MenuOverlay on every other screen. The name still exists and
still compiles -- it just refers to a different object now, quite possibly on a
different screen. The firmware then binds MCB telemetry to the wrong band, or
opens the wrong screen from a burger-menu row, silently.

So this asserts the things main.cpp believes that the compiler cannot: which
parent each named object hangs off, and the label text that identifies a row by
its role rather than its number.

Run from import_ui.ps1 straight after the mirror, before anything is built.

  python ui_contract.py            # report; exit 1 on any mismatch

Stdlib only, so import_ui.ps1 can always run it -- same constraint as
ui_assets.py. Everything prints to stdout, because the caller runs under
$ErrorActionPreference = "Stop" where stderr would look like a failure.
"""

from __future__ import annotations

import pathlib
import re
import sys

# --- what main.cpp assumes ---------------------------------------------------
#
# Each entry is a symbol the firmware names and the parent it must hang off.
# Keep this in step with main.cpp: if you point the firmware at a new export
# object, add it here too, or the next renumbering will go unnoticed.
#
# Spec V2 gives every screen but BootScreen the same four-piece chrome, and each
# piece is an instance numbered across the whole project -- which is exactly the
# renumbering this script exists to catch. So the chrome table is the bulk of
# it, and is expanded rather than written out: eleven screens times five
# instances is too many lines to keep honest by hand.

# screen -> the TopBar, DriveBand, ErrorBanner, MenuKey and MenuOverlay instance
# numbers, exactly as main.cpp names them.
CHROME: dict[str, tuple[str, str, str, str, str]] = {
    "ui_LockedScreen":       ("1",  "1",  "2",  "1",  "1"),
    "ui_DriveScreen":        ("2",  "2",  "4",  "2",  "2"),
    "ui_JoystickScreen":     ("3",  "4",  "10", "3",  "3"),
    "ui_SeatScreen":         ("4",  "3",  "1",  "4",  "4"),
    "ui_BenchGateScreen":    ("5",  "11", "11", "11", "11"),
    "ui_LogScreen":          ("6",  "5",  "5",  "6",  "6"),
    "ui_BenchMotorsScreen":  ("7",  "6",  "3",  "5",  "5"),
    "ui_SettingsScreen":     ("8",  "7",  "6",  "7",  "7"),
    "ui_SkunkWorksScreen":   ("9",  "8",  "7",  "8",  "8"),
    "ui_DiagnosticsScreen":  ("10", "9",  "8",  "9",  "9"),
    "ui_UpdateScreen":       ("11", "10", "9",  "10", "10"),
}

PARENTS: dict[str, str] = {
    # boot logo: main.cpp swaps the pre-rasterised A8 mask onto this image
    "ui_Image3": "ui_BootScreen",

    # LockedScreen: the ring the unlock fills, the shackle that rises when it
    # lands, and the legend that says how (the stick, not a button)
    "ui_DriveHint": "ui_LockedContent",
    "ui_LockRing": "ui_LockedContent",
    "ui_Shackle": "ui_LockedContent",

    # DriveScreen: the readouts and the three drive-profile buttons
    "ui_SpeedValue": "ui_DriveContent",
    "ui_RangeMeter": "ui_DriveContent",
    "ui_ModeManual": "ui_DriveContent",
    "ui_ModeAssist": "ui_DriveContent",
    "ui_ModeAuto": "ui_DriveContent",

    # JoystickScreen: the three axis bars, the button counter, and the label
    # joystick_cal.cpp prompts in
    "ui_XAxisBar": "ui_JoystickContent",
    "ui_YAxisBar": "ui_JoystickContent",
    "ui_TwistBar": "ui_JoystickContent",
    "ui_ButtonCounter": "ui_JoystickContent",
    "ui_JoystickHint": "ui_JoystickContent",
    "ui_CalibrateButton": "ui_JoystickContent",
    # the meter main.cpp binds to the press-and-hold that starts a run
    "ui_CalibrateFill": "ui_CalibrateButton",

    # BenchGateScreen: four dots and eleven keys on the one content panel.
    # main.cpp hands each key its digit by name, so a key that moved would type
    # a different PIN without failing to compile.
    "ui_BenchIntro": "ui_BenchContent",
    "ui_BenchPinDot1": "ui_BenchContent",
    "ui_BenchPinDot4": "ui_BenchContent",
    "ui_BenchKey1": "ui_BenchContent",
    "ui_BenchKey9": "ui_BenchContent",
    "ui_BenchKey0": "ui_BenchContent",
    "ui_BenchKeyBack": "ui_BenchContent",

    # LogScreen: log_view.cpp points this text area at the captured serial log
    "ui_TextArea1": "ui_LogScreenPanelInner",

    # SettingsScreen: main.cpp fills in the title per page, deletes the
    # Parameter1 template, and builds each page's rows into SpecificSettingsRows
    "ui_SettingsTitle": "ui_SettingsBody",
    "ui_Parameter1": "ui_SpecificSettingsRows",

    # SkunkWorksScreen: main.cpp deletes the placeholder tiles and builds one
    # per entry in actions_spec.h into the flex panel
    "ui_SkunkWorksTitle": "ui_SkunkWorksBody",
    "ui_GenericActionsFlexPanel": "ui_SlotRows",

    # DiagnosticsScreen: main.cpp drives the rate label, deletes the template
    # component and builds one per entry in RAMMP_DIAG_TABLE into the rows panel
    "ui_DiagnosticsFreqLabel": "ui_DiagnosticsScreen",
    "ui_DiagnosticsFlexRows": "ui_SpecificSettingsInnerPanel1",

    # SeatScreen grids. The numbers skip 3 and jump to 7 -- SquareLine
    # renumbered these when the screen was renamed -- so the LABELS table below
    # is what actually pins each button to the axis it drives.
    "ui_SeatButton1": "ui_SeatFunctionsButtonsPanel",
    "ui_SeatButton7": "ui_SeatFunctionsButtonsPanel",
    "ui_SeatButton6": "ui_SeatFunctionsButtonsPanel",
    "ui_SeatAdjustmentButton1": "ui_SeatAdjustmentPanel",
    "ui_SeatAdjustmentButton5": "ui_SeatAdjustmentPanel",
    "ui_AngleLabel": "ui_AngleIndicator",
}

for _screen, (_bar, _band, _banner, _key, _overlay) in CHROME.items():
    PARENTS["ui_TopBar" + _bar] = _screen
    PARENTS["ui_DriveBand" + _band] = _screen
    PARENTS["ui_ErrorBanner" + _banner] = _screen
    PARENTS["ui_MenuKey" + _key] = _screen
    PARENTS["ui_MenuOverlay" + _overlay] = _screen

# Labels that identify a widget by its ROLE. A seat button is only "the one that
# drives FB Tilt" because of what it says; if the numbering shifts, the parent
# check above still passes -- it is still some button on the functions panel --
# and only the text gives it away.
#
# cui_* names are component-internal locals (ui_comp_menuoverlay.c), which is
# where the burger menu's rows live: they carry no screen global to name.
LABELS: dict[str, str] = {
    # joystick_cal.cpp reads this as the button's resting text, and writes
    # CANCEL over it during a run
    "ui_CalibrateButtonLabel": "Calibrate",

    # The four live seat functions, in the order seat_buttons_grid walks them
    # (row-major, two per row) against the rows of RAMMP_SEAT_AXIS_TABLE. Read
    # this beside main.cpp's seat_button_axis(): a mismatch is a press that
    # moves the wrong actuator with the right label on it.
    "ui_SeatButtonLabel1": "FB Tilt",
    "ui_SeatButtonLabel2": "Side Tilt",
    "ui_SeatButtonLabel4": "Elevation",
    "ui_SeatButtonLabel7": "Translation",

    # The PIN pad, spot-checked at both ends and either side of the hole.
    "ui_BenchKey1Label": "1",
    "ui_BenchKey5Label": "5",
    "ui_BenchKey9Label": "9",
    "ui_BenchKey0Label": "0",

    # The burger menu, in the order nav_go switches on. A row that moves opens
    # the wrong screen, silently -- nav_row_cb carries only the row index.
    "cui_RowLabel1": "Drive",
    "cui_RowLabel2": "Seat Functions",
    "cui_RowLabel3": "Skunk Works",
    "cui_RowLabel4": "Log",
    "cui_RowLabel5": "Diagnostics",
    "cui_RowLabel6": "Bench",
    "cui_RowLabel7": "UI Settings",
    "cui_RowLabel8": "Joystick",
}

_CREATE_RE = re.compile(
    r"^\s*(ui_[A-Za-z0-9_]+)\s*=\s*[A-Za-z0-9_]+_create\(\s*(ui_[A-Za-z0-9_]+|NULL)\s*\)", re.M)
# c?ui_: the burger menu's rows are locals inside ui_comp_menuoverlay.c, so the
# only name they have is the component-internal one.
_LABEL_RE = re.compile(
    r'lv_label_set_text\(\s*(c?ui_[A-Za-z0-9_]+)\s*,\s*"((?:[^"\\]|\\.)*)"', re.M)


def repo_root() -> pathlib.Path:
    """Walk up from this file until components/ui is in sight."""
    directory = pathlib.Path(__file__).resolve().parent
    while True:
        if (directory / "components" / "ui").is_dir():
            return directory
        if directory.parent == directory:
            raise SystemExit("could not find components/ui above this script")
        directory = directory.parent


def scan(root: pathlib.Path) -> tuple[dict[str, str], dict[str, str]]:
    parents: dict[str, str] = {}
    labels: dict[str, str] = {}
    sources = list((root / "components" / "ui").rglob("*.c"))
    if not sources:
        raise SystemExit("components/ui holds no .c files - did the import run?")
    for path in sources:
        text = path.read_text(encoding="utf-8", errors="replace")
        for symbol, parent in _CREATE_RE.findall(text):
            parents[symbol] = parent
        for symbol, value in _LABEL_RE.findall(text):
            # SquareLine emits the C escape, so "\n" arrives as two characters
            labels[symbol] = value.replace("\\n", "\n")
    return parents, labels


def main() -> int:
    root = repo_root()
    parents, labels = scan(root)
    problems: list[str] = []

    for symbol, expected in PARENTS.items():
        actual = parents.get(symbol)
        if actual is None:
            problems.append(f"{symbol}: gone from the export (main.cpp still names it)")
        elif actual != expected:
            problems.append(f"{symbol}: now created on {actual}, main.cpp expects {expected}")

    for symbol, expected in LABELS.items():
        actual = labels.get(symbol)
        if actual is None:
            problems.append(f"{symbol}: no lv_label_set_text found (main.cpp identifies it by text)")
        elif actual.split("\n")[0].strip() != expected:
            problems.append(
                f"{symbol}: reads {actual.split(chr(10))[0].strip()!r}, main.cpp expects {expected!r}")

    checked = len(PARENTS) + len(LABELS)
    if not problems:
        print(f"ui_contract: OK - {checked} assumptions still hold "
              f"({len(parents)} objects seen in the export)")
        return 0

    print(f"ui_contract: {len(problems)} of {checked} assumptions BROKEN\n")
    for problem in problems:
        print(f"  {problem}")
    print("\nThe export no longer matches what main.cpp reaches for. This is the failure")
    print("the compiler cannot catch: the symbols still exist, they just mean something")
    print("else now, so the firmware would bind to the wrong widget silently.")
    print("Fix the SquareLine project, or update main.cpp AND the tables in this script.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
