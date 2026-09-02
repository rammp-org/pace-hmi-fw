#!/usr/bin/env python3
"""Copy the flashable build artifacts into precompiled/ so anyone who can't build
the firmware can still flash the exact same image.

Run automatically after every `idf.py build` (see the `precompiled` target in the
top-level CMakeLists.txt); flash the result with flash_precompiled.ps1.

Everything is read out of build/flasher_args.json rather than hard-coded, so a
change to partitions.csv or to the flash mode/size flows through on its own. The
files are flattened into precompiled/ (no bootloader/ and partition_table/
subdirectories) and a matching flash_args is written beside them, which is what
lets esptool be pointed at "@flash_args" from inside that one folder.

precompiled/ is gitignored: it is build output, not source. A file is only rewritten
when its SHA256 actually changed, so rebuilding without touching any source leaves
the folder's mtimes alone.
"""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from datetime import datetime, timezone


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def copy_if_changed(src, dst):
    """Copy src over dst unless they already hold the same bytes.

    Returns the destination's SHA256. Skipping the copy keeps mtimes stable, so an
    unchanged rebuild does not churn the folder or retrigger anything watching it.
    """
    digest = sha256(src)
    if os.path.isfile(dst) and sha256(dst) == digest:
        return digest
    shutil.copyfile(src, dst)
    return digest


def write_if_changed(path, text, volatile_prefix=None):
    """Write text to path unless the file already says the same thing.

    Lines starting with volatile_prefix are ignored in that comparison: the
    manifest's build timestamp changes on every build, and rewriting the file for
    that alone would churn its mtime even when the binaries are identical.
    """
    if os.path.isfile(path):
        with open(path, encoding="utf-8") as f:
            old = f.read()

        def significant(s):
            if not volatile_prefix:
                return s
            return [ln for ln in s.splitlines() if not ln.startswith(volatile_prefix)]

        if significant(old) == significant(text):
            return
    with open(path, "w", newline="\n", encoding="utf-8") as f:
        f.write(text)


def git_output(args, cwd):
    try:
        out = subprocess.run(
            ["git"] + args, cwd=cwd, capture_output=True, text=True, timeout=10, check=False
        )
    except (OSError, subprocess.SubprocessError):
        return ""
    return out.stdout.strip() if out.returncode == 0 else ""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, help="the CMake binary dir")
    parser.add_argument("--out-dir", required=True, help="destination, normally precompiled/")
    args = parser.parse_args()

    build_dir = os.path.abspath(args.build_dir)
    out_dir = os.path.abspath(args.out_dir)
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    with open(os.path.join(build_dir, "flasher_args.json"), encoding="utf-8") as f:
        flasher = json.load(f)
    with open(os.path.join(build_dir, "project_description.json"), encoding="utf-8") as f:
        project = json.load(f)

    os.makedirs(out_dir, exist_ok=True)

    # offset -> build-relative path, e.g. "0x2000" -> "bootloader/bootloader.bin".
    # Sorted numerically so flash_args reads bottom-of-flash first, like IDF's own.
    flash_files = sorted(
        flasher["flash_files"].items(), key=lambda item: int(item[0], 0)
    )

    hashes = []
    lines = [" ".join(flasher["write_flash_args"])]
    for offset, rel_path in flash_files:
        src = os.path.join(build_dir, rel_path)
        name = os.path.basename(rel_path)
        digest = copy_if_changed(src, os.path.join(out_dir, name))
        hashes.append((name, os.path.getsize(src), digest))
        lines.append(f"{offset} {name}")

    # The .elf is here so a crash on a flashed board can be
    # decoded locally against that exact image. CI already publishes it for
    # everyone else (see .github/workflows/package_main.yml).
    elf = os.path.join(build_dir, project["project_name"] + ".elf")
    if os.path.isfile(elf):
        copy_if_changed(elf, os.path.join(out_dir, os.path.basename(elf)))

    write_if_changed(os.path.join(out_dir, "flash_args"), "\n".join(lines) + "\n")

    manifest = [
        f"project:    {project['project_name']}",
        f"version:    {project['project_version']}",
        f"commit:     {git_output(['rev-parse', 'HEAD'], repo_root) or 'unknown'}",
        f"branch:     {git_output(['rev-parse', '--abbrev-ref', 'HEAD'], repo_root) or 'unknown'}",
        f"dirty:      {'yes' if git_output(['status', '--porcelain'], repo_root) else 'no'}",
        f"built:      {datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC')}",
        f"target:     {project['target']}",
        f"esp-idf:    {project.get('git_revision', 'unknown')}",
        "",
        "sha256:",
    ]
    manifest += [f"  {digest}  {name}" for name, _, digest in hashes]

    write_if_changed(
        os.path.join(out_dir, "manifest.txt"),
        "\n".join(manifest) + "\n",
        volatile_prefix="built:",
    )

    print(
        f"precompiled: {project['project_version']} -> "
        f"{os.path.relpath(out_dir, repo_root)} "
        f"({', '.join(name for name, _, _ in hashes)})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
