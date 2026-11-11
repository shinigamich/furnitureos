# ADR-0007: Plugin safety model

* Status: Proposed (recommendation; not yet binding)
* Date: 2026-11-11

## Context

Users will want to share material, hardware and template libraries, report templates, and eventually custom automation or machine postprocessors. A CAD/CAM tool that generates machine code makes plugin safety a physical-safety issue (a bad postprocessor can crash a spindle into a table), not just a data-security one. Code-executing plugins also conflict with an "open, auditable, offline" trust model. AGPL-3.0 applies to FurnitureOS itself; plugins that link in-process into an AGPL program are likely to be treated as derivative works (a legal question to confirm, not settled here).

## Decision (recommended)

* **Phase 1: declarative data plugins only.** Material libraries, edge-band libraries, hardware definitions, furniture templates and report templates are data files with a manifest (id, version, schema version, licence, author, compatible app versions, content hash). **No code is executed.** Loading is validated against schemas; unknown or invalid data is rejected with diagnostics. Hardware dimensions and machine data carry a `verified` flag (ARCHITECTURE.md 4.7).
* **Phase 2: sandboxed scripting or out-of-process plugins** with a **permission manifest** (e.g. read model, write exports into a chosen folder; no network or process spawning by default; explicit user consent per permission; displayed on install). Output of any plugin that produces manufacturing data is subject to the same mandatory validation and human review as built-in output (see `docs/research/cam-and-postprocessors.md`).
* **Native in-process plugins (shared libraries) are not loaded by default.** They can only be enabled by an explicit expert setting, clearly marked unsafe, never auto-discovered from project files.

## Options analysed

| Option | Isolation | Security notes | Licensing notes (from general knowledge, not verified in this session) |
|---|---|---|---|
| Declarative data (JSON) | Complete (no code) | Parser bugs and resource exhaustion remain; limit sizes and depth | None beyond content licences. |
| Embedded Lua | Language-level sandbox only if the standard library is restricted; no memory/CPU limits by default | Must remove `io`, `os`, `package`, `debug`, bytecode loading; add instruction-count hooks and memory allocator limits; escapes have occurred historically in unrestricted setups | Lua is MIT. |
| WASM runtime (e.g. Wasmtime, Wasmer, WAMR, wasm3) | Strong memory isolation; capability-based host APIs (WASI) | Smaller attack surface; need fuel/memory limits; host API design is the main work; toolchain burden for plugin authors | Wasmtime Apache-2.0 (with LLVM exception), Wasmer MIT, WAMR Apache-2.0 (verify before adoption). C++ embedding of Rust-based runtimes via C API. |
| Embedded Python | Weak: Python cannot be reliably sandboxed in-process | Not recommended for untrusted code; large runtime to ship | PSF licence (permissive). |
| Out-of-process (JSON-RPC over stdio or local socket) | OS-level process boundary; can be combined with OS sandboxes (seccomp, AppContainer, sandbox-exec) | Plugin can be written in any language; the process still has the user's privileges unless an OS sandbox is applied; protocol versioning and timeouts needed | Process boundary also simplifies the licence story (separate program communicating at arm's length, still not legal advice). |
| Native shared library | None | Full code execution in-process | GPL/AGPL linking implications. |

## Alternatives considered

* No plugin system at all: simplest, but libraries are needed for the product to be useful; data plugins give most of the value at minimal risk.
* Phase 2 chosen as out-of-process first or WASM first: not decided; choose after concrete use cases exist.

## Consequences

* Phase 1 is cheap, safe and testable; extension capability is limited to data.
* Authors of custom postprocessors cannot ship code in phase 1; machine support comes from first-party profiles/postprocessors in the main repository (reviewed).
* A signing/registry story (the optional `furnitureos-website-backend` plugin registry) is outside this ADR; the desktop app must work without it.
* Supply-chain risk: even data plugins can contain wrong dimensions; verification flags and provenance are mandatory in the UI.

## Revisit when

* A concrete need for executable extensions (custom parametric rules, postprocessors) is documented.
* A WASM or out-of-process prototype passes a threat-model review.
* Legal review of AGPL implications for plugin licensing is done.
