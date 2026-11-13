# Plugins API (`fos::plugins`): declarative data plugins

Status: implements **phase 1 of ADR-0007**: declarative data plugins only. A plugin is a directory of data
files with a manifest. **FurnitureOS never executes plugin content**: no shared libraries, scripts,
interpreters or processes. The only contribution kind supported now is **libraries** (`.foslib` files with
materials, edge bands and hardware definitions, read with `formats::importLibrary`, checksums verified).

Headers: `fos/plugins/semver.hpp`, `manifest.hpp`, `package.hpp`, `manager.hpp`. Module dependencies:
`core`, `domain`, `formats`.

## 1. Package layout

```
org.example.oak-boards/          <- directory name MUST equal the plugin id
├── plugin.json                  <- manifest (at most 64 KiB)
├── LICENSE                      <- optional; copied on install (LICENSE*, LICENCE*, COPYING*, README*, NOTICE*)
└── libraries/
    └── oak.foslib               <- contributed library (at most 16 MiB each, 64 MiB in total)
```

## 2. Manifest reference (`plugin.json`, manifestVersion 1)

| Key | Required | Type / rule |
|---|---|---|
| `manifestVersion` | yes | integer `1`. Higher values: "newer than this version of FurnitureOS supports". |
| `id` | yes | plugin id, see section 3 |
| `name` | yes | non-empty string (English display name) |
| `version` | yes | SemVer 2.0.0 string, see section 6 |
| `description` | yes | string (may be empty) |
| `author` | yes | non-empty string |
| `license` | yes | SPDX license identifier (`CC0-1.0`, `MIT`, `GPL-2.0+`, `LicenseRef-Acme`) or a simple expression joined by `AND` / `OR` / `WITH` without parentheses. Syntax only: identifiers are **not** checked against the SPDX list. |
| `homepage` | no | `https://` URL with a host, no whitespace, no `user@`. Other schemes are refused. |
| `compatibility` | yes | object `{ "minAppVersion": "<semver>", "maxAppVersion": "<semver>" (optional) }`; both bounds inclusive; max >= min |
| `permissions` | yes | non-empty array of unique strings. Only `"data"` is defined. Any other permission (e.g. `"network"`) is an **error**: the plugin is not loadable and the message says so. |
| `contributes` | yes | object, see below |
| `localizedNames` | no | object: language tag (`uk`, `en-GB`: 2-3 lower-case letters, then `-` subtags of 2-8 letters/digits) -> non-empty name |

`contributes`:

| Key | Status |
|---|---|
| `libraries` | **supported**: array (at most 32) of unique relative paths of `.foslib` files |
| `templates`, `reports`, `postprocessors`, `importers` | accepted with the warning `contribution kind "<kind>" is not supported by this version of FurnitureOS and is ignored.` (`ExtensionKind`, `isSupported()`) |
| anything else | warning "unknown contribution kind", ignored |

Unknown keys anywhere else in the manifest are **warnings**, not errors (forward compatibility).
Every diagnostic message starts with the JSON pointer of the offending value, e.g.
`/compatibility/minAppVersion: required key "minAppVersion" is missing.` or
`/contributes/libraries/1: "../x.foslib" is not allowed: the path contains '..'; ...`.
Codes are stable: `plugins.manifest.missing_key | wrong_type | empty | bad_id | bad_version | bad_license |
bad_homepage | bad_range | duplicate | unsupported_permission | unsupported_version | bad_path | too_many |
bad_language | too_large | too_deep | parse_error | unknown_key | unsupported_contribution`.

Example (the shipped example plugin, `examples/plugins/org.furnitureos.example-materials/plugin.json`):

```json
{
  "manifestVersion": 1,
  "id": "org.furnitureos.example-materials",
  "name": "Example generic boards and edge bands",
  "version": "1.0.0",
  "description": "Example declarative data plugin ...",
  "author": "FurnitureOS contributors",
  "license": "CC0-1.0",
  "compatibility": { "minAppVersion": "0.1.0" },
  "permissions": ["data"],
  "contributes": { "libraries": ["libraries/generic-boards.foslib"] },
  "localizedNames": { "uk": "Приклад: типові плити та крайки" }
}
```

## 3. Ids

* **Plugin id**: reverse-DNS, lower case, 3 to 128 characters, at least two segments:
  `^[a-z][a-z0-9]*(-[a-z0-9]+)*(\.[a-z][a-z0-9]*(-[a-z0-9]+)*)+$`
  (each segment starts with a letter; hyphens only between letters/digits; no `_`, `:`, `/`, upper case).
* **Contributed entity ids** (materials, edge bands, hardware) **must** be `<plugin-id>:<local-id>` with a
  non-empty local id, e.g. `org.example.oak-boards:oak-18`. A violation is an error and the whole plugin is not
  loaded. Because `:` cannot occur in a plugin id, two plugins can never produce the same entity id, and a plugin
  cannot produce an id of a user-created entity (UUIDs) or of a built-in one.
* An id used twice inside one plugin (same category, across all its files) is an error.

## 4. Safety model

* **No code execution.** Only JSON data is read.
* **Paths stay inside the plugin.** Contributed paths are checked for shape (relative, `/` separators, no
  absolute path, no drive letter or `:`, no backslash, no `..`/`.`/empty component, no control characters)
  and then resolved with symlinks followed; the canonical result must lie strictly inside the canonical plugin
  directory and be a regular file. Symlinks that point outside (files or directories) are rejected; `plugin.json`
  itself is subject to the same rule.
* **Resource limits** (`PluginLimits`, defaults): manifest 64 KiB and nesting depth 32 (checked on the raw text
  before parsing), each library 16 MiB, all libraries of one plugin 64 MiB, at most 32 libraries and 20 000
  entries per plugin. Sizes are checked before a file is read; the manifest read is additionally bounded.
* **Isolation.** Every plugin is validated and loaded independently and all-or-nothing; a broken plugin is reported
  (`Invalid` / `LoadFailed`) and never changes the target library or prevents other plugins from loading.
* **No overwriting.** Libraries are merged with `formats::ConflictPolicy::Skip`: an entry whose id already exists
  in the target is left unchanged (warning `plugins.load.skipped_existing`).
* **Re-validation.** `loadInto()` re-inspects each package immediately before reading it; a package whose id or
  version changed since `discover()` fails with `plugins.load.changed`.
* **Install** validates everything loading would (manifest, permissions, paths, sizes, app compatibility,
  every library, namespacing), copies only the manifest, the contributed libraries and top-level licence/readme
  files (no other files, no symlinks) into a hidden staging directory, validates the staged copy again and then
  renames it into `<installDir>/<id>`; the previous copy is restored if the swap fails.
* **Remove** only deletes `<installDir>/<id>` after checking that the id is valid (so it contains no path
  separators), the entry is a real directory (not a symlink) directly inside the canonical install directory and
  contains a `plugin.json` whose id, if readable, matches. Symlinks inside the tree are removed as links; their
  targets are not touched.

Remaining risks (data plugins can still be wrong): dimensions in a library can be incorrect. Hardware carries
`dimensionsVerified` (ARCHITECTURE.md 4.7), which the UI must show; plugins do not change that flag's meaning.

## 5. Plugin manager

```cpp
using namespace fos::plugins;
PluginManagerOptions o;
o.appVersion = parseSemVer("0.1.0").value();
o.searchDirs = {systemPluginDir, userPluginDir};   // scanned in this order
o.installDir = userPluginDir;
PluginManager manager(o);
manager.setDisabledIds(settings.disabledPlugins);  // persisted by the application
auto discoveryNotes = manager.discover();
formats::Library library = /* user's library */;
LoadReport report = manager.loadInto(library);
for (const auto& r : manager.plugins()) show(r.manifest.id, toString(r.state), r.diagnostics);
settings.disabledPlugins = manager.disabledIds();
```

* **Discovery order**: search dirs in the given order; within a dir, subdirectories by byte-wise name order;
  entries whose name starts with `.` and plain files are skipped; a missing search dir is an Info diagnostic.
* **States**: `Invalid`, `Incompatible` (app version outside `[minAppVersion, maxAppVersion]`), `Shadowed`,
  `Disabled`, `Enabled`, `Loaded`, `LoadFailed`.
* **Duplicates**: among valid and compatible copies with the same id the highest version wins; for equal
  precedence the earlier search dir wins. The others become `Shadowed` with a message naming the winner.
  An incompatible copy never shadows a compatible one.
* **Loading** happens in plugin-id order; `LoadReport` lists the loaded plugins with their `formats::MergeReport`.

## 6. Versioning

* Plugin versions and app versions use **SemVer 2.0.0** (`parseSemVer`, `compare`), strictly: no leading `v`,
  no leading zeros, three numeric parts, pre-release identifiers compared numerically / ASCII
  (`1.0.0-alpha < 1.0.0-alpha.1 < 1.0.0-alpha.beta < 1.0.0-beta < 1.0.0-beta.2 < 1.0.0-beta.11 < 1.0.0-rc.1 <
  1.0.0`), build metadata ignored for precedence.
* A pre-release app (e.g. `0.2.0-dev`) is **lower** than `0.2.0`, so a plugin with `minAppVersion` `0.2.0`
  is incompatible with it. Use `0.2.0-0` as minimum to admit pre-releases.
* Install refuses to replace an installed newer version (or an unreadable installed copy) unless
  `InstallOptions::overwriteNewer` is set; equal or older installed versions are replaced.
* `manifestVersion` will be increased only for incompatible manifest changes; new optional keys are added
  without a bump (older versions warn and ignore them).

## 7. Writing a library plugin, step by step

1. Choose an id under a domain you control, e.g. `org.example.oak-boards`, and create a directory of that name.
2. Build a `formats::Library` whose ids are all `org.example.oak-boards:<local-id>` and write it with
   `formats::exportLibrary(path, library, appInfo, options)` (never edit a `.foslib` by hand: the payload
   checksum would no longer match and the file is refused). Put it at e.g. `libraries/oak.foslib`.
   The example plugin's library is generated by the tool `fos_plugins_make_example`
   (`src/plugins/tools/`); a test checks that the committed file equals a fresh export.
3. Write `plugin.json` (section 2) listing `"libraries": ["libraries/oak.foslib"]`, `"permissions": ["data"]`,
   a licence for the data, and the minimum app version you tested with.
4. Add a `LICENSE` file for the data.
5. Check it: `inspectPackage(dir)` must return no errors and `loadPackageLibraries()` must succeed; or install it
   with `PluginManager::install(dir)`, which runs the same checks.
6. Release new versions with a higher SemVer; keep local ids stable, as projects refer to entries by id.

## 8. Not supported (phase 1)

* Executable extensions of any kind (scripts, WASM, out-of-process plugins, shared libraries) — see ADR-0007
  phase 2. Contribution kinds `templates`, `reports`, `postprocessors`, `importers` are recognised but ignored.
* Signatures, content hashes in the manifest, a registry or downloading; plugin archives (`.zip`): install
  takes an unpacked directory.
* SPDX expressions with parentheses; validation of SPDX identifiers against the official list.
* Remapping of entity ids, dependencies between plugins, per-plugin settings, localisation of entity names
  (only the plugin name has `localizedNames`).
* Removal from search dirs other than `installDir` (e.g. a system-wide directory).
* Library files are read by `formats::importLibrary` after a size check; a file replaced between that check and
  the read is not detected (the manifest read itself is bounded).
