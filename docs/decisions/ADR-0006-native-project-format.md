# ADR-0006: Native project format (`.fosp`)

* Status: Accepted
* Date: 2026-11-11

## Context

Project files are the user's work product and may outlive application versions. Required properties: human-inspectable, diffable and recoverable; forward/backward policy; safe against crashes and power loss; tolerant of data written by newer minor versions or plugins; no network dependence.

## Decision

A `.fosp` file is a UTF-8 JSON document (an *envelope*):

```json
{ "format": "furnitureos-project", "formatVersion": 1, "app": { ... }, "checksum": "...", "payload": { ... } }
```

* `formatVersion` is an integer. **Sequential migrations** N to N+1 are applied on load; there are no direct N to N+k migrations. A file with a version newer than the application supports is **refused with a clear message and never partially loaded**.
* `checksum` covers the canonical payload serialisation, so truncation or accidental edits are detected. It is an integrity check, not a security signature. The hash algorithm and canonicalisation must be fixed in the format spec before release (open item).
* **Unknown keys are preserved** on round-trip (kept in an opaque map next to the known fields) so that a file touched by an older version does not lose data written by a newer one or by plugins. Preservation applies to objects whose schema is extensible; this must be defined per object.
* **Atomic save**: write to a temporary file in the same directory, flush and `fsync`, then rename over the target; keep rotating backups (for example `name.fosp.bak1..bakN`) before replacing. On Windows use an atomic replace API (`ReplaceFile`/`MoveFileEx` with replace); fsync of the directory on POSIX. Not yet implemented or tested in this session.
* Only authoritative data is saved (parameters, libraries, user edits to derived artefacts keyed by stable ids). Derived results (cut lists, drawings, toolpaths) are regenerated, never stored (ARCHITECTURE.md section 3).
* Numbers are written to round-trip doubles (ADR-0003).

## Alternatives considered

| Option | For | Against |
|---|---|---|
| Custom binary format | Compact, fast | Not diffable or recoverable by hand; versioning burden; tooling needed. |
| SQLite database | Transactional, incremental saves, queryable | Binary blob in version control; schema migrations for relational schema; harder partial recovery; single-writer file locks; unneeded for project sizes of tens of MB at most. |
| ZIP package (OPC-style: manifest + JSON parts + assets) | Handles binary assets, compression, partial reads | More complex atomic save and diffing; extra dependency (zlib/miniz/libzip); overkill while all data is text. |
| JSON with schema only (no envelope) | Simpler | No explicit format identification, version or checksum. |
| YAML/TOML | Readable | Ambiguity (YAML) or poor nesting/array-of-struct support (TOML); JSON is universal. |

## When a packaged archive becomes necessary

* Embedded textures/decor images, thumbnails, or imported meshes/STEP files that make the JSON large (a rough threshold: tens of MB, or any binary content that cannot be represented efficiently as text; no measurements yet).
* Need to load assets lazily or to attach source files to the project.
* Then the envelope becomes the manifest inside a ZIP container with a new `formatVersion`; the JSON-only form would remain readable via a migration. Choose the container library with attention to licence (zlib/miniz permissive).

## Consequences

* Files are easy to inspect, diff and repair; large projects may be slow to save (full rewrite each time) and verbose.
* JSON has no comment or native big-int support; ids and numbers must be validated on load.
* The checksum means manual edits require recomputation (a documented `fos` CLI command is advisable) or a "ignore checksum" recovery option.
* Preserving unknown keys can retain stale data; schema documentation must state which objects are extensible.

## Revisit when

* Binary assets or file sizes exceed what JSON handles well.
* Concurrent/collaborative editing or incremental save is required.
* A security requirement (signed projects) appears.
