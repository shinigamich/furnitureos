// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/cli/cli.hpp"

#include "common.hpp"

#include <array>
#include <exception>
#include <ostream>

namespace fos::cli {
namespace {

using Handler = int (*)(const std::vector<std::string>&, detail::Io&);

struct CommandEntry {
    const char* name;
    const char* summary;
    Handler handler;
};

constexpr std::array<CommandEntry, 10> kCommands{{
    {"new", "Create a project file, optionally with one cabinet from a template.", detail::cmdNew},
    {"info", "Show items, parameters, part counts and diagnostics of a project.", detail::cmdInfo},
    {"set", "Set a parameter of an item to a value or an expression and save.", detail::cmdSet},
    {"validate", "Run the model validation checks (exit 1 on errors).", detail::cmdValidate},
    {"report", "Production reports as text/CSV, or an HTML production sheet.", detail::cmdReport},
    {"cut", "Optimise cutting plans (heuristic) and write one SVG per sheet.", detail::cmdCut},
    {"drawing", "Assembly or part drawing as SVG (exact scale) or DXF (1:1).", detail::cmdDrawing},
    {"labels", "Part labels as SVG pages.", detail::cmdLabels},
    {"cam", "UNVERIFIED G-code for one face of one part (fictional demo machine).", detail::cmdCam},
    {"export-obj", "Wavefront OBJ (+ MTL) of the resolved model.", detail::cmdExportObj},
}};

} // namespace

std::string overviewHelp() {
    std::string s = "Usage: fos <command> [arguments] [options]\n\n"
                    "FurnitureOS command-line tool (offline). Lengths are millimetres.\n\nCommands:\n";
    for (const auto& c : kCommands) {
        std::string name = c.name;
        name.resize(12, ' ');
        s += "  " + name + c.summary + "\n";
    }
    s += "\nRun 'fos <command> --help' for the arguments of a command.\n"
         "Exit codes: 0 success, 1 errors found (validation, refused CAM, ...), 2 usage or I/O error.\n";
    return s;
}

int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    detail::Io io{out, err};
    if (args.empty()) {
        err << overviewHelp();
        return kExitUsageOrIo;
    }
    const std::string& command = args.front();
    if (command == "--help" || command == "-h" || command == "help") {
        out << overviewHelp();
        return kExitOk;
    }
    if (command == "--version") {
        out << "fos (FurnitureOS) " << FOS_CLI_VERSION << "\n";
        return kExitOk;
    }
    for (const auto& c : kCommands) {
        if (command != c.name) continue;
        const std::vector<std::string> rest(args.begin() + 1, args.end());
        try {
            return c.handler(rest, io);
        } catch (const std::exception& e) {
            // Only programming errors throw; report instead of terminating.
            err << "fos " << command << ": internal error: " << e.what() << "\n";
            return kExitUsageOrIo;
        }
    }
    err << "fos: unknown command '" << command << "'.\n\n" << overviewHelp();
    return kExitUsageOrIo;
}

} // namespace fos::cli
