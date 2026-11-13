// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Headless command-line front end of FurnitureOS (`fos`). The executable is a thin
/// `main()` around run(); everything is in the library `fos::cli_core` so every command is
/// testable in-process. The CLI is offline and deterministic: the same project file and
/// arguments give the same output bytes (except `fos new`, which draws a fresh UUID v4 and
/// reads the clock unless `--id` / `--date` are given, and the `savedAt` stamp written by
/// saves). All lengths are millimetres. Messages are English.
#include <iosfwd>
#include <string>
#include <vector>

namespace fos::cli {

/// Process exit codes of every command.
enum ExitCode : int {
    kExitOk = 0,               ///< Success (warnings may have been printed).
    kExitValidationErrors = 1, ///< The command ran but found Error findings (validation, refused CAM, ...).
    kExitUsageOrIo = 2,        ///< Bad arguments, unknown command/item/part, unreadable or unwritable file.
};

/// Runs one `fos` invocation. `args` are the arguments AFTER the program name, e.g.
/// {"validate", "kitchen.fosp", "--format", "markdown"}. Normal output goes to `out`,
/// errors and warnings about the invocation to `err`. Returns an ExitCode value.
/// Never throws for bad input.
int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

/// The list of commands with a one-line description each (what `fos --help` prints).
std::string overviewHelp();

} // namespace fos::cli
