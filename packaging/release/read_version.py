#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Print the FurnitureOS version defined in the root CMakeLists.txt.

The version is project(FurnitureOS VERSION X.Y.Z) plus the default of the cache variable
FOS_VERSION_SUFFIX (e.g. "-beta.1"). With --check-tag vX.Y.Z[-suffix] the script exits 1 unless
the tag is exactly "v" + version. With --github-output the values are appended to $GITHUB_OUTPUT.
"""
import argparse
import os
import re
import sys
from pathlib import Path

SUFFIX_RE = re.compile(r"^(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$")


def read_version(cmakelists: Path) -> tuple[str, str]:
    text = cmakelists.read_text(encoding="utf-8")
    m = re.search(r"project\(\s*FurnitureOS\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)\b", text)
    if not m:
        raise SystemExit(f"error: no 'project(FurnitureOS VERSION X.Y.Z' in {cmakelists}")
    s = re.search(r'set\(\s*FOS_VERSION_SUFFIX\s+"([^"]*)"\s+CACHE\s+STRING', text)
    if not s:
        raise SystemExit(f"error: no 'set(FOS_VERSION_SUFFIX \"...\" CACHE STRING' in {cmakelists}")
    suffix = s.group(1)
    if not SUFFIX_RE.match(suffix):
        raise SystemExit(f"error: FOS_VERSION_SUFFIX '{suffix}' is not a pre-release suffix like -beta.1")
    return m.group(1), suffix


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmakelists", type=Path,
                        default=Path(__file__).resolve().parents[2] / "CMakeLists.txt")
    parser.add_argument("--check-tag", metavar="TAG", help="fail unless TAG == 'v' + version")
    parser.add_argument("--github-output", action="store_true",
                        help="append version/base/suffix/prerelease to $GITHUB_OUTPUT")
    args = parser.parse_args()

    base, suffix = read_version(args.cmakelists)
    version = base + suffix
    if args.check_tag is not None and args.check_tag != "v" + version:
        print(f"error: tag '{args.check_tag}' does not match the CMake version 'v{version}' "
              f"(project VERSION {base}, FOS_VERSION_SUFFIX '{suffix}'). Bump the version in "
              f"CMakeLists.txt or re-tag; see docs/development/releasing.md.", file=sys.stderr)
        return 1
    if args.github_output:
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as out:
            out.write(f"version={version}\nbase={base}\nsuffix={suffix}\n"
                      f"prerelease={'true' if suffix else 'false'}\n")
    print(version)
    return 0


if __name__ == "__main__":
    sys.exit(main())
