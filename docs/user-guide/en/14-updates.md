<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# 14. Updates

FurnitureOS works fully offline. Checking for updates is optional, and an update is never installed
without your confirmation. No personal data and no usage data are sent: the check is one anonymous request
for the public list of FurnitureOS releases on GitHub.

## Checking

* **Help ▸ Check for updates…** checks now, whatever the setting below.
* **Check for updates at startup** (Edit ▸ Preferences): on the first start FurnitureOS asks once whether
  it may check automatically. If you answer *No*, it never checks by itself; you can change your mind in
  Preferences. An automatic check that fails (no network, GitHub unreachable) only shows a note in the status
  bar.
* **Update channel** (Preferences): *Stable releases only* (default) or *Include pre-releases (beta)*.

## When an update is available

The dialog shows the new version, its release notes and the download size:

* **Update now** downloads the update with a progress bar (you can cancel), checks its size and SHA-256
  checksum against two published lists, and refuses and deletes the file if anything does not match.
* **Later** asks again at the next check.
* **Skip this version** stops automatic checks from offering this version (Help ▸ Check for updates… still
  shows it).

Before installing, FurnitureOS closes; you are asked about unsaved changes first, and you can still cancel.

## How the update is installed

| Your copy | What happens |
|---|---|
| Windows, installed with `setup.exe` | The new installer runs silently after FurnitureOS has closed, then FurnitureOS starts again. |
| Windows, portable folder (ZIP) | The new version is unpacked into a folder next to yours (`FurnitureOS.update-X.Y.Z`). After FurnitureOS has closed, your folder is renamed to `….old`, the new one takes its place, and FurnitureOS starts again. If anything fails, your old folder is put back. The old folder is deleted only after the new one is in place. |
| Linux and other systems | FurnitureOS cannot replace itself. **Open release page** shows where to download the new version; update through your package manager or rebuild from source. |

Settings, libraries, components and autosaves are kept: they live in your user profile, not in the program
folder. The next start reports whether the update succeeded (or why it did not).

A portable folder must be in a location you can write to (not, for example, `C:\Program Files`); otherwise
download the new ZIP yourself.
