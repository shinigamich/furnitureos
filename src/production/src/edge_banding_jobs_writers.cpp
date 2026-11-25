// SPDX-License-Identifier: AGPL-3.0-only
// Text, table (CSV) and JSON renderings of the edge-banding job list.
#include "fos/production/edge_banding_jobs.hpp"

#include "fos/core/units.hpp"

#include <cstdio>

namespace fos::production {
namespace {

using core::formatLength;

std::string metres(double mm) { return formatLength(mm / 1000.0, 3) + " m"; }

std::string bandTitle(const domain::EdgeBandId& id, const std::string& name, bool missing) {
    return id.str() + (missing ? " (MISSING from the model)" : " '" + name + "'");
}

std::string partTitle(int number, const std::string& name, const domain::PartId& id) {
    return "#" + std::to_string(number) + " " + name + " (" + id.str() + ")";
}

std::string jsonString(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
                o += buf;
            } else {
                o += static_cast<char>(c);
            }
        }
    }
    return o + "\"";
}

/// JSON number in mm, up to 6 decimals, trailing zeros stripped.
std::string num(double v) { return formatLength(v, 6); }

std::string optNum(const std::optional<double>& v) { return v ? num(*v) : "null"; }

} // namespace

std::string edgeBandingJobsText(const EdgeBandingJobs& jobs) {
    std::string s = "Edge banding jobs (overhang " + formatLength(jobs.overhangPerEnd) + " mm per strip end)\n";
    s += std::string(kBandingOrderNote) + "\n";
    if (jobs.groups.empty() && jobs.contour.empty()) s += "\nNo banded edges.\n";
    for (const auto& g : jobs.groups) {
        s += "\nBand " + bandTitle(g.edgeBandId, g.name, g.missing);
        if (!g.missing) s += " " + formatLength(g.thickness) + " x " + formatLength(g.width) + " mm";
        s += ": " + std::to_string(g.parts.size()) + " part(s), " + std::to_string(g.edgeCount) +
             " edge(s), edge length " + metres(g.totalEdgeLength) + ", strip length incl. overhang " +
             metres(g.totalStripLength) + "\n";
        for (const auto& p : g.parts) {
            s += "  " + partTitle(p.partNumber, p.partName, p.partId) + "  cut " + formatLength(p.cutLength) +
                 " x " + formatLength(p.cutWidth) + "  finished " + formatLength(p.length) + " x " +
                 formatLength(p.width) + (p.otherBands ? "  (other edges: other band)" : "") + "\n";
            for (const auto& e : p.edges)
                s += "     step " + std::to_string(e.step) + "/" + std::to_string(p.stepsTotal) + "  " +
                     domain::toString(e.side) + "  cut edge " + formatLength(e.cutEdgeLength) + "  edge " +
                     formatLength(e.edgeLength) + "  strip " + formatLength(e.stripLength) + "\n";
        }
    }
    if (!jobs.contour.empty()) {
        s += "\nShaped parts: " + std::string(kContourBandingNote) + "\n";
        for (const auto& c : jobs.contour) {
            s += "  " + partTitle(c.partNumber, c.partName, c.partId) + "  blank " + formatLength(c.cutLength) +
                 " x " + formatLength(c.cutWidth) +
                 (c.routed ? "  routed outline" : "  rectangular outline (per-segment bands)") + "\n";
            if (!c.lengthsAvailable) s += "     lengths unavailable (see diagnostics)\n";
            int n = 0;
            for (const auto& st : c.strips)
                s += "     strip " + std::to_string(++n) + "  " + st.edgeBandId.str() + "  length " +
                     formatLength(st.length) + "  strip " + formatLength(st.stripLength) + "\n";
            std::string seg = "     banded segments:";
            for (const auto& sb : c.segments)
                seg += " " + std::to_string(sb.segment) + "=" + sb.edgeBandId.str() +
                       (sb.length ? "/" + formatLength(*sb.length) : std::string());
            s += seg + "\n";
        }
        for (const auto& t : jobs.contourTotals)
            s += "  total " + bandTitle(t.edgeBandId, t.name, t.missing) + ": " + std::to_string(t.stripCount) +
                 " strip(s), length " + metres(t.totalLength) + ", strip length incl. overhang " +
                 metres(t.totalStripLength) + "\n";
    }
    return s;
}

Table edgeBandingJobsTable(const EdgeBandingJobs& jobs) {
    Table t;
    t.header = {"Band",      "Band name", "Band thickness", "Band width", "Kind",         "No.",         "Part ID", "Part",
                "Cut length", "Cut width", "Edge",           "Step",       "Cut edge length", "Edge length", "Strip length"};
    for (const auto& g : jobs.groups) {
        const std::string th = g.missing ? "" : formatLength(g.thickness);
        const std::string wd = g.missing ? "" : formatLength(g.width);
        for (const auto& p : g.parts)
            for (const auto& e : p.edges)
                t.rows.push_back({g.edgeBandId.str(), g.name, th, wd, "edge", std::to_string(p.partNumber),
                                  p.partId.str(), p.partName, formatLength(p.cutLength), formatLength(p.cutWidth),
                                  domain::toString(e.side),
                                  std::to_string(e.step) + "/" + std::to_string(p.stepsTotal),
                                  formatLength(e.cutEdgeLength), formatLength(e.edgeLength),
                                  formatLength(e.stripLength)});
    }
    for (const auto& c : jobs.contour) {
        int n = 0;
        for (const auto& st : c.strips) {
            std::string name, th, wd;
            for (const auto& tot : jobs.contourTotals)
                if (tot.edgeBandId == st.edgeBandId) name = tot.name;
            t.rows.push_back({st.edgeBandId.str(), name, "", "", "contour", std::to_string(c.partNumber),
                              c.partId.str(), c.partName, formatLength(c.cutLength), formatLength(c.cutWidth),
                              "strip" + std::to_string(++n), "", "", formatLength(st.length),
                              formatLength(st.stripLength)});
        }
        if (!c.lengthsAvailable)
            t.rows.push_back({c.segments.front().edgeBandId.str(), "", "", "", "contour", std::to_string(c.partNumber),
                              c.partId.str(), c.partName, formatLength(c.cutLength), formatLength(c.cutWidth),
                              "lengths unavailable", "", "", "", ""});
    }
    return t;
}

std::string edgeBandingJobsJson(const EdgeBandingJobs& jobs) {
    std::string s = "{\n  \"overhangPerEnd\": " + num(jobs.overhangPerEnd) + ",\n  \"orderNote\": " +
                    jsonString(kBandingOrderNote) + ",\n  \"groups\": [";
    for (std::size_t gi = 0; gi < jobs.groups.size(); ++gi) {
        const auto& g = jobs.groups[gi];
        s += std::string(gi ? "," : "") + "\n    {\"edgeBandId\": " + jsonString(g.edgeBandId.str()) +
             ", \"name\": " + jsonString(g.name) + ", \"missing\": " + (g.missing ? "true" : "false") +
             ", \"thickness\": " + num(g.thickness) + ", \"width\": " + num(g.width) +
             ", \"edgeCount\": " + std::to_string(g.edgeCount) + ", \"totalEdgeLength\": " +
             num(g.totalEdgeLength) + ", \"totalStripLength\": " + num(g.totalStripLength) + ",\n     \"parts\": [";
        for (std::size_t pi = 0; pi < g.parts.size(); ++pi) {
            const auto& p = g.parts[pi];
            s += std::string(pi ? "," : "") + "\n      {\"partNumber\": " + std::to_string(p.partNumber) +
                 ", \"partId\": " + jsonString(p.partId.str()) + ", \"partName\": " + jsonString(p.partName) +
                 ", \"assemblyId\": " + jsonString(p.assemblyId.str()) + ", \"length\": " + num(p.length) +
                 ", \"width\": " + num(p.width) + ", \"cutLength\": " + num(p.cutLength) + ", \"cutWidth\": " +
                 num(p.cutWidth) + ", \"thickness\": " + num(p.thickness) + ", \"stepsTotal\": " +
                 std::to_string(p.stepsTotal) + ", \"otherBands\": " + (p.otherBands ? "true" : "false") +
                 ", \"edges\": [";
            for (std::size_t ei = 0; ei < p.edges.size(); ++ei) {
                const auto& e = p.edges[ei];
                s += std::string(ei ? ", " : "") + "{\"side\": " + jsonString(domain::toString(e.side)) +
                     ", \"step\": " + std::to_string(e.step) + ", \"cutEdgeLength\": " + num(e.cutEdgeLength) +
                     ", \"edgeLength\": " + num(e.edgeLength) + ", \"stripLength\": " + num(e.stripLength) + "}";
            }
            s += "]}";
        }
        s += "]}";
    }
    s += "],\n  \"contourNote\": " + jsonString(kContourBandingNote) + ",\n  \"contour\": [";
    for (std::size_t ci = 0; ci < jobs.contour.size(); ++ci) {
        const auto& c = jobs.contour[ci];
        s += std::string(ci ? "," : "") + "\n    {\"partNumber\": " + std::to_string(c.partNumber) +
             ", \"partId\": " + jsonString(c.partId.str()) + ", \"partName\": " + jsonString(c.partName) +
             ", \"assemblyId\": " + jsonString(c.assemblyId.str()) + ", \"cutLength\": " + num(c.cutLength) +
             ", \"cutWidth\": " + num(c.cutWidth) + ", \"thickness\": " + num(c.thickness) +
             ", \"routed\": " + (c.routed ? "true" : "false") +
             ", \"lengthsAvailable\": " + (c.lengthsAvailable ? "true" : "false") + ",\n     \"segments\": [";
        for (std::size_t i = 0; i < c.segments.size(); ++i)
            s += std::string(i ? ", " : "") + "{\"segment\": " + std::to_string(c.segments[i].segment) +
                 ", \"edgeBandId\": " + jsonString(c.segments[i].edgeBandId.str()) +
                 ", \"length\": " + optNum(c.segments[i].length) + "}";
        s += "],\n     \"strips\": [";
        for (std::size_t i = 0; i < c.strips.size(); ++i)
            s += std::string(i ? ", " : "") + "{\"edgeBandId\": " + jsonString(c.strips[i].edgeBandId.str()) +
                 ", \"length\": " + num(c.strips[i].length) + ", \"stripLength\": " + num(c.strips[i].stripLength) +
                 "}";
        s += "]}";
    }
    s += "],\n  \"contourTotals\": [";
    for (std::size_t i = 0; i < jobs.contourTotals.size(); ++i) {
        const auto& t = jobs.contourTotals[i];
        s += std::string(i ? "," : "") + "\n    {\"edgeBandId\": " + jsonString(t.edgeBandId.str()) +
             ", \"name\": " + jsonString(t.name) + ", \"missing\": " + (t.missing ? "true" : "false") +
             ", \"stripCount\": " + std::to_string(t.stripCount) + ", \"totalLength\": " + num(t.totalLength) +
             ", \"totalStripLength\": " + num(t.totalStripLength) + "}";
    }
    s += "],\n  \"diagnostics\": [";
    for (std::size_t i = 0; i < jobs.diagnostics.size(); ++i) {
        const auto& d = jobs.diagnostics[i];
        s += std::string(i ? "," : "") + "\n    {\"severity\": " +
             jsonString(d.severity == core::Severity::Error     ? "error"
                        : d.severity == core::Severity::Warning ? "warning"
                                                                : "info") +
             ", \"code\": " + jsonString(d.code) + ", \"message\": " + jsonString(d.message) +
             ", \"entity\": " + jsonString(d.entityId) + "}";
    }
    s += "]\n}\n";
    return s;
}

} // namespace fos::production
