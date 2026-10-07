"""Flash a bench build, keep last-good images, restore one.

    python flash.py preflight --build-dir C:\\b\\main_bench
    python flash.py flash     --build-dir C:\\b\\main_bench [--save-as LABEL]
    python flash.py save      --build-dir C:\\b\\main_bench --label LABEL
    python flash.py restore   LABEL
    python flash.py list

Rules (plan §6 B1, profile "Reset to known state"):
- never erase: the only write is `esptool write-flash @flash_args`, no erase options;
- never flash when the board's partition table (read at 0x8000) differs from the
  build's: a different table reformats /storage and loses the joystick calibration;
- only bench builds: config/sdkconfig.h must have `#define CONFIG_HMI_REMOTE_UI 1`.
The table is compared again immediately before every write, restore included.
Last-good images live in C:\\b\\bench\\good\\<label>\\ (bootloader, partition table,
ota_data_initial, app, flash_args), outside the repo.
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import board  # noqa: E402
import common  # noqa: E402

REMOTE_UI_LINE = "#define CONFIG_HMI_REMOTE_UI 1"


def flash_files(image_dir: pathlib.Path) -> list[tuple[str, str]]:
    """(offset, relative path) from flash_args; the first line is the options."""
    lines = (image_dir / "flash_args").read_text(encoding="utf-8").splitlines()
    out = []
    for line in lines[1:]:
        parts = line.split()
        if len(parts) == 2:
            out.append((parts[0], parts[1]))
    return out


def check_bench_config(build_dir: pathlib.Path) -> tuple[bool, str]:
    header = build_dir / "config" / "sdkconfig.h"
    try:
        text = header.read_text(encoding="utf-8", errors="replace")
    except OSError as e:
        return False, f"cannot read {header}: {e}"
    if REMOTE_UI_LINE in text.splitlines():
        return True, f"{REMOTE_UI_LINE} in {header}"
    return False, f"{header} lacks '{REMOTE_UI_LINE}': not a bench build"


def check_images(image_dir: pathlib.Path) -> tuple[bool, str]:
    try:
        files = flash_files(image_dir)
    except OSError as e:
        return False, f"no flash_args: {e}"
    missing = [rel for _, rel in files if not (image_dir / rel).is_file()]
    names = {pathlib.Path(rel).name for _, rel in files}
    need = {"bootloader.bin", "partition-table.bin", "ota_data_initial.bin"}
    if missing:
        return False, f"missing images: {missing}"
    if not need <= names or len(files) < 4:
        return False, f"flash_args lists {sorted(names)}; expected bootloader, table, otadata, app"
    return True, f"{len(files)} images: " + ", ".join(f"{o} {r}" for o, r in files)


def preflight(build_dir: pathlib.Path, port: str) -> list[dict]:
    """The flash-specific B0 checks: every entry {name, ok, detail}."""
    checks = []
    ok, detail = check_images(build_dir)
    checks.append({"name": "images", "ok": ok, "detail": detail})
    ok, detail = check_bench_config(build_dir)
    checks.append({"name": "bench_config", "ok": ok, "detail": detail})
    try:
        ok, detail = board.compare_partition_table(port, build_dir)
    except Exception as e:  # a read that fails is not a match
        ok, detail = False, f"could not read the board's table: {e}"
    checks.append({"name": "partition_table", "ok": ok, "detail": detail})
    return checks


def _write(image_dir: pathlib.Path, port: str) -> tuple[int, str]:
    equal, detail = board.compare_partition_table(port, image_dir)
    if not equal:
        raise SystemExit(f"NOT FLASHING: partition table {detail}")
    r = common.esptool(["--chip", common.CHIP, "-p", port, "-b", str(common.BAUD),
                        "--before", "default-reset", "--after", "hard-reset",
                        "write-flash", "@flash_args"], timeout=900, cwd=str(image_dir))
    return r.returncode, (r.stdout + r.stderr)[-3000:]


def flash(build_dir: pathlib.Path, port: str) -> tuple[int, str]:
    ok, detail = check_bench_config(build_dir)
    if not ok:
        raise SystemExit(f"NOT FLASHING: {detail}")
    return _write(build_dir, port)


def save(build_dir: pathlib.Path, label: str, note: str = "") -> pathlib.Path:
    """Copy the images and flash_args to good/<label>/, keeping flash_args' layout."""
    dest = common.GOOD_DIR / label
    tmp = common.GOOD_DIR / (label + ".tmp")
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp.mkdir(parents=True)
    shutil.copy2(build_dir / "flash_args", tmp / "flash_args")
    for _, rel in flash_files(build_dir):
        (tmp / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(build_dir / rel, tmp / rel)
    header = build_dir / "config" / "sdkconfig.h"
    (tmp / "config").mkdir(exist_ok=True)
    shutil.copy2(header, tmp / "config" / "sdkconfig.h")
    meta = {"label": label, "saved": common.now_iso(), "from": str(build_dir), "note": note}
    try:
        desc = json.loads((build_dir / "project_description.json").read_text(encoding="utf-8"))
        meta["project_path"] = desc.get("project_path")
        meta["version"] = desc.get("project_version")
    except (OSError, ValueError):
        pass
    common.write_json(tmp / "meta.json", meta)
    if dest.exists():
        shutil.rmtree(dest)
    os.replace(tmp, dest)
    return dest


def restore(label: str, port: str) -> tuple[int, str]:
    image_dir = common.GOOD_DIR / label
    ok, detail = check_images(image_dir)
    if not ok:
        raise SystemExit(f"cannot restore {label}: {detail}")
    return _write(image_dir, port)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("preflight", "flash", "save"):
        c = sub.add_parser(name)
        c.add_argument("--build-dir", type=pathlib.Path, required=True)
        if name == "flash":
            c.add_argument("--save-as", default=None)
        if name == "save":
            c.add_argument("--label", required=True)
    r = sub.add_parser("restore")
    r.add_argument("label")
    sub.add_parser("list")
    a = p.parse_args()

    if a.cmd == "list":
        for d in sorted(common.GOOD_DIR.glob("*")):
            if d.is_dir():
                print(d.name, (d / "meta.json").read_text(encoding="utf-8") if (d / "meta.json").exists() else "")
        return 0
    if a.cmd == "save":
        print(save(a.build_dir, a.label))
        return 0
    import lease
    port = board.find_port()
    with lease.held():
        if a.cmd == "preflight":
            checks = preflight(a.build_dir, port)
            for c in checks:
                print(("OK   " if c["ok"] else "FAIL ") + c["name"] + ": " + c["detail"])
            return 0 if all(c["ok"] for c in checks) else 1
        if a.cmd == "flash":
            code, out = flash(a.build_dir, port)
            print(out)
            if code == 0 and a.save_as:
                print("saved", save(a.build_dir, a.save_as))
            return code
        code, out = restore(a.label, port)
        print(out)
        return code


if __name__ == "__main__":
    sys.exit(main())
