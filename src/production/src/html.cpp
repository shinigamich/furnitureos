// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/html.hpp"

#include "fos/core/units.hpp"

namespace fos::production {
namespace {

const char* const kStyle =
    "body{font-family:\"DejaVu Sans\",Arial,Helvetica,sans-serif;font-size:10pt;color:#000;margin:12mm}"
    "h1{font-size:16pt;margin:0 0 2mm}h2{font-size:12pt;margin:6mm 0 2mm}"
    "table{border-collapse:collapse;width:100%}"
    "th,td{border:0.2mm solid #444;padding:1mm 1.5mm;text-align:left;vertical-align:top}"
    "th{background:#eee}tr{page-break-inside:avoid}.meta td{border:none;padding:0.5mm 3mm 0.5mm 0}"
    ".meta{width:auto}.empty{color:#555;font-style:italic}"
    "@page{size:A4 landscape;margin:10mm}@media print{body{margin:0}th{background:#eee !important}}";

void section(std::string& out, const std::string& title, const Table& table, const std::string& none) {
    out += "<h2>" + escapeXml(title) + "</h2>\n";
    if (table.rows.empty())
        out += "<p class=\"empty\">" + escapeXml(none) + "</p>\n";
    else
        out += tableHtml(table);
}

} // namespace

std::string tableHtml(const Table& table) {
    std::string out = "<table>\n<thead><tr>";
    for (const auto& h : table.header) out += "<th>" + escapeXml(h) + "</th>";
    out += "</tr></thead>\n<tbody>\n";
    for (const auto& row : table.rows) {
        out += "<tr>";
        for (const auto& cell : row) out += "<td>" + escapeXml(cell) + "</td>";
        out += "</tr>\n";
    }
    out += "</tbody>\n</table>\n";
    return out;
}

core::Result<std::string> productionSheetHtml(const domain::Model& model, const domain::AssemblyId& assembly,
                                              const HtmlSheetOptions& o) {
    const auto* a = model.findAssembly(assembly);
    if (!a)
        return core::makeError(core::ErrorCode::NotFound,
                               "Assembly '" + assembly.str() + "' is not in the model (" +
                                   std::to_string(model.assemblies.size()) + " assemblies).",
                               assembly.str());
    const Scope scope{assembly};
    const auto parts = partList(model, scope);
    const auto cuts = cutList(model, o.cutList, scope);
    const auto bands = edgeBandingReport(model, o.edgeBanding, scope);
    const auto hw = hardwareReport(model, scope);
    const auto summary = projectSummary(model, scope);
    const auto& t = o.text;

    std::string out = "<!DOCTYPE html>\n<html lang=\"" + escapeXml(t.lang) + "\">\n<head>\n<meta charset=\"utf-8\">\n";
    out += "<title>" + escapeXml(t.title + " - " + a->name) + "</title>\n<style>" + kStyle + "</style>\n</head>\n<body>\n";
    out += "<h1>" + escapeXml(t.title) + "</h1>\n<table class=\"meta\">\n";
    auto meta = [&](const std::string& k, const std::string& v) {
        out += "<tr><td>" + escapeXml(k) + "</td><td><strong>" + escapeXml(v) + "</strong></td></tr>\n";
    };
    if (!o.projectName.empty()) meta(t.project, o.projectName);
    meta(t.assembly, a->name);
    if (!summary.assemblies.empty()) {
        const auto& s = summary.assemblies.front();
        const int d = o.numbers.length;
        meta(t.envelope, core::formatLength(s.envelopeWidth, d) + " x " + core::formatLength(s.envelopeDepth, d) +
                             " x " + core::formatLength(s.envelopeHeight, d));
    }
    out += "</table>\n";

    section(out, t.partList, partListTable(parts, o.localisation, o.numbers), t.none);
    section(out, t.cutList, cutListTable(cuts, o.localisation, o.numbers), t.none);
    section(out, t.edgeBanding, edgeBandingTable(bands, o.localisation, o.numbers), t.none);
    section(out, t.hardware, hardwareTable(hw, o.localisation, o.numbers), t.none);

    // Part-list diagnostics already contain the missing material / edge band findings
    // that the cut list and edge banding report repeat.
    core::Diagnostics ds = parts.diagnostics;
    ds.insert(ds.end(), hw.diagnostics.begin(), hw.diagnostics.end());
    if (!ds.empty()) {
        out += "<h2>" + escapeXml(t.warnings) + "</h2>\n<ul>\n";
        for (const auto& d : ds) out += "<li>" + escapeXml(d.message) + "</li>\n";
        out += "</ul>\n";
    }
    out += "</body>\n</html>\n";
    return out;
}

} // namespace fos::production
