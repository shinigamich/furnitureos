// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/job_pack.hpp"

#include "fos/core/units.hpp"
#include "fos/production/bought_in.hpp"
#include "fos/production/cut_list.hpp"
#include "fos/production/html.hpp"
#include "fos/production/part_list.hpp"
#include "fos/production/reports.hpp"
#include "fos/production/tables.hpp"
#include "job_pack_sections.hpp"

#include <array>
#include <set>
#include <tuple>

namespace fos::production {
namespace {

constexpr std::array<JobPackSection, 10> kAll = {
    JobPackSection::Cover,       JobPackSection::Parts,    JobPackSection::CutList, JobPackSection::BoughtIn,
    JobPackSection::EdgeBanding, JobPackSection::Hardware, JobPackSection::Packing, JobPackSection::Assembly,
    JobPackSection::Plans,       JobPackSection::Warnings};

const char* title(JobPackSection s) noexcept {
    switch (s) {
    case JobPackSection::Cover: return "Job pack";
    case JobPackSection::Parts: return "Part list";
    case JobPackSection::CutList: return "Cut list";
    case JobPackSection::BoughtIn: return "Bought-in items";
    case JobPackSection::EdgeBanding: return "Edge-banding jobs";
    case JobPackSection::Hardware: return "Hardware";
    case JobPackSection::Packing: return "Packing list";
    case JobPackSection::Assembly: return "Assembly sheets";
    case JobPackSection::Plans: return "Cutting plans";
    case JobPackSection::Warnings: return "Warnings";
    }
    return "";
}

const char* const kStyle =
    "body{font-family:\"DejaVu Sans\",Arial,Helvetica,sans-serif;font-size:9pt;color:#000;margin:10mm;background:#fff}"
    "h1{font-size:18pt;margin:0 0 3mm}h2{font-size:13pt;margin:0 0 3mm;border-bottom:0.4mm solid #000}"
    "h3{font-size:10.5pt;margin:4mm 0 1.5mm}p{margin:1mm 0}"
    "table{border-collapse:collapse;width:100%;margin:1mm 0 3mm}"
    "th,td{border:0.2mm solid #444;padding:0.8mm 1.2mm;text-align:left;vertical-align:top}"
    "th{background:#eee}tr,li,.plan{page-break-inside:avoid;break-inside:avoid}"
    ".meta{width:auto}.meta td{border:none;padding:0.5mm 4mm 0.5mm 0}"
    ".empty{color:#555;font-style:italic}.note{color:#333;font-style:italic}.c{text-align:center}"
    ".box{display:inline-block;width:3.5mm;height:3.5mm;border:0.3mm solid #000}"
    ".plan svg{max-width:100%;height:auto;max-height:170mm}.toc a{color:#000}"
    "section+section{page-break-before:always;break-before:page;margin-top:8mm}"
    "@page{size:A4 landscape;margin:10mm}"
    "@media print{body{margin:0}section+section{margin-top:0}th{background:#eee !important}}";

std::string esc(std::string_view s) { return escapeXml(s); }

/// Table or the "none" note.
std::string tableOrNone(const Table& t) { return t.rows.empty() ? jobpack::emptyNote() : tableHtml(t); }

std::string cutListByMaterial(const CutList& cuts) {
    if (cuts.rows.empty()) return jobpack::emptyNote();
    std::string out;
    std::size_t i = 0;
    while (i < cuts.rows.size()) {
        CutList one;
        const auto& first = cuts.rows[i];
        while (i < cuts.rows.size() && cuts.rows[i].materialId == first.materialId) one.rows.push_back(cuts.rows[i++]);
        int pieces = 0;
        double areaM2 = 0;
        for (const auto& r : one.rows) {
            pieces += r.quantity;
            areaM2 += r.quantity * r.cutLength * r.cutWidth / 1e6;
        }
        out += "<h3>" + esc(first.materialMissing ? first.materialId.str() + " (MISSING material)"
                                                  : first.materialName + " (" + first.materialId.str() + ")") +
               esc(" - " + core::formatLength(first.thickness) + " mm") + "</h3>\n<p>" + std::to_string(pieces) +
               " piece(s), cut area " + esc(core::formatLength(areaM2, 4)) + " m2</p>\n" + tableHtml(cutListTable(one));
    }
    return out;
}

std::string extractSvg(const std::string& svg) {
    const auto begin = svg.find("<svg");
    if (begin == std::string::npos) return {};
    const auto end = svg.rfind("</svg>");
    if (end == std::string::npos || end < begin) return svg.substr(begin);
    return svg.substr(begin, end + 6 - begin);
}

struct Pack {
    const domain::Model& model;
    const JobPackOptions& o;
    core::Diagnostics diagnostics;
    std::set<std::tuple<std::string, std::string, std::string>> seen;

    void collect(const core::Diagnostics& ds) {
        for (const auto& d : ds)
            if (d.severity != core::Severity::Info && seen.emplace(d.code, d.entityId, d.message).second)
                diagnostics.push_back(d);
    }

    std::string cover(const ProjectSummary& summary, const BoughtInList& boughtIn,
                      const std::vector<JobPackSection>& sections) const {
        std::string out = "<table class=\"meta\">\n";
        auto meta = [&](const char* k, const std::string& v) {
            if (!v.empty()) out += "<tr><td>" + std::string(k) + "</td><td><strong>" + esc(v) + "</strong></td></tr>\n";
        };
        meta("Project", o.projectName);
        meta("Date", o.date);
        meta("Author", o.author);
        out += "</table>\n<h3>Items</h3>\n";
        if (summary.assemblies.empty()) {
            out += jobpack::emptyNote();
        } else {
            out += "<table>\n<thead><tr><th>No.</th><th>Item</th><th>Item ID</th><th>Qty</th><th>Parts</th>"
                   "<th>Hardware</th><th>W x D x H (mm)</th></tr></thead>\n<tbody>\n";
            int n = 0;
            for (const auto& a : summary.assemblies) {
                const auto q = o.itemQuantities.find(a.id);
                out += "<tr><td>" + std::to_string(++n) + "</td><td>" + esc(a.name) + "</td><td>" + esc(a.id.str()) +
                       "</td><td>" + std::to_string(q == o.itemQuantities.end() ? 1 : q->second) + "</td><td>" +
                       std::to_string(a.partCount) + "</td><td>" + std::to_string(a.hardwareQuantity) + "</td><td>" +
                       esc(core::formatLength(a.envelopeWidth) + " x " + core::formatLength(a.envelopeDepth) + " x " +
                           core::formatLength(a.envelopeHeight)) +
                       "</td></tr>\n";
            }
            out += "</tbody>\n</table>\n";
        }
        out += "<h3>Totals</h3>\n<table class=\"meta\">\n";
        auto total = [&](const char* k, const std::string& v) {
            out += "<tr><td>" + std::string(k) + "</td><td><strong>" + esc(v) + "</strong></td></tr>\n";
        };
        total("Items", std::to_string(summary.assemblyCount));
        total("Parts (incl. bought-in)", std::to_string(summary.partCount));
        total("Bought-in items", std::to_string(boughtIn.itemCount) + " (" +
                                     core::formatLength(boughtIn.totalAreaM2, 4) + " m2)");
        total("Materials", std::to_string(summary.materialCount));
        total("Finished panel area", core::formatLength(summary.finishedAreaM2, 4) + " m2");
        total("Cut area", core::formatLength(summary.cutAreaM2, 4) + " m2");
        total("Edge banding applied", core::formatLength(summary.edgeBandingAppliedM, 3) + " m");
        total("Hardware pieces", std::to_string(summary.hardwareQuantity));
        total("Operations", std::to_string(summary.operationCount));
        if (o.cuttingPlans.size() > 0) total("Cutting-plan sheets", std::to_string(o.cuttingPlans.size()));
        out += "</table>\n<h3>Contents</h3>\n<ol class=\"toc\">\n";
        for (const auto s : sections)
            if (s != JobPackSection::Cover)
                out += "<li><a href=\"#" + std::string(toString(s)) + "\">" + title(s) + "</a></li>\n";
        return out + "</ol>\n";
    }

    std::string plans() const {
        std::string out;
        for (const auto& n : o.planNotes) out += "<p class=\"note\">" + esc(n) + "</p>\n";
        if (o.cuttingPlans.empty()) return out + jobpack::emptyNote();
        for (const auto& p : o.cuttingPlans)
            out += "<div class=\"plan\"><h3>" + esc(p.caption) + "</h3>\n" + extractSvg(p.svg) + "\n</div>\n";
        return out;
    }
};

core::Error invalid(const std::string& message) { return core::makeError(core::ErrorCode::InvalidArgument, message); }

} // namespace

const char* toString(JobPackSection s) noexcept {
    switch (s) {
    case JobPackSection::Cover: return "cover";
    case JobPackSection::Parts: return "parts";
    case JobPackSection::CutList: return "cutlist";
    case JobPackSection::BoughtIn: return "boughtin";
    case JobPackSection::EdgeBanding: return "edgebanding";
    case JobPackSection::Hardware: return "hardware";
    case JobPackSection::Packing: return "packing";
    case JobPackSection::Assembly: return "assembly";
    case JobPackSection::Plans: return "plans";
    case JobPackSection::Warnings: return "warnings";
    }
    return "";
}

std::optional<JobPackSection> jobPackSectionFromString(std::string_view name) noexcept {
    for (const auto s : kAll)
        if (name == toString(s)) return s;
    return std::nullopt;
}

std::vector<JobPackSection> allJobPackSections() { return {kAll.begin(), kAll.end()}; }

core::Result<std::string> jobPackHtml(const domain::Model& model, const JobPackOptions& o) {
    if (o.sections.empty()) return invalid("The job pack needs at least one section (none was selected).");
    std::vector<JobPackSection> sections;
    for (const auto s : kAll)
        for (const auto chosen : o.sections)
            if (chosen == s) {
                sections.push_back(s);
                break;
            }
    auto has = [&](JobPackSection s) {
        for (const auto x : sections)
            if (x == s) return true;
        return false;
    };
    for (const auto& [id, q] : o.itemQuantities)
        if (q < 1)
            return invalid("Quantity of item '" + id.str() + "' is " + std::to_string(q) + "; it must be at least 1.");
    for (const auto& p : o.cuttingPlans)
        if (p.svg.find("<svg") == std::string::npos)
            return invalid("Cutting-plan image '" + p.caption + "' is not an SVG document (no <svg> element).");
    std::optional<PackingList> packing;
    if (has(JobPackSection::Packing)) {
        auto pl = packingList(model, o.packing);
        if (!pl) return pl.error();
        packing = std::move(pl.value());
    }

    Pack pack{model, o, {}, {}};
    const auto summary = projectSummary(model);
    const auto parts = partList(model);
    const auto boughtIn = boughtInList(model);
    // Part-list diagnostics already contain the missing material / edge band findings that the
    // cut list, edge-banding jobs and packing list repeat (the exact duplicates are dropped).
    pack.collect(parts.diagnostics);

    std::string out = "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n<title>" +
                      esc(o.projectName.empty() ? std::string("Job pack") : "Job pack - " + o.projectName) +
                      "</title>\n<style>" + kStyle + "</style>\n</head>\n<body>\n";
    std::string body;
    for (const auto s : sections) {
        std::string content;
        switch (s) {
        case JobPackSection::Cover: content = pack.cover(summary, boughtIn, sections); break;
        case JobPackSection::Parts: content = tableOrNone(partListTable(parts)); break;
        case JobPackSection::CutList: {
            const auto cuts = cutList(model, o.cutList);
            pack.collect(cuts.diagnostics);
            content = cutListByMaterial(cuts);
            break;
        }
        case JobPackSection::BoughtIn:
            pack.collect(boughtIn.diagnostics);
            content = "<p class=\"note\">Ordered finished to size from the supplier; not cut, banded or machined in "
                      "the workshop. Sizes in mm.</p>\n" +
                      tableOrNone(boughtInTable(boughtIn));
            break;
        case JobPackSection::EdgeBanding: {
            const auto jobs = edgeBandingJobs(model, o.edgeBanding);
            pack.collect(jobs.diagnostics);
            content = jobpack::edgeBandingJobsHtml(jobs);
            break;
        }
        case JobPackSection::Hardware: {
            const auto hw = hardwareReport(model);
            pack.collect(hw.diagnostics);
            content = "<p class=\"note\">Total length (m) is given for linear fittings cut to length (rails, "
                      "tracks).</p>\n" +
                      tableOrNone(hardwareTable(hw));
            break;
        }
        case JobPackSection::Packing:
            pack.collect(packing->diagnostics);
            content = jobpack::packingHtml(*packing);
            break;
        case JobPackSection::Assembly: {
            const auto sheets = assemblySheets(model);
            pack.collect(sheets.diagnostics);
            content = jobpack::assemblySheetsHtml(sheets);
            break;
        }
        case JobPackSection::Plans: content = pack.plans(); break;
        case JobPackSection::Warnings: {
            pack.collect(o.extraDiagnostics);
            if (pack.diagnostics.empty()) continue;
            content = "<ul>\n";
            for (const auto& d : pack.diagnostics)
                content += "<li>[" + std::string(d.severity == core::Severity::Error ? "error" : "warning") + "] " +
                           esc(d.message) + "</li>\n";
            content += "</ul>\n";
            break;
        }
        }
        const char* heading = s == JobPackSection::Cover ? "h1" : "h2";
        body += "<section id=\"" + std::string(toString(s)) + "\">\n<" + heading + ">" + title(s) + "</" + heading +
                ">\n" + content + "</section>\n";
    }
    return out + body + "</body>\n</html>\n";
}

} // namespace fos::production
