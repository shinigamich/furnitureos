#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Write SHA256SUMS.txt and update.json (and optionally the release notes) for a FurnitureOS release.

update.json is read by the in-app updater; its format is fixed:
  {"version": "X.Y.Z[-suffix]", "published": "YYYY-MM-DD", "notes": "<markdown>",
   "assets": {"windows-x64-zip":   {"name": ..., "sha256": ..., "size": ...},
              "windows-x64-setup": {"name": ..., "sha256": ..., "size": ...}}}
Notes come from the CHANGELOG.md section "## [X.Y.Z...]", otherwise from the tag message.
"""
import argparse
import datetime
import hashlib
import json
import re
import sys
from pathlib import Path

UPDATE_ASSETS = {
    "windows-x64-zip": "FurnitureOS-{v}-windows-x64.zip",
    "windows-x64-setup": "FurnitureOS-{v}-windows-x64-setup.exe",
}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def changelog_section(changelog: Path, version: str) -> str:
    """Body of '## [version] ...' up to the next '## ' heading (empty if absent)."""
    if not changelog.is_file():
        return ""
    lines = changelog.read_text(encoding="utf-8").splitlines()
    heading = re.compile(r"^##\s+\[?" + re.escape(version) + r"\]?(\s|$)")
    body, inside = [], False
    for line in lines:
        if line.startswith("## "):
            if inside:
                break
            inside = bool(heading.match(line))
            continue
        if inside:
            body.append(line)
    # Drop link reference definitions ("[0.1.0]: https://...") that belong to the whole file.
    body = [l for l in body if not re.match(r"^\[[^\]]+\]:\s*\S+", l)]
    return "\n".join(body).strip()


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--version", required=True, help="X.Y.Z[-suffix]")
    p.add_argument("--assets-dir", type=Path, required=True,
                   help="folder with all release files; outputs are written here too")
    p.add_argument("--changelog", type=Path, default=Path("CHANGELOG.md"))
    p.add_argument("--tag-message", type=Path, help="file with the annotated tag message (fallback notes)")
    p.add_argument("--notes-out", type=Path, help="also write the release notes (markdown) here")
    p.add_argument("--date", help="publication date YYYY-MM-DD (default: today, UTC)")
    a = p.parse_args()

    outputs = {"SHA256SUMS.txt", "update.json"}
    files = sorted(f for f in a.assets_dir.iterdir() if f.is_file() and f.name not in outputs)
    if not files:
        print(f"error: no release files in {a.assets_dir}", file=sys.stderr)
        return 1
    digests = {f.name: sha256(f) for f in files}

    assets = {}
    for key, pattern in UPDATE_ASSETS.items():
        name = pattern.format(v=a.version)
        path = a.assets_dir / name
        if not path.is_file():
            print(f"error: expected release file '{name}' is missing from {a.assets_dir}", file=sys.stderr)
            return 1
        assets[key] = {"name": name, "sha256": digests[name], "size": path.stat().st_size}

    notes = changelog_section(a.changelog, a.version)
    if not notes and a.tag_message and a.tag_message.is_file():
        notes = a.tag_message.read_text(encoding="utf-8").strip()
        # Strip an attached PGP/SSH signature, if any.
        notes = re.split(r"\n-----BEGIN (PGP|SSH) SIGNATURE-----", notes)[0].strip()
    if not notes:
        print(f"error: no release notes: add a '## [{a.version}]' section to {a.changelog} "
              f"or use an annotated tag with a message", file=sys.stderr)
        return 1

    published = a.date or datetime.datetime.now(datetime.timezone.utc).date().isoformat()
    datetime.date.fromisoformat(published)  # validates the format

    # Two spaces between hash and name: the format `sha256sum -c` and Get-FileHash users expect.
    (a.assets_dir / "SHA256SUMS.txt").write_text(
        "".join(f"{digests[n]}  {n}\n" for n in sorted(digests)), encoding="utf-8", newline="\n")
    update = {"version": a.version, "published": published, "notes": notes, "assets": assets}
    (a.assets_dir / "update.json").write_text(
        json.dumps(update, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    if a.notes_out:
        a.notes_out.write_text(notes + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(update, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
