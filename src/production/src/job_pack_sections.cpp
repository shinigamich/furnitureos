// SPDX-License-Identifier: AGPL-3.0-only
#include "job_pack_sections.hpp"

#include "fos/core/units.hpp"
#include "fos/production/html.hpp"

namespace fos::production::jobpack {
namespace {

std::string esc(std::string_view s) { return escapeXml(s); }
std::string mm(double v) { return core::formatLength(v); }
std::string metres(double mmValue) { return core::formatLength(mmValue / 1000.0, 3); }

std::string th(std::initializer_list<const char*> cells) {
    std::string out = "<thead><tr>";
    for (const char* c : cells) out += std::string("<th>") + c + "</th>";
    return out + "</tr></thead>\n";
}

std::string td(const std::string& escapedCell, const char* cls = nullptr) {
    return cls ? "<td class=\"" + std::string(cls) + "\">" + escapedCell + "</td>"
               : "<td>" + escapedCell + "</td>";
}

std::string bandTitle(const domain::EdgeBandId& id, const std::string& name, bool missing) {
    return missing ? id.str() + " (MISSING edge band)" : name + " (" + id.str() + ")";
}

} // namespace

std::string emptyNote() { return "<p class=\"empty\">none</p>\n"; }

std::string edgeBandingJobsHtml(const EdgeBandingJobs& jobs) {
    if (jobs.groups.empty() && jobs.contour.empty()) return emptyNote();
    std::string out = "<p class=\"note\">" + esc(kBandingOrderNote) + " Overhang per strip end: " +
                      esc(mm(jobs.overhangPerEnd)) + " mm. Lengths in mm.</p>\n";
    for (const auto& g : jobs.groups) {
        out += "<h3>" + esc(bandTitle(g.edgeBandId, g.name, g.missing));
        if (!g.missing) out += esc(" - " + mm(g.thickness) + " x " + mm(g.width) + " mm");
        out += "</h3>\n<p>" + std::to_string(g.edgeCount) + " edge(s), applied " + esc(metres(g.totalEdgeLength)) +
               " m, strips incl. overhang " + esc(metres(g.totalStripLength)) + " m</p>\n<table>\n" +
               th({"No.", "Part", "Part ID", "Cut L x W", "Edge", "Step", "Cut edge", "Edge length", "Strip length",
                   "Note"}) +
               "<tbody>\n";
        for (const auto& p : g.parts)
            for (std::size_t i = 0; i < p.edges.size(); ++i) {
                const auto& e = p.edges[i];
                out += "<tr>";
                if (i == 0) {
                    const std::string span = std::to_string(p.edges.size());
                    out += "<td rowspan=\"" + span + "\">" + std::to_string(p.partNumber) + "</td><td rowspan=\"" +
                           span + "\">" + esc(p.partName) + "</td><td rowspan=\"" + span + "\">" +
                           esc(p.partId.str()) + "</td><td rowspan=\"" + span + "\">" +
                           esc(mm(p.cutLength) + " x " + mm(p.cutWidth)) + "</td>";
                }
                out += td(esc(domain::toString(e.side))) +
                       td(std::to_string(e.step) + "/" + std::to_string(p.stepsTotal)) + td(esc(mm(e.cutEdgeLength))) +
                       td(esc(mm(e.edgeLength))) + td(esc(mm(e.stripLength))) +
                       td(i == 0 && p.otherBands ? "other edges carry another band" : "");
                out += "</tr>\n";
            }
        out += "</tbody>\n</table>\n";
    }
    if (!jobs.contour.empty()) {
        out += "<h3>Shaped parts: " + esc(kContourBandingNote) + "</h3>\n<table>\n" +
               th({"No.", "Part", "Part ID", "Blank L x W", "Band", "Strip", "Applied length", "Strip length"}) +
               "<tbody>\n";
        for (const auto& c : jobs.contour) {
            const std::string head = td(std::to_string(c.partNumber)) + td(esc(c.partName)) + td(esc(c.partId.str())) +
                                     td(esc(mm(c.cutLength) + " x " + mm(c.cutWidth)));
            if (c.strips.empty()) {
                out += "<tr>" + head + td("") + td("") + td("lengths unavailable (see warnings)") + td("") + "</tr>\n";
                continue;
            }
            for (std::size_t i = 0; i < c.strips.size(); ++i) {
                const auto& s = c.strips[i];
                out += "<tr>" + head + td(esc(s.edgeBandId.str())) + td(std::to_string(i + 1)) + td(esc(mm(s.length))) +
                       td(esc(mm(s.stripLength))) + "</tr>\n";
            }
        }
        out += "</tbody>\n</table>\n";
        for (const auto& t : jobs.contourTotals)
            out += "<p>" + esc(bandTitle(t.edgeBandId, t.name, t.missing)) + ": " + std::to_string(t.stripCount) +
                   " contour strip(s), applied " + esc(metres(t.totalLength)) + " m, strips " +
                   esc(metres(t.totalStripLength)) + " m</p>\n";
    }
    return out;
}

std::string packingHtml(const PackingList& list) {
    if (list.assemblies.empty()) return emptyNote();
    std::string out = "<p class=\"note\">Packages: panels stacked flat, largest at the bottom (simple first-fit "
                      "heuristic); size = largest panel + 2 x " +
                      esc(mm(list.options.padding)) + " mm padding; weight limit " +
                      esc(core::formatLength(list.options.maxPackageWeightKg, 2)) +
                      " kg. Weights are panels only, from material densities; never estimated.</p>\n";
    for (const auto& a : list.assemblies) {
        out += "<h3>" + esc(a.assemblyName) + " (" + esc(a.assemblyId.str()) + ")</h3>\n";
        if (!a.packages.empty()) {
            out += "<table>\n" +
                   th({"Package", "Size L x W x H (mm)", "Weight", "No.", "Panel", "Material", "L x W x T (mm)",
                       "Panel weight", "Packed"}) +
                   "<tbody>\n";
            for (const auto& p : a.packages) {
                std::string flags;
                if (p.overweight) flags += " OVERWEIGHT";
                if (p.oversize) flags += " OVERSIZE";
                for (std::size_t i = 0; i < p.parts.size(); ++i) {
                    const auto& part = p.parts[i];
                    out += "<tr>";
                    if (i == 0) {
                        const std::string span = std::to_string(p.parts.size());
                        out += "<td rowspan=\"" + span + "\"><strong>" + esc(p.id) + "</strong>" + esc(flags) +
                               "</td><td rowspan=\"" + span + "\">" +
                               esc(mm(p.length) + " x " + mm(p.width) + " x " + mm(p.height)) + "</td><td rowspan=\"" +
                               span + "\">" + esc(weightText(p.weightKg)) + "</td>";
                    }
                    out += td(std::to_string(part.partNumber)) + td(esc(part.name)) + td(esc(part.materialName)) +
                           td(esc(mm(part.length) + " x " + mm(part.width) + " x " + mm(part.thickness))) +
                           td(esc(weightText(part.weightKg))) + td("<span class=\"box\"></span>", "c") + "</tr>\n";
                }
            }
            out += "</tbody>\n</table>\n";
        }
        if (!a.bag.empty()) {
            out += "<p><strong>Hardware bag " + esc(a.bag.id) + "</strong></p>\n<table>\n" +
                   th({"Group", "Hardware", "Article", "Kind", "Qty", "Total length (m)", "Packed"}) + "<tbody>\n";
            for (const auto& [group, lines] :
                 {std::pair{"Fittings", &a.bag.fittings}, std::pair{"Fasteners", &a.bag.fasteners}})
                for (const auto& l : *lines)
                    out += "<tr>" + td(group) +
                           td(esc(l.unknownDefinition ? l.name + " (UNKNOWN DEFINITION)" : l.name)) + td(esc(l.article)) +
                           td(esc(domain::toString(l.kind))) + td(std::to_string(l.quantity)) +
                           td(l.totalLengthM > 0 ? esc(core::formatLength(l.totalLengthM, 3)) : "") +
                           td("<span class=\"box\"></span>", "c") + "</tr>\n";
            out += "</tbody>\n</table>\n";
        }
    }
    return out;
}

std::string assemblySheetsHtml(const AssemblySheets& sheets) {
    if (sheets.sheets.empty()) return emptyNote();
    std::string out = "<p class=\"note\">" + esc(kAssemblyOrderNote) + "</p>\n";
    for (const auto& s : sheets.sheets) {
        out += "<div class=\"item\"><h3>" + esc(s.assemblyName) + " (" + esc(s.assemblyId.str()) + ")</h3>\n<ol>\n";
        for (const auto& step : s.steps) {
            out += "<li><strong>" + esc(step.title) + "</strong> - " + esc(step.instruction) + "\n";
            if (!step.parts.empty()) {
                out += "<br>Parts: ";
                for (std::size_t i = 0; i < step.parts.size(); ++i)
                    out += (i ? ", " : "") + std::string("#") + std::to_string(step.parts[i].partNumber) + " " +
                           esc(step.parts[i].name);
                out += "\n";
            }
            if (!step.hardware.empty()) {
                out += "<br>Hardware: ";
                for (std::size_t i = 0; i < step.hardware.size(); ++i) {
                    const auto& h = step.hardware[i];
                    out += (i ? ", " : "") + std::to_string(h.quantity) + " x " + esc(h.name) +
                           (h.unknownDefinition ? " (UNKNOWN DEFINITION)" : "");
                }
                out += "\n";
            }
            out += "</li>\n";
        }
        out += "</ol></div>\n";
    }
    return out;
}

} // namespace fos::production::jobpack
