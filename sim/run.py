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

1. Checks for CMake, a build generator and the MSVC toolchain, and tells you
   what to install if one is missing. It finds Visual Studio itself, so it
   works from a plain shell -- and it ignores an ESP-IDF environment if one
   happens to be loaded, since that cross toolchain cannot build a host
   binary.
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
import re
import shutil
import stat
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


def _force_rmtree(path):
    """
    Delete a directory tree, clearing read-only bits on the way.

    Git marks the objects in a pack read-only, and build/_deps holds a whole
    LVGL clone of them, so a plain rmtree stops at the first one with 'Access
    is denied' on Windows.
    """
    def clear_readonly(func, target, _exc):
        os.chmod(target, stat.S_IWRITE)
        func(target)

    # onerror is what onexc replaced in 3.12; passing the old one there warns.
    handler = "onexc" if sys.version_info >= (3, 12) else "onerror"
    shutil.rmtree(path, **{handler: clear_readonly})


def _repair_path_entry(entry):
    """
    Rejoin a PATH entry that has a line break wrapped into the middle of it.

    Windows will happily store a newline in PATH if one is pasted into the
    environment editor, and it stays invisible until something parses PATH as
    text. vcvars64.bat is one such thing: the newline ends its ``set PATH=``
    line and the remainder is read as a command, which fails with a message
    ('\\Common was unexpected at this time') that names neither PATH nor the
    entry at fault.
    """
    return re.sub(r"\s*[\r\n]+\s*", "", entry)


def _sanitized_env():
    """
    Copy the environment with ESP-IDF's cross toolchain stripped out of PATH.

    The sim is a host build with nothing to do with ESP-IDF, but the firmware's
    PowerShell profile puts Espressif's cross-clang on PATH -- and CMake, left
    to choose, picks that clang over MSVC. It compiles and then fails at link
    with ``unable to find library -lkernel32``, because there is no Windows SDK
    behind it. So drop Espressif before CMake ever looks.

    This is also why check_toolchain() resolves cmake and ninja to absolute
    paths: on a machine set up for the firmware, those are Espressif's copies
    and would vanish along with everything else.
    """
    env = dict(os.environ)
    env.pop("CC", None)
    env.pop("CXX", None)

    entries = [_repair_path_entry(entry)
               for entry in env.get("PATH", "").split(os.pathsep)]
    if os.pathsep.join(entries) != env.get("PATH", ""):
        print("NOTE: your PATH has a line break inside one of its entries.")
        print("      Working around it here, but it will break other tools;")
        print("      worth fixing in the Windows environment editor.")
        print()

    env["PATH"] = os.pathsep.join(
        entry for entry in entries
        if "espressif" not in entry.lower() and "esp-idf" not in entry.lower()
    )
    return env


def _esp_tool(pattern):
    """
    Find a host build tool in the ESP-IDF tools directory.

    CMake and Ninja there are ordinary Windows binaries -- only the compiler
    in the ESP-IDF toolchain is a cross compiler -- so on a machine set up for
    the firmware there is no reason to make anyone install a second copy. They
    have to be found by absolute path because _sanitized_env() takes Espressif
    back off PATH before CMake runs.

    Returns:
        Path to the newest match as a string, or None.
    """
    roots = [os.environ.get("IDF_TOOLS_PATH"),
             r"C:\Espressif\tools",
             str(Path.home() / ".espressif" / "tools")]
    for root in roots:
        if root and (found := sorted(Path(root).glob(pattern))):
            return str(found[-1])
    return None


def _msvc_env(env):
    """
    Layer the MSVC toolchain onto ``env``.

    The Ninja generator cannot find ``cl.exe`` by itself -- it expects to be
    run from a Developer Command Prompt. Rather than make that the user's
    problem, locate Visual Studio with vswhere and import whatever
    ``vcvars64.bat`` sets.

    Returns:
        The environment with MSVC on it, or None if no VS C++ toolset is
        installed.
    """
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)"),
                   "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not vswhere.exists():
        return None

    found = subprocess.run(
        [str(vswhere), "-latest", "-products", "*",
         "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
         "-property", "installationPath"],
        capture_output=True, text=True)
    roots = found.stdout.strip().splitlines()
    if found.returncode != 0 or not roots:
        return None

    vcvars = Path(roots[0], "VC", "Auxiliary", "Build", "vcvars64.bat")
    if not vcvars.exists():
        return None

    # A string with shell=True, not an argument list: the list form escapes
    # the quotes around the (space-containing) path as \" , which cmd does not
    # understand and reports as 'not recognized as an internal command'.
    dumped = subprocess.run(f'call "{vcvars}" >nul && set',
                            shell=True, capture_output=True, text=True, env=env)
    if dumped.returncode != 0:
        return None

    for line in dumped.stdout.splitlines():
        # Upper-cased because os.environ already is on Windows, and a dict
        # holding both "PATH" and "Path" hands the child two of them.
        key, sep, value = line.partition("=")
        if sep:
            env[key.upper()] = value
    return env


def check_toolchain():
    """
    Check for CMake, a generator and the MSVC toolchain.

    Everything is resolved to an absolute path here, because the build runs in
    an environment with ESP-IDF stripped out of PATH -- see _sanitized_env().

    Returns:
        ``(ok, tools)`` -- ``ok`` is False if something required is missing,
        and ``tools`` is what configure() and build() need to run CMake.
    """
    cmake = shutil.which("cmake") or _esp_tool("cmake/*/bin/cmake.exe")
    if not cmake:
        print("ERROR: CMake not found on PATH.")
        print()
        print("  Install with:")
        print("    winget install Kitware.CMake")
        return False, None

    # Ninja is much faster than the Visual Studio generator for a build this
    # size, but it is not required -- CMake's default works fine.
    ninja = shutil.which("ninja") or _esp_tool("ninja/*/ninja.exe")
    if ninja is None:
        print("NOTE: Ninja not found; falling back to CMake's default generator.")
        print("      'winget install Ninja-build.Ninja' makes builds noticeably faster.")
        print()

    env = _msvc_env(_sanitized_env())
    if env is None:
        print("ERROR: no MSVC C/C++ toolchain found.")
        print()
        print("  Install with:")
        print("    winget install Microsoft.VisualStudio.2022.BuildTools")
        print("  and select the 'Desktop development with C++' workload.")
        return False, None

    return True, {
        "cmake": cmake,
        "ninja": ninja,
        "generator": "Ninja" if ninja else None,
        "env": env,
    }


def configure(tools, verbose):
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
    cmd = [tools["cmake"], "-S", str(SCRIPT_DIR), "-B", str(BUILD_DIR)]
    if tools["generator"]:
        cmd += ["-G", tools["generator"], f"-DCMAKE_MAKE_PROGRAM={tools['ninja']}"]

    if _run(cmd, tools["env"], verbose, "configure"):
        return True

    # CMake writes its cache even when configure fails, and that cache pins
    # the compiler it failed with. Clear it, or the next run skips configure
    # and goes straight to a doomed build.
    try:
        _force_rmtree(BUILD_DIR)
    except OSError:
        print(f"NOTE: could not clear {BUILD_DIR}; delete it before retrying.")
    return False


def build(tools, verbose):
    """
    Compile the simulator.

    Returns:
        True on success.
    """
    print("Building...")
    cmd = [tools["cmake"], "--build", str(BUILD_DIR), "--target", TARGET]
    # Multi-config generators (Visual Studio) need the config naming the
    # build type that sim/CMakeLists.txt defaults to.
    if not (BUILD_DIR / "build.ninja").exists():
        cmd += ["--config", "RelWithDebInfo"]
    return _run(cmd, tools["env"], verbose, "build")


def _run(cmd, env, verbose, what):
    """
    Run a subprocess, printing its output only if it is wanted or it failed.

    A successful build is noise; a failed one is the whole point. So the
    output is captured and replayed on failure, unless --verbose asked for it
    live.
    """
    if verbose:
        result = subprocess.run(cmd, cwd=SCRIPT_DIR, env=env)
        return result.returncode == 0

    result = subprocess.run(cmd, cwd=SCRIPT_DIR, capture_output=True, text=True, env=env)
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

    ok, tools = check_toolchain()
    if not ok:
        return 1

    if args.rebuild and BUILD_DIR.exists():
        print(f"Removing {BUILD_DIR}...")
        _force_rmtree(BUILD_DIR)

    if not configure(tools, args.verbose):
        return 1
    if not build(tools, args.verbose):
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
