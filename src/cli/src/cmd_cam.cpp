// SPDX-License-Identifier: AGPL-3.0-only
// fos cam (UNVERIFIED output, fictional demo machine)
#include "common.hpp"

#include "fos/cam/generate.hpp"
#include "fos/cam/generic_iso.hpp"
#include "fos/cam/simulate.hpp"
#include "fos/cli/demo_cam.hpp"
#include "fos/core/units.hpp"
#include "fos/machining/validation.hpp"

namespace fos::cli::detail {
namespace {

const char* kCamHelp =
    "Usage: fos cam <file.fosp> <part-id> --face A|B --out prog.nc [--peck DEPTH]\n\n"
    "Generates a 3-axis program for the operations on one face of one part, using the\n"
    "FICTIONAL 'FurnitureOS demo profile — not a real machine' and a fictional demo tool\n"
    "library (drills 3 4 5 7 8 10 12 15 20 35 mm, end mills 4 6 8 mm). The part's length runs\n"
    "along machine +X; its minimum corner is at machine (20, 20, 0); face B up = turned over\n"
    "about the length axis. The program is simulated independently and the generic ISO\n"
    "postprocessor REFUSES output when the simulation reports an error (exit code 1).\n"
    "Operations on edges (horizontal drilling) and pockets are listed as UNSUPPORTED, and\n"
    "operations on the other face as needing the other setup - never silently dropped.\n"
    "THE OUTPUT IS UNVERIFIED: no profile, tool or dialect has been proven on any machine.\n"
    "Never run it without review and proof by a qualified operator on the actual control.\n";

const domain::Part* findPart(const domain::Model& model, const std::string& id) {
    for (const auto& a : model.assemblies)
        if (const auto* p = a.findPart(domain::PartId(id))) return p;
    return nullptr;
}

void printPlanned(std::ostream& out, const char* title, const std::vector<cam::PlannedOperation>& ops) {
    out << title << ": " << ops.size() << "\n";
    for (const auto& op : ops) out << "  " << op.id.str() << (op.reason.empty() ? "" : ": " + op.reason) << "\n";
}

} // namespace

int cmdCam(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"face"}, {"out"}, {"peck"}});
    if (!parsed) return usageError(io, "cam", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kCamHelp;
        return kExitOk;
    }
    if (a.positional.size() != 2) return usageError(io, "cam", "expected <file.fosp> <part-id>.");
    const std::string faceText = a.value("face").value_or("");
    if (faceText != "A" && faceText != "B") return usageError(io, "cam", "--face must be A or B.");
    if (!a.has("out")) return usageError(io, "cam", "--out prog.nc is required.");
    cam::CamOptions options;
    if (auto peck = a.value("peck")) {
        const auto v = parseNumber(*peck);
        if (!v || *v < 0) return usageError(io, "cam", "--peck must be a depth >= 0 in mm.");
        options.peckDepth = *v;
    }
    auto loaded = loadProjectOrReport(io, "cam", a.positional[0]);
    if (!loaded) return kExitUsageOrIo;
    const std::string& partId = a.positional[1];
    const domain::Part* part = findPart(loaded->resolved.model, partId);
    if (!part) return usageError(io, "cam", "no generated part '" + partId + "' in the project.");

    auto& out = io.out;
    const cam::MachineProfile machine = demoMachineProfile();
    const cam::ToolLibrary tools = demoToolLibrary();
    const cam::Setup setup{faceText == "A" ? domain::Face::A : domain::Face::B, demoStockOrigin()};
    out << "Part " << partId << " \"" << part->name << "\" " << core::formatLength(part->length) << " x "
        << core::formatLength(part->width) << " x " << core::formatLength(part->thickness) << " mm, "
        << part->operations.size() << " operations, face " << faceText << " up\n"
        << "Machine: " << machine.name << " (fictional, verified: no)\n";

    const auto opDiagnostics = machining::validateOperations(*part);
    out << "Machining validation: " << (opDiagnostics.empty() ? "no findings" : std::to_string(opDiagnostics.size()) + " finding(s)") << "\n";
    printDiagnostics(out, opDiagnostics);

    auto generated = cam::generateProgram(*part, setup, machine, tools, options);
    if (!generated) {
        io.err << "fos cam: refused: " << generated.error().message << "\nNo program was written.\n";
        return kExitValidationErrors;
    }
    const auto& g = generated.value();
    printPlanned(out, "Machinable in this setup", g.plan.machinableNow);
    printPlanned(out, "Needs the other face up", g.plan.needsOtherSetup);
    printPlanned(out, "UNSUPPORTED (not in this program)", g.plan.unsupported);
    // The per-operation setup warnings are the lists above; print the remaining ones.
    core::Diagnostics other;
    for (const auto& d : g.diagnostics)
        if (d.code != "cam.setup.needs_other_setup" && d.code != "cam.setup.unsupported") other.push_back(d);
    if (!other.empty()) {
        out << "CAM diagnostics:\n";
        printDiagnostics(out, other);
    }
    if (g.plan.machinableNow.empty()) {
        io.err << "fos cam: nothing to machine on face " << faceText << " of " << partId
               << "; no program was written.\n";
        return kExitValidationErrors;
    }

    const auto sim = cam::simulate(g.program, *part, setup, machine, tools);
    out << "Simulation: " << errorCount(sim.diagnostics) << " errors, " << warningCount(sim.diagnostics)
        << " warnings; " << sim.toolChanges << " tool change(s), rapid " << core::formatLength(sim.rapidLength)
        << " mm, cutting " << core::formatLength(sim.cutLength) << " mm, estimated "
        << core::formatLength(sim.estimatedSeconds, 0) << " s (estimate, no acceleration)\n";
    printDiagnostics(out, sim.diagnostics);

    const cam::GenericIsoPostprocessor post;
    auto emitted = post.generate(cam::PostInput{g.program, *part, setup, machine, tools});
    if (!emitted) {
        io.err << "fos cam: refused by the postprocessor: " << emitted.error().message << "\nNo program was written.\n";
        return kExitValidationErrors;
    }
    const auto& e = emitted.value();
    if (!writeOutputFile(io, "cam", *a.value("out"), e.text)) return kExitUsageOrIo;
    out << "Wrote " << *a.value("out") << " (" << e.postprocessorName << ")\n"
        << "verifiedForMachine: " << (e.verifiedForMachine ? "true" : "false")
        << " - UNVERIFIED output for a fictional demo machine; review before any use.\n";
    if (!e.reviewWarnings.empty()) {
        out << "Review before use:\n";
        for (const auto& w : e.reviewWarnings) out << "  - " << w << "\n";
    }
    return kExitOk;
}

} // namespace fos::cli::detail
