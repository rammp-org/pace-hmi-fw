#!/usr/bin/env python3
"""Fetch and build what the ROS 2 stress bench needs (run through pixi: `pixi run setup`).

- rammp-interfaces-ros2 2.1.0: the message packages the orchestrator mocks speak, and
  rammp_peripheral_interfaces for the joystick.
- rammp-orchestrator#1 (feat/orchestrator): the orchestrator and its mocks.

The workspace lives at a short path (RAMMP_STRESS_WS, default C:/w/r2 on Windows, as
MSVC and rosidl's generated paths do not mix well with long ones; ./ws elsewhere).
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
WS = os.environ.get("RAMMP_STRESS_WS") or ("C:/w/r2" if os.name == "nt" else os.path.join(HERE, "ws"))
SOURCES = [
    ("rammp-interfaces-ros2", "https://github.com/rammp-org/rammp-interfaces-ros2.git", "6e50dff"),
    ("rammp-orchestrator", "https://github.com/rammp-org/rammp-orchestrator.git", "78443f0"),
]


def run(*cmd: str, cwd: str | None = None) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=cwd, check=True)


def main() -> int:
    src = os.path.join(WS, "src")
    os.makedirs(src, exist_ok=True)
    for name, url, rev in SOURCES:
        path = os.path.join(src, name)
        if not os.path.isdir(path):
            run("git", "clone", "-q", url, path)
        run("git", "-C", path, "fetch", "-q", "origin", "+refs/heads/*:refs/remotes/origin/*")
        run("git", "-C", path, "checkout", "-q", rev)
    run("colcon", "build", "--merge-install",
        "--packages-up-to", "rammp_orchestrator", "rammp_peripheral_interfaces",
        "--cmake-args", "-DBUILD_TESTING=OFF", "-DCMAKE_BUILD_TYPE=Release", "-G", "Ninja", cwd=WS)
    print(f"built into {WS}/install", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
