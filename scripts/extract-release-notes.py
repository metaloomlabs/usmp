#!/usr/bin/env python3
"""scripts/extract-release-notes.py

Extracts release notes from CHANGELOG.md for a given git release tag.
Formats the release title and outputs a clean markdown file for GitHub Releases,
supporting smart fallback when changelog entries are missing.

Usage:
    python scripts/extract-release-notes.py --tag v1.2.0
    python scripts/extract-release-notes.py --tag python-v1.3.0 --output dist/notes.md
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def parse_tag_info(raw_tag: str) -> tuple[str, str, str]:
    """Extract semver, component name, and clean title from raw git tag.

    Returns:
        (semver, component, title)
    """
    tag = raw_tag.strip()

    if tag.startswith("python-v"):
        semver = tag.removeprefix("python-v")
        component = "python"
        title = f"USMP Python SDK v{semver}"
    elif tag.startswith("arduino-v"):
        semver = tag.removeprefix("arduino-v")
        component = "arduino"
        title = f"USMP Arduino Port v{semver}"
    elif tag.startswith("esp-v"):
        semver = tag.removeprefix("esp-v")
        component = "esp32"
        title = f"USMP ESP32 Port v{semver}"
    elif tag.startswith("v"):
        semver = tag.removeprefix("v")
        component = "core"
        title = f"USMP v{semver}"
    else:
        semver = tag
        component = "custom"
        title = f"USMP Release {tag}"

    return semver, component, title


def extract_changelog_section(changelog_path: Path, semver: str) -> tuple[str | None, bool]:
    """Extract notes section for semver from CHANGELOG.md.

    Returns:
        (notes_text, found)
    """
    if not changelog_path.exists():
        return None, False

    content = changelog_path.read_text(encoding="utf-8")

    # Match heading like `## [1.2.0]` or `## [1.2.0] — 2026-08-15` or `## [v1.2.0]`
    escaped_ver = re.escape(semver)
    pattern = rf"(?m)^##\s+\[(?:v)?{escaped_ver}\](?:[^\n]*)\n([\s\S]*?)(?=(?:\n##\s+\[|\Z))"
    match = re.search(pattern, content)

    if match:
        body = match.group(1).strip()
        # Remove trailing horizontal rule if present
        body = re.sub(r"\n---\s*$", "", body).strip()
        return body, True

    return None, False


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract release notes from CHANGELOG.md")
    parser.add_argument("--tag", required=True, help="Release tag name (e.g. v1.2.0, python-v1.3.0)")
    parser.add_argument(
        "--changelog",
        default=str(REPO_ROOT / "CHANGELOG.md"),
        help="Path to CHANGELOG.md",
    )
    parser.add_argument(
        "--output",
        default=str(REPO_ROOT / "RELEASE_NOTES.md"),
        help="Output markdown file path",
    )
    parser.add_argument(
        "--github-output",
        default=os.environ.get("GITHUB_OUTPUT", ""),
        help="Path to $GITHUB_OUTPUT file",
    )

    args = parser.parse_args()

    semver, component, title = parse_tag_info(args.tag)
    changelog_path = Path(args.changelog)
    output_path = Path(args.output)

    notes, found = extract_changelog_section(changelog_path, semver)

    if found and notes:
        print(f"[Release Notes] Extracted section for v{semver} from {changelog_path.name}")
        final_notes = notes
    else:
        print(
            f"[Release Notes] No section found for v{semver} in {changelog_path.name}. "
            "Using fallback header."
        )
        final_notes = (
            f"## {title}\n\n"
            f"> [!NOTE]\n"
            f"> Detailed release notes were not found in `CHANGELOG.md` for version `{semver}`. "
            "See the automated pull request changelog below."
        )

    # Ensure parent dir exists and write output file
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(final_notes.strip() + "\n", encoding="utf-8")
    print(f"[Release Notes] Wrote release notes to {output_path}")

    # Set GitHub Actions output parameters if file exists or specified
    gh_output = args.github_output
    if gh_output and os.path.exists(gh_output):
        with open(gh_output, "a", encoding="utf-8") as f:
            f.write(f"title={title}\n")
            f.write(f"semver={semver}\n")
            f.write(f"component={component}\n")
            f.write(f"has_changelog={'true' if found else 'false'}\n")
            f.write(f"notes_file={output_path.resolve().as_posix()}\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())
