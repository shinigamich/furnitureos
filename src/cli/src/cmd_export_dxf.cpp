// SPDX-License-Identifier: AGPL-3.0-only
// `fos export-dxf`: part outlines (with cut-outs, holes, grooves) as DXF R12 for CNC / cutting software.
#include "common.hpp"

#include "fos/formats/dxf_export.hpp"

namespace fos::cli::detail {
namespace {

const char* kExportDxfHelp =
    "Usage: fos export-dxf <file.fosp> <out.dxf | out-dir> [--finished] [--one-file-per-part]\n"
    "                      [--part PART-ID] [--spacing MM]\n\n"
    "Writes the part outlines of the resolved model as ASCII DXF R12 (AC1009, units mm,\n"
    "$INSUNITS 4) for CNC, nesting and cutting software. Each part is drawn in its part frame\n"
    "seen from face A. Layers: OUTLINE (raw outline before edge banding; --finished: finished\n"
    "outline), CUTOUTS (shape cut-outs, through pockets), POCKETS (blind pockets on face A),\n"
    "DRILL_<d> (holes on face A and through holes, one layer per diameter, e.g. DRILL_5,\n"
    "DRILL_8_5), GROOVES (groove footprints on face A), LABELS (part id as TEXT). Arcs are exact\n"
    "polyline bulges. Blind operations on face B and edge operations are not drawn (warning).\n"
    "Default: one file, parts in a row along X, --spacing mm apart (default 50).\n"
    "--one-file-per-part: <out-dir> is created; one <part-id>.dxf per part ('/' etc. as '_').\n"
    "--part: only that part.\n"
    "Exit codes: 0 written, 1 a part cannot be exported or the model has resolve errors,\n"
    "2 usage error (unknown part, bad option) or I/O error.\n";

} // namespace

int cmdExportDxf(const std::vector<std::string>& args, Io& io) {
    const char* cmd = "export-dxf";
    auto parsed = parseArgs(args, {{"finished", false}, {"one-file-per-part", false}, {"part"}, {"spacing"}});
    if (!parsed) return usageError(io, cmd, parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kExportDxfHelp;
        return kExitOk;
    }
    if (a.positional.size() != 2) return usageError(io, cmd, "expected <file.fosp> <out.dxf | out-dir>.");
    formats::DxfExportOptions options;
    options.finishedOutline = a.has("finished");
    options.oneFilePerPart = a.has("one-file-per-part");
    options.partId = a.value("part").value_or("");
    if (auto s = a.value("spacing")) {
        const auto v = parseNumber(*s);
        if (!v || !(*v >= 0 && *v <= 100000)) return usageError(io, cmd, "--spacing must be a number in [0, 100000] mm, got '" + *s + "'.");
        options.spacing = *v;
    }
    const std::filesystem::path out = pathFromUtf8(a.positional[1]);
    std::error_code ec;
    if (!options.oneFilePerPart && !out.parent_path().empty() && !std::filesystem::is_directory(out.parent_path(), ec))
        return usageError(io, cmd, "directory '" + pathToUtf8(out.parent_path()) + "' does not exist.");

    auto loaded = loadProjectOrReport(io, cmd, a.positional[0]);
    if (!loaded) return kExitUsageOrIo;
    auto r = formats::exportPartOutlinesDxf(loaded->resolved.model, options);
    if (!r) {
        if (r.error().code == core::ErrorCode::NotFound) return usageError(io, cmd, r.error().message);
        io.err << "fos export-dxf: " << r.error().message << "\n";
        return kExitValidationErrors;
    }
    const formats::DxfExport& ex = r.value();
    if (options.oneFilePerPart) {
        if (!ensureOutputDirectory(io, cmd, out)) return kExitUsageOrIo;
        for (const auto& f : ex.files)
            if (!writeOutputFile(io, cmd, out / pathFromUtf8(f.fileName), f.text)) return kExitUsageOrIo;
        io.out << "Wrote " << ex.files.size() << " DXF file(s) to " << pathToUtf8(out) << "\n";
    } else {
        if (!writeOutputFile(io, cmd, out, ex.files.front().text)) return kExitUsageOrIo;
        io.out << "Wrote " << ex.partCount << " part(s) to " << pathToUtf8(out) << "\n";
    }
    io.out << "Outline: " << (options.finishedOutline ? "finished" : "raw (before edge banding)") << ", DXF R12, mm.\n";
    if (!ex.diagnostics.empty()) printDiagnostics(io.out, ex.diagnostics);
    const std::string incomplete = resolveErrorNotice(cmd, *loaded);
    io.err << incomplete;
    return incomplete.empty() ? kExitOk : kExitValidationErrors;
}

} // namespace fos::cli::detail
