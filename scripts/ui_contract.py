#!/usr/bin/env python3
"""Fail the import when the SquareLine export stops matching what main.cpp assumes.

main.cpp reaches into the export by symbol name -- ui_StatusPanel4, ui_Button1,
ui_Image3 and ~90 others. Most drift is caught by the compiler: rename or delete
an object and the build fails on an undefined symbol, loudly, which needs no
help from this script.

What the compiler CANNOT catch is renumbering. SquareLine names instances by
creation order across the whole project, so adding a StatusPanel to one screen
can renumber the StatusPanels on every other screen. The name still exists and
still compiles -- it just refers to a different object now, quite possibly on a
different screen. The firmware then binds MCB telemetry to the wrong panel, or
opens the R&D screen from the wrong settings row, silently.

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
PARENTS: dict[str, str] = {
    # boot logo: main.cpp swaps the pre-rasterised A8 mask onto this image
    "ui_Image3": "ui_BootScreen",

    # MainScreenFlex pager. The lock gating hides these three by binding
    # paging_subject to their HIDDEN flag, so they have to be pager pages.
    "ui_FlexPanel": "ui_MainScreenFlex",
    "ui_LockedPanel": "ui_FlexPanel",
    "ui_DrivePanel": "ui_FlexPanel",
    "ui_SeatAdjustmentMenu": "ui_FlexPanel",
    "ui_SettingsMenu": "ui_FlexPanel",

    # the push-and-hold arcs, one per pager page
    "ui_GraphicsPanel": "ui_LockedPanel",
    "ui_GraphicsPanel1": "ui_DrivePanel",
    "ui_GraphicsPanel2": "ui_SeatAdjustmentMenu",
    "ui_UnlockArc": "ui_GraphicsPanel",
    "ui_UnlockArc1": "ui_GraphicsPanel1",
    "ui_UnlockArc2": "ui_GraphicsPanel2",

    # settings rows the firmware wires handlers onto
    "ui_SettingsFlexPanel": "ui_SettingsMenu",
    "ui_Button1": "ui_SettingsFlexPanel",           # DEBUG ACTUATORS -> RDScreen
    "ui_Button6": "ui_SettingsFlexPanel",           # SELF TEST -> selftest_request
    "ui_FPSCounterButton": "ui_SettingsFlexPanel",
    "ui_HapticTestButton": "ui_SettingsFlexPanel",
    "ui_ScreenBrightnessButton": "ui_SettingsFlexPanel",  # -> SpecificSettingScreen

    # exit gestures: each bar has to be on the screen its gesture applies to,
    # or it fills a bar the user cannot see
    "ui_ExitBarPress1": "ui_DriveScreen",
    "ui_ExitBarPull1": "ui_SeatAdjustmentFlexScreen",
    "ui_ExitBarPushLeft": "ui_SeatAdjustmentPanel",
    "ui_ExitBarPull2": "ui_RDScreen",
    "ui_ExitBarPull4": "ui_SpecificSettingScreen",
    "ui_ExitBarPress2": "ui_LogScreen",

    # LogScreen: log_view.cpp points this text area at the captured serial log
    "ui_TextArea1": "ui_LogScreenPanelInner",
    "ui_GoToOldestButton": "ui_LogScreenPanel",
    "ui_GoToNewestButton": "ui_LogScreenPanel",

    # RDScreen PIN entry
    "ui_SeatFunctionsButtonsPanel1": "ui_SeatAdjustmentScreenFlexPanel1",
    "ui_CheckboxContainer": "ui_SeatFunctionsButtonsPanel1",
    "ui_Checkbox1": "ui_CheckboxContainer",
    "ui_Checkbox2": "ui_CheckboxContainer",
    "ui_Checkbox3": "ui_CheckboxContainer",
    "ui_Checkbox4": "ui_CheckboxContainer",
    "ui_Keyboard1": "ui_SeatFunctionsButtonsPanel1",
    "ui_SeatFunctionsLabel2": "ui_SeatFunctionsButtonsPanel1",

    # SpecificSettingScreen: main.cpp fills in the title and instructions per
    # page, deletes the Parameter1 template, and builds each page's rows into
    # SpecificSettingsRows
    "ui_SettingTitleLabel": "ui_SpecificSettingsInnerPanel",
    "ui_BriefInstructionsLabel": "ui_SpecificSettingsInnerPanel",
    "ui_Parameter1": "ui_SpecificSettingsRows",
    "ui_ErrorWarningPanel6": "ui_SpecificSettingScreen",

    # GenericActionsScreen: entered by holding up on this pager page (its arc
    # shows the hold); main.cpp deletes the component instances and builds one
    # per entry in actions_spec.h into the flex panel
    "ui_GenericActionsPanel1": "ui_FlexPanel",
    "ui_UnlockArc3": "ui_GraphicsPanel3",
    "ui_GenericActionsFlexPanel": "ui_GenericActionsPanel",
    "ui_GenericActionsTitle": "ui_GenericActionsFlexPanel",
    "ui_ExitBarPull5": "ui_GenericActionsScreen",
    "ui_ErrorWarningPanel7": "ui_GenericActionsScreen",
    "ui_StatusPanel8": "ui_GenericActionsScreen",
    "ui_TopBar9": "ui_GenericActionsScreen",

    # seat screen grids
    "ui_SeatButton1": "ui_SeatFunctionsButtonsPanel",
    "ui_SeatButton6": "ui_SeatFunctionsButtonsPanel",
    "ui_SeatAdjustmentButton1": "ui_SeatAdjustmentPanel",
    "ui_SeatAdjustmentButton5": "ui_SeatAdjustmentPanel",

    # ErrorWarningPanels main.cpp raises: entry refused (drive or seat page),
    # and link/MCB lost on the drive and seat screens
    "ui_ErrorWarningPanel4": "ui_MainScreenFlex",
    "ui_ErrorWarningPanel": "ui_DriveScreen",
    "ui_ErrorWarningPanel1": "ui_SeatAdjustmentFlexScreen",

    # JoystickTest: joystick_cal.cpp runs its calibration from this button
    # and prompts in this label
    "ui_CalibrateJoystickButton": "ui_LockedPanel2",
    "ui_CalibrateJoystickButtonLabel": "ui_CalibrateJoystickButton",
    "ui_JoystickInstructionsLabel": "ui_JoystickTextPanel",

    # one StatusPanel and one TopBar per screen, each bound to MCB telemetry by
    # number -- the single most renumbering-prone thing in the project
    "ui_StatusPanel": "ui_MainScreenFlex",
    "ui_StatusPanel1": "ui_JoystickTest",
    "ui_StatusPanel2": "ui_DriveScreen",
    "ui_StatusPanel3": "ui_SeatAdjustmentFlexScreen",
    "ui_StatusPanel4": "ui_RDScreen",
    "ui_StatusPanel5": "ui_LogScreen",
    "ui_StatusPanel7": "ui_SpecificSettingScreen",
    "ui_TopBar1": "ui_JoystickTest",
    "ui_TopBar2": "ui_DriveScreen",
    "ui_TopBar3": "ui_MainScreenFlex",
    "ui_TopBar4": "ui_SeatAdjustmentFlexScreen",
    "ui_TopBar5": "ui_RDScreen",
    "ui_TopBar6": "ui_LogScreen",
    "ui_TopBar8": "ui_SpecificSettingScreen",
}

# Labels that identify a widget by its ROLE. A settings row is only "the R&D
# DEBUG row" because of what it says; if the numbering shifts, the parent check
# above still passes (it is still some row on the settings panel) and only the
# text gives it away.
LABELS: dict[str, str] = {
    "ui_ButtonLabel1": "DEBUG ACTUATORS",  # the row that opens ui_RDScreen
    "ui_ButtonLabel6": "SELF TEST",        # the row that starts the self test
    "ui_FPSCounterLabel": "FPS COUNTER",
    "ui_HapticTestLabel": "HAPTIC TEST",
    "ui_ButtonLabel7": "SCREEN BRIGHTNESS",  # opens the brightness settings page
    "ui_CalibrateJoystickButtonLabel": "CALIBRATE",  # joystick_cal.cpp's button
    "ui_GoToOldestButtonLabel": "Oldest",   # log_view.cpp: scroll to the top
    "ui_GoToNewestButtonLabel": "Newest",   # log_view.cpp: scroll to the end
    # the four live seat functions, in the order seat_buttons_grid expects
    "ui_SeatButtonLabel1": "Elevation",
    "ui_SeatButtonLabel2": "Real Tilt",
    "ui_SeatButtonLabel3": "FW Tilt",
    "ui_SeatButtonLabel4": "Side Tilt",
}

_CREATE_RE = re.compile(
    r"^\s*(ui_[A-Za-z0-9_]+)\s*=\s*[A-Za-z0-9_]+_create\(\s*(ui_[A-Za-z0-9_]+|NULL)\s*\)", re.M)
_LABEL_RE = re.compile(r'lv_label_set_text\(\s*(ui_[A-Za-z0-9_]+)\s*,\s*"((?:[^"\\]|\\.)*)"', re.M)


def repo_root() -> pathlib.Path:
    """Walk up from this file until main/ui is in sight."""
    directory = pathlib.Path(__file__).resolve().parent
    while True:
        if (directory / "main" / "ui").is_dir():
            return directory
        if directory.parent == directory:
            raise SystemExit("could not find main/ui above this script")
        directory = directory.parent


def scan(root: pathlib.Path) -> tuple[dict[str, str], dict[str, str]]:
    parents: dict[str, str] = {}
    labels: dict[str, str] = {}
    sources = list((root / "main" / "ui").rglob("*.c"))
    if not sources:
        raise SystemExit("main/ui holds no .c files - did the import run?")
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
