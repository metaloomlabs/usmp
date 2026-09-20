#!/usr/bin/env python3
"""scripts/bundle-ports.py

Cross-platform utility to transiently bundle core protocol C engine sources into
USMP port directories (ports/usmp-arduino and ports/usmp-esp32) for release packaging
or test compilation.

Supports:
  python scripts/bundle-ports.py --target arduino [--zip]
  python scripts/bundle-ports.py --target esp32
  python scripts/bundle-ports.py --clean
"""

import argparse
import os
import re
import shutil
import sys
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CORE_DIR = REPO_ROOT / "core"
ARDUINO_PORT_DIR = REPO_ROOT / "ports" / "usmp-arduino"
ESP32_PORT_DIR = REPO_ROOT / "ports" / "usmp-esp32"
LICENSE_FILE = REPO_ROOT / "LICENSE"

# Shim content for ports/usmp-arduino/src/usmp_api.h
ARDUINO_SHIM_CONTENT = """#pragma once

/*
 * usmp_api.h — repo-local shim; NOT the header that ships.
 *
 * The Arduino wrappers include "usmp_api.h" because the packaged library renames
 * the core public header (core/include/usmp.h) to usmp_api.h at build time, to
 * dodge the USMP.h vs usmp.h collision on case-insensitive filesystems.
 *
 * In the repo this file forwards to the real core header, so there is ONE source
 * of truth and nothing to hand-sync. At package time
 * scripts/bundle-ports.py OVERWRITES this shim with the actual
 * core/include/usmp.h, so the forward below never ships — it exists only for
 * editor indexing and standalone compilation.
 */
#ifndef USMP_TEST_MAIN
#include "../../../core/include/usmp.h"
#endif
"""

# Native files that belong to ports/usmp-arduino/src (never delete these on clean)
NATIVE_ARDUINO_SRC_FILES = {
    "USMP.cpp",
    "USMP.h",
    "USMPTransport.cpp",
    "USMPTransport.h",
    "usmp_api.h",
    "usmp_port_arduino.cpp",
}


def bundle_arduino(create_zip: bool = False) -> None:
    """Bundle core C/H sources into ports/usmp-arduino/src/ and optionally create zip."""
    src_dir = ARDUINO_PORT_DIR / "src"
    if not src_dir.exists():
        raise RuntimeError(f"Arduino src directory not found at: {src_dir}")

    print("[USMP] Bundling core sources into ports/usmp-arduino/src/...")

    # 1. Copy core/src C/H files (excluding tests)
    core_src = CORE_DIR / "src"
    for f in core_src.glob("*"):
        if f.is_file() and f.suffix in [".c", ".h"]:
            dest = src_dir / f.name
            shutil.copy2(f, dest)

    # 2. Copy core/include public headers
    core_inc = CORE_DIR / "include"
    for f in core_inc.glob("*.h"):
        if f.is_file():
            # usmp.h is specially renamed to usmp_api.h to prevent case collisions with USMP.h
            if f.name == "usmp.h":
                shutil.copy2(f, src_dir / "usmp_api.h")
            else:
                shutil.copy2(f, src_dir / f.name)

    # 3. Patch #include "usmp.h" -> #include "usmp_api.h" across all staged C/H files
    include_pattern = re.compile(r'#include\s+["<]usmp\.h[">]')
    for f in src_dir.glob("*"):
        if f.is_file() and f.suffix in [".c", ".h", ".cpp"]:
            try:
                content = f.read_text(encoding="utf-8")
                if include_pattern.search(content):
                    patched = include_pattern.sub('#include "usmp_api.h"', content)
                    f.write_text(patched, encoding="utf-8")
            except OSError as e:
                print(f"  Warning patching {f.name}: {e}")

    print("  Arduino bundling complete.")

    if create_zip:
        version = "unknown"
        prop_file = ARDUINO_PORT_DIR / "library.properties"
        if prop_file.exists():
            for line in prop_file.read_text(encoding="utf-8").splitlines():
                if line.startswith("version="):
                    version = line.split("=", 1)[1].strip()
                    break

        zip_path = REPO_ROOT / f"usmp-{version}-arduino.zip"
        print(f"[USMP] Creating offline library zip: {zip_path.name}...")
        with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
            for root, _, files in os.walk(ARDUINO_PORT_DIR):
                for file in files:
                    full_path = Path(root) / file
                    rel_path = full_path.relative_to(ARDUINO_PORT_DIR)
                    arc_name = Path("usmp-arduino") / rel_path
                    zf.write(full_path, arc_name)
        print(f"  Zip created: {zip_path}")


def bundle_esp32() -> None:
    """Bundle core C/H sources into ports/usmp-esp32/core/ and copy LICENSE."""
    core_dest = ESP32_PORT_DIR / "core"
    print("[USMP] Bundling core sources into ports/usmp-esp32/core/...")

    if core_dest.exists():
        shutil.rmtree(core_dest)

    # Copy core/src and core/include
    shutil.copytree(CORE_DIR / "src", core_dest / "src", ignore=shutil.ignore_patterns("tests"))
    shutil.copytree(CORE_DIR / "include", core_dest / "include")

    # Copy LICENSE if exists
    if LICENSE_FILE.exists():
        shutil.copy2(LICENSE_FILE, ESP32_PORT_DIR / "LICENSE")

    print("  ESP-IDF bundling complete.")


def clean_ports() -> None:
    """Clean all bundled core files from both port directories."""
    print("[USMP] Cleaning bundled files from port directories...")

    # 1. Clean Arduino port src/
    src_dir = ARDUINO_PORT_DIR / "src"
    if src_dir.exists():
        for f in src_dir.glob("*"):
            if f.is_file() and f.name not in NATIVE_ARDUINO_SRC_FILES:
                f.unlink()
                print(f"  Removed {f.name} from ports/usmp-arduino/src/")

        # Restore usmp_api.h shim
        shim_file = src_dir / "usmp_api.h"
        shim_file.write_text(ARDUINO_SHIM_CONTENT, encoding="utf-8")
        print("  Restored ports/usmp-arduino/src/usmp_api.h shim.")

    # 2. Clean ESP32 port
    core_dest = ESP32_PORT_DIR / "core"
    if core_dest.exists():
        shutil.rmtree(core_dest)
        print("  Removed ports/usmp-esp32/core/")

    license_dest = ESP32_PORT_DIR / "LICENSE"
    if license_dest.exists():
        license_dest.unlink()
        print("  Removed ports/usmp-esp32/LICENSE")

    print("[USMP] Ports cleaned successfully.")


def main() -> int:
    parser = argparse.ArgumentParser(description="USMP Port Bundling Utility")
    parser.add_argument(
        "--target",
        choices=["arduino", "esp32", "all"],
        help="Target port to bundle (arduino, esp32, or all)",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Clean all bundled core files and restore shims",
    )
    parser.add_argument(
        "--zip",
        action="store_true",
        help="Create offline ZIP archive (applicable to --target arduino)",
    )

    args = parser.parse_args()

    if args.clean:
        clean_ports()
        return 0

    if not args.target:
        parser.print_help()
        return 1

    if args.target in ["arduino", "all"]:
        bundle_arduino(create_zip=args.zip)
    if args.target in ["esp32", "all"]:
        bundle_esp32()

    return 0


if __name__ == "__main__":
    sys.exit(main())
