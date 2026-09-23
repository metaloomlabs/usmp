#!/usr/bin/env python3
"""scripts/validate-release-tag.py

Pre-flight validation utility for USMP release tagging.
Asserts that the release tag format is valid and that the target manifest
versions match the tag version before slicing and pushing downstream.

Supported tag formats:
  - python-vX.Y.Z   -> Target: Python (sdk/python/pyproject.toml)
  - arduino-vX.Y.Z  -> Target: Arduino (ports/usmp-arduino/library.properties)
  - esp-vX.Y.Z      -> Target: ESP32 (ports/usmp-esp32/idf_component.yml)
  - vX.Y.Z          -> Target: All three manifests simultaneously
"""

import argparse
import os
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def get_manifest_versions() -> dict[str, str]:
    """Extract versions declared across all project manifests."""
    versions: dict[str, str] = {}

    # 1. Python SDK (sdk/python/pyproject.toml)
    pyproject_file = REPO_ROOT / "sdk" / "python" / "pyproject.toml"
    if pyproject_file.exists():
        content = pyproject_file.read_text(encoding="utf-8")
        m = re.search(r'(?m)^version\s*=\s*["\']([^"\']+)["\']', content)
        if m:
            versions["python"] = m.group(1).strip()

    # 2. Arduino Port (ports/usmp-arduino/library.properties)
    arduino_file = REPO_ROOT / "ports" / "usmp-arduino" / "library.properties"
    if arduino_file.exists():
        content = arduino_file.read_text(encoding="utf-8")
        m = re.search(r"(?m)^version\s*=\s*(.+)$", content)
        if m:
            versions["arduino"] = m.group(1).strip()

    # 3. ESP32 Port (ports/usmp-esp32/idf_component.yml)
    esp32_file = REPO_ROOT / "ports" / "usmp-esp32" / "idf_component.yml"
    if esp32_file.exists():
        content = esp32_file.read_text(encoding="utf-8")
        m = re.search(r'(?m)^version\s*:\s*["\']?([^"\'\s]+)["\']?', content)
        if m:
            versions["esp32"] = m.group(1).strip()

    return versions


def parse_tag(raw_tag: str) -> tuple[str, str, str, dict[str, bool]]:
    """Parse tag name into target, clean_tag, semver, and active flags."""
    clean_tag = raw_tag.strip()
    flags = {"python": False, "arduino": False, "esp32": False}

    if clean_tag.startswith("python-v"):
        target = "python"
        clean_tag = clean_tag.removeprefix("python-")
        flags["python"] = True
    elif clean_tag.startswith("arduino-v"):
        target = "arduino"
        clean_tag = clean_tag.removeprefix("arduino-")
        flags["arduino"] = True
    elif clean_tag.startswith("esp-v"):
        target = "esp32"
        clean_tag = clean_tag.removeprefix("esp-")
        flags["esp32"] = True
    elif clean_tag.startswith("v") and "-" not in clean_tag:
        target = "all"
        flags["python"] = True
        flags["arduino"] = True
        flags["esp32"] = True
    else:
        raise ValueError(
            f"Unsupported tag format: '{raw_tag}'. Must match python-v*, arduino-v*, "
            "esp-v*, or v* (without hyphens)."
        )

    semver = clean_tag.removeprefix("v")
    # Basic SemVer format check (X.Y.Z)
    if not re.match(r"^\d+\.\d+\.\d+", semver):
        raise ValueError(f"Extracted version '{semver}' from tag '{raw_tag}' is not valid SemVer.")

    return target, clean_tag, semver, flags


def validate_tag(raw_tag: str, github_output_file: str | None = None) -> int:
    """Validate tag against manifests and optionally write to GITHUB_OUTPUT."""
    print(f"[USMP Pre-Flight] Validating release tag: '{raw_tag}'...")

    try:
        target, clean_tag, semver, flags = parse_tag(raw_tag)
    except ValueError as e:
        print(f"::error::{e}", file=sys.stderr)
        return 1

    print(f"[USMP Pre-Flight] Target: {target}, Downstream Tag: {clean_tag}, SemVer: {semver}")

    manifest_versions = get_manifest_versions()
    errors: list[str] = []

    if flags["python"]:
        py_ver = manifest_versions.get("python")
        if not py_ver:
            errors.append("sdk/python/pyproject.toml missing or has no version declared.")
        elif py_ver != semver:
            errors.append(
                f"sdk/python/pyproject.toml version ('{py_ver}') does not match tag SemVer ('{semver}')."
            )
        else:
            print(f"  [OK] sdk/python/pyproject.toml matches {py_ver}")

    if flags["arduino"]:
        ard_ver = manifest_versions.get("arduino")
        if not ard_ver:
            errors.append("ports/usmp-arduino/library.properties missing or has no version declared.")
        elif ard_ver != semver:
            errors.append(
                f"ports/usmp-arduino/library.properties version ('{ard_ver}') does not match tag SemVer ('{semver}')."
            )
        else:
            print(f"  [OK] ports/usmp-arduino/library.properties matches {ard_ver}")

    if flags["esp32"]:
        esp_ver = manifest_versions.get("esp32")
        if not esp_ver:
            errors.append("ports/usmp-esp32/idf_component.yml missing or has no version declared.")
        elif esp_ver != semver:
            errors.append(
                f"ports/usmp-esp32/idf_component.yml version ('{esp_ver}') does not match tag SemVer ('{semver}')."
            )
        else:
            print(f"  [OK] ports/usmp-esp32/idf_component.yml matches {esp_ver}")

    if errors:
        for err in errors:
            print(f"::error::{err}", file=sys.stderr)
        return 1

    print("[USMP Pre-Flight] All target manifest versions match the tag successfully.")

    if github_output_file:
        try:
            with open(github_output_file, "a", encoding="utf-8") as f:
                f.write(f"tag={clean_tag}\n")
                f.write(f"semver={semver}\n")
                f.write(f"target={target}\n")
                f.write(f"is_python={'true' if flags['python'] else 'false'}\n")
                f.write(f"is_arduino={'true' if flags['arduino'] else 'false'}\n")
                f.write(f"is_esp32={'true' if flags['esp32'] else 'false'}\n")
            print(f"[USMP Pre-Flight] Wrote release outputs to {github_output_file}")
        except OSError as e:
            print(f"::error::Failed writing to GITHUB_OUTPUT: {e}", file=sys.stderr)
            return 1

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="USMP Release Tag Pre-Flight Validator")
    parser.add_argument(
        "--tag",
        default=os.environ.get("RAW_TAG", ""),
        help="Raw git tag (e.g. python-v1.2.1, arduino-v1.2.1, esp-v1.2.1, v1.2.1). Defaults to $RAW_TAG.",
    )
    parser.add_argument(
        "--github-output",
        default=os.environ.get("GITHUB_OUTPUT"),
        help="Path to GITHUB_OUTPUT file. Defaults to $GITHUB_OUTPUT.",
    )

    args = parser.parse_args()

    if not args.tag:
        print(
            "::error::No tag provided. Set RAW_TAG environment variable or pass --tag.",
            file=sys.stderr,
        )
        return 1

    return validate_tag(args.tag, args.github_output)


if __name__ == "__main__":
    sys.exit(main())
