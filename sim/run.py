#!/usr/bin/env python3
"""
RAMMP HMI Simulator Launcher
============================

One command to configure, build and launch the desktop simulator: the HMI's
real screens in a window on a PC, driven from the keyboard, with no Tab5 and
no MCB on the bench.

Usage::

    python run.py                 # build (if needed) and launch
    python run.py --zoom 100      # launch at the panel's true 720x1280
    python run.py --rebuild       # wipe the build dir and start over
    python run.py --build-only    # compile without launching
    python run.py --verbose       # show the full compiler output

What it does:

1. Checks for CMake, a build generator and a C/C++ compiler, and tells you
   what to install if one is missing.
2. Configures ``sim/build/`` on first run. CMake fetches LVGL 9.5 itself, so
   there is nothing to install by hand and no ESP-IDF involved.
3. Builds ``main/ui/`` -- the same SquareLine export the firmware compiles,
   unmodified -- plus the simulator's input, navigation and fake-MCB layers.
4. Launches the executable and forwards its exit code.

The first build takes a few minutes: it compiles LVGL, ThorVG and the
generated font and image tables from scratch. Every build after that is
incremental and takes seconds.

See Also:
    - ``sim/README.md`` -- key map, and what the sim is and is not evidence of.
    - ``scripts/rtps_mcb_gui.py`` -- the other test tool, which drives the real
      firmware over RTPS and does need a board.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent.resolve()
BUILD_DIR = SCRIPT_DIR / "build"
TARGET = "pace-hmi-sim"

IS_WINDOWS = sys.platform == "win32"
IS_LINUX = sys.platform.startswith("linux")
IS_MACOS = sys.platform == "darwin"


def check_platform():
    """
    Check that the simulator supports this OS.

    The display backend is LVGL's native Win32 driver, chosen so the sim needs
    nothing installed beyond a compiler -- no SDL, no vcpkg. That choice is
    what makes it Windows-only today. Porting to Linux/macOS means adding
    LVGL's SDL backend in ``sim/CMakeLists.txt`` and ``sim/lv_conf.h``
    alongside the Win32 one; nothing in ``main/ui/`` would have to change.

    Returns:
        True if this platform is supported.
    """
    if IS_WINDOWS:
        return True

    other = "Linux" if IS_LINUX else "macOS" if IS_MACOS else sys.platform
    print(f"ERROR: the simulator is Windows-only right now (this is {other}).")
    print()
    print("  It renders through LVGL's Win32 backend so that it needs no")
    print("  external graphics dependency. Adding LVGL's SDL backend would")
    print("  lift this; see the note in check_platform() in this file.")
    return False


def _find_compiler():
    """
    Pick a C/C++ compiler for CMake.

    Returns a ``(c_compiler, cxx_compiler)`` pair of executable names, or
    ``(None, None)`` to let CMake choose for itself (which is what we want
    when only MSVC is installed -- CMake finds ``cl.exe`` through the Visual
    Studio environment, and naming it explicitly here would break unless we
    were already inside a Developer Command Prompt).
    """
    if shutil.which("clang-cl"):
        return "clang-cl", "clang-cl"
    return None, None


def check_toolchain():
    """
    Check for CMake, a generator and a compiler.

    Returns:
        ``(ok, generator)`` -- ``ok`` is False if something required is
        missing, and ``generator`` is the CMake generator name to use, or
        None to accept CMake's default.
    """
    if not shutil.which("cmake"):
        print("ERROR: CMake not found on PATH.")
        print()
        print("  Install with:")
        print("    winget install Kitware.CMake")
        return False, None

    # Ninja is much faster than the Visual Studio generator for a build this
    # size, but it is not required -- CMake's default works fine.
    generator = "Ninja" if shutil.which("ninja") else None
    if generator is None:
        print("NOTE: Ninja not found; falling back to CMake's default generator.")
        print("      'winget install Ninja-build.Ninja' makes builds noticeably faster.")
        print()

    have_compiler = (
        shutil.which("clang-cl")
        or shutil.which("cl")
        or Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)"),
                "Microsoft Visual Studio").exists()
        or Path(os.environ.get("ProgramFiles", "C:/Program Files"),
                "Microsoft Visual Studio").exists()
    )
    if not have_compiler:
        print("ERROR: no C/C++ compiler found.")
        print()
        print("  Install either:")
        print("    winget install LLVM.LLVM                      (clang-cl)")
        print("    winget install Microsoft.VisualStudio.2022.BuildTools")
        return False, None

    return True, generator


def configure(generator, verbose):
    """
    Run the CMake configure step if the build dir has no cache yet.

    This is where LVGL 9.5 is fetched, so the first run needs a network
    connection and takes longer than the ones after it.

    Returns:
        True on success.
    """
    if (BUILD_DIR / "CMakeCache.txt").exists():
        return True

    print("Configuring (this fetches LVGL, so it needs the network once)...")
    cmd = ["cmake", "-S", str(SCRIPT_DIR), "-B", str(BUILD_DIR)]
    if generator:
        cmd += ["-G", generator]

    c_compiler, cxx_compiler = _find_compiler()
    if c_compiler:
        cmd += [f"-DCMAKE_C_COMPILER={c_compiler}", f"-DCMAKE_CXX_COMPILER={cxx_compiler}"]

    return _run(cmd, verbose, "configure")


def build(verbose):
    """
    Compile the simulator.

    Returns:
        True on success.
    """
    print("Building...")
    cmd = ["cmake", "--build", str(BUILD_DIR), "--target", TARGET]
    # Multi-config generators (Visual Studio) need the config naming the
    # build type that sim/CMakeLists.txt defaults to.
    if not (BUILD_DIR / "build.ninja").exists():
        cmd += ["--config", "RelWithDebInfo"]
    return _run(cmd, verbose, "build")


def _run(cmd, verbose, what):
    """
    Run a subprocess, printing its output only if it is wanted or it failed.

    A successful build is noise; a failed one is the whole point. So the
    output is captured and replayed on failure, unless --verbose asked for it
    live.
    """
    if verbose:
        result = subprocess.run(cmd, cwd=SCRIPT_DIR)
        return result.returncode == 0

    result = subprocess.run(cmd, cwd=SCRIPT_DIR, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"\nERROR: {what} failed.\n")
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        return False
    return True


def find_executable():
    """
    Locate the built binary.

    Single-config generators put it at the top of the build dir; multi-config
    ones nest it under the configuration name.

    Returns:
        Path to the executable, or None if it is not there.
    """
    name = f"{TARGET}.exe" if IS_WINDOWS else TARGET
    for candidate in (BUILD_DIR / name, BUILD_DIR / "RelWithDebInfo" / name):
        if candidate.exists():
            return candidate

    matches = list(BUILD_DIR.rglob(name))
    return matches[0] if matches else None


def launch(passthrough):
    """
    Run the simulator, forwarding any extra arguments to it.

    Returns:
        The simulator's exit code.
    """
    exe = find_executable()
    if exe is None:
        print(f"ERROR: built successfully but could not find {TARGET} under {BUILD_DIR}")
        return 1

    print(f"Launching {exe.name}...")
    print()
    return subprocess.run([str(exe), *passthrough], cwd=SCRIPT_DIR).returncode


def main():
    parser = argparse.ArgumentParser(
        description="Build and launch the RAMMP HMI simulator.",
        epilog="Any other arguments are passed through to the simulator itself.",
    )
    parser.add_argument("--rebuild", action="store_true",
                        help="delete the build directory and start from scratch")
    parser.add_argument("--build-only", action="store_true",
                        help="compile without launching")
    parser.add_argument("--verbose", action="store_true",
                        help="show the full CMake and compiler output")
    parser.add_argument("--zoom", type=int, metavar="PERCENT",
                        help="window scale; 100 is the panel's true 720x1280 (default 50)")
    args, passthrough = parser.parse_known_args()

    print("=" * 62)
    print("  RAMMP HMI Simulator")
    print("=" * 62)
    print()

    if not check_platform():
        return 1

    ok, generator = check_toolchain()
    if not ok:
        return 1

    if args.rebuild and BUILD_DIR.exists():
        print(f"Removing {BUILD_DIR}...")
        shutil.rmtree(BUILD_DIR)

    if not configure(generator, args.verbose):
        return 1
    if not build(args.verbose):
        return 1

    if args.build_only:
        exe = find_executable()
        print(f"Built {exe}" if exe else "Built.")
        return 0

    if args.zoom is not None:
        passthrough = ["--zoom", str(args.zoom), *passthrough]

    try:
        return launch(passthrough)
    except KeyboardInterrupt:
        print("\nClosed.")
        return 0


if __name__ == "__main__":
    sys.exit(main())
