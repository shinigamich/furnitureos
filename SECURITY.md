# Security Policy

## Supported versions

FurnitureOS is in early development; only the latest commit on the default branch is supported.

## Reporting a vulnerability

Please report vulnerabilities privately through GitHub Security Advisories
("Report a vulnerability" on the repository's **Security** tab). Do not open a public issue for
security problems. Include a description, affected version/commit, reproduction steps and impact.

## Scope and threat model

* **Project and library files are untrusted input.** Parsers must reject malformed data with an error and
  must never crash, hang or execute content. Reports of crashes on malformed files are welcome.
* **Plugins.** Only declarative (data) plugins are loaded; FurnitureOS does not execute third-party code
  by default (ADR-0007).
* **Network.** The desktop application works fully offline and does not upload project data.
* **CNC output.** Any machine program is experimental until verified for the specific machine and must be
  reviewed by a qualified operator before execution. FurnitureOS never starts a machine by itself.
