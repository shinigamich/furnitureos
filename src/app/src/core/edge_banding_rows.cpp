// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/edge_banding_rows.hpp"

#include "fos/app/packing_rows.hpp"
#include "fos/production/bought_in.hpp"
#include "fos/production/csv.hpp"
#include "fos/project/outputs.hpp"

namespace fos::app {
namespace {

QString len(double mm) { return QString::fromStdString(core::formatLength(mm)); }
QString metres(double mm) { return QString::fromStdString(core::formatLength(mm / 1000.0, 3)); }
QString str(const std::string& s) { return QString::fromStdString(s); }

QString bandCaption(const std::string& id, const std::string& name, bool missing, double thickness, double width) {
    if (missing) return EdgeBandingText::tr("%1 (MISSING from the library)").arg(str(id));
    return EdgeBandingText::tr("%1 (%2) — %3 × %4 mm").arg(str(name), str(id), len(thickness), len(width));
}

QString severityPrefix(core::Severity s) {
    switch (s) {
    case core::Severity::Error: return EdgeBandingText::tr("Error: ");
    case core::Severity::Warning: return EdgeBandingText::tr("Warning: ");
    case core::Severity::Info: return EdgeBandingText::tr("Note: ");
    }
    return {};
}

} // namespace

production::EdgeBandingJobs projectEdgeBandingJobs(const project::Project& project, const domain::Model& model) {
    return production::edgeBandingJobs(model, project::productionOptions(project).edgeBanding);
}

QStringList EdgeBandingText::columns() {
    return {tr("Item"), tr("Edge"), tr("Step"), tr("Cut edge length"), tr("Edge length"), tr("Strip length")};
}

QString EdgeBandingText::orderNote() {
    return tr("Suggested order (common practice, not a machine requirement): long edges first, then short edges, "
              "so the short-edge band overlaps the ends of the long-edge band.");
}

QString EdgeBandingText::contourNote() { return tr("contour banding - manual / edge bander with contour unit"); }

EdgeBandingJobRows EdgeBandingText::rows(const production::EdgeBandingJobs& jobs) {
    EdgeBandingJobRows out;
    int edges = 0;
    double strip = 0;
    for (const auto& g : jobs.groups) {
        TextNode band;
        band.emphasis = true;
        band.warning = g.missing;
        band.columns = {bandCaption(g.edgeBandId.str(), g.name, g.missing, g.thickness, g.width),
                        tr("%n edge(s)", nullptr, g.edgeCount), QString(), QString(), len(g.totalEdgeLength),
                        len(g.totalStripLength)};
        for (const auto& p : g.parts) {
            TextNode part;
            QString caption = tr("#%1 %2 — cut %3 × %4 × %5 mm")
                                  .arg(p.partNumber)
                                  .arg(str(p.partName), len(p.cutLength), len(p.cutWidth), len(p.thickness));
            if (p.otherBands) caption += tr(" (other edges: another band)");
            part.columns = {caption, tr("%n edge(s)", nullptr, static_cast<int>(p.edges.size()))};
            for (const auto& e : p.edges) {
                TextNode edge;
                edge.columns = {QString(), QString::fromLatin1(domain::toString(e.side)),
                                QStringLiteral("%1/%2").arg(e.step).arg(p.stepsTotal), len(e.cutEdgeLength),
                                len(e.edgeLength), len(e.stripLength)};
                part.children.push_back(std::move(edge));
            }
            band.children.push_back(std::move(part));
        }
        edges += g.edgeCount;
        strip += g.totalStripLength;
        out.bands.push_back(std::move(band));
    }
    for (const auto& c : jobs.contour) {
        TextNode part;
        part.warning = !c.lengthsAvailable;
        QString caption = tr("#%1 %2 — blank %3 × %4 × %5 mm")
                              .arg(c.partNumber)
                              .arg(str(c.partName), len(c.cutLength), len(c.cutWidth), len(c.thickness));
        if (!c.lengthsAvailable) caption += tr(" (segment lengths unavailable)");
        QStringList segments;
        for (const auto& s : c.segments) segments << QString::number(s.segment);
        part.columns = {caption, tr("segments %1").arg(segments.join(QStringLiteral(", ")))};
        int n = 0;
        for (const auto& s : c.strips) {
            TextNode row;
            row.columns = {tr("Strip %1").arg(++n), str(s.edgeBandId.str()), QString(), QString(), len(s.length),
                           len(s.stripLength)};
            part.children.push_back(std::move(row));
        }
        out.contour.push_back(std::move(part));
    }
    for (const auto& t : jobs.contourTotals) {
        if (t.stripCount == 0) continue;
        TextNode total;
        total.emphasis = true;
        total.warning = t.missing;
        total.columns = {tr("Total %1").arg(t.missing ? tr("%1 (MISSING)").arg(str(t.edgeBandId.str()))
                                                      : QStringLiteral("%1 (%2)").arg(str(t.name), str(t.edgeBandId.str()))),
                         tr("%n strip(s)", nullptr, t.stripCount), QString(), QString(), len(t.totalLength),
                         len(t.totalStripLength)};
        strip += t.totalStripLength;
        out.contour.push_back(std::move(total));
    }
    out.summary = tr("Overhang per end: %1 mm · %n band(s) · ", nullptr, static_cast<int>(jobs.groups.size()))
                      .arg(len(jobs.overhangPerEnd)) +
                  tr("%n straight edge(s) · ", nullptr, edges) +
                  tr("%n shaped part(s) · strips total %1 m", nullptr, static_cast<int>(jobs.contour.size()))
                      .arg(metres(strip));
    for (const auto& d : jobs.diagnostics) out.warnings << severityPrefix(d.severity) + str(d.message);
    return out;
}

std::string edgeBandingJobsCsv(const production::EdgeBandingJobs& jobs) {
    production::CsvOptions options;
    options.utf8Bom = true;
    return production::toCsv(production::edgeBandingJobsTable(jobs), options);
}

core::Result<std::vector<std::filesystem::path>> exportEdgeBandingSheets(const production::EdgeBandingJobs& jobs,
                                                                         const std::filesystem::path& file) {
    const auto sheets = production::edgeBandingJobSheetsSvg(jobs);
    std::vector<std::filesystem::path> written;
    for (const auto& sheet : sheets) {
        if (sheet.pages.empty()) continue;
        std::string id = sheet.edgeBandId.str();
        for (char& ch : id) {
            const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                            ch == '-' || ch == '_' || ch == '.';
            if (!ok) ch = '_';
        }
        std::filesystem::path out = file;
        out.replace_filename(file.stem().string() + "-" + id + file.extension().string());
        auto r = writeSvgPages(sheet.pages, out);
        if (!r) return r.error();
        written.insert(written.end(), r.value().begin(), r.value().end());
    }
    if (written.empty()) return core::makeError(core::ErrorCode::NotFound, "No part of the model is edge banded.");
    return written;
}

production::Table BoughtInText::table(const production::BoughtInList& list) {
    production::Table t = production::boughtInTable(list);
    const std::vector<QString> captions{tr("Item"),  tr("Material"),        tr("Width"),
                                        tr("Height"), tr("Thickness"),      tr("Qty"),
                                        tr("Area each (m2)"), tr("Total area (m2)"), tr("Name"),
                                        tr("Part numbers")};
    if (t.header.size() == captions.size())
        for (std::size_t i = 0; i < captions.size(); ++i) t.header[i] = captions[i].toStdString();
    const std::string glass = tr("Glass pane").toStdString();
    for (auto& row : t.rows)
        if (!row.empty() && row[0] == production::kGlassPaneKind) row[0] = glass;
    return t;
}

QString BoughtInText::summary(const production::BoughtInList& list) {
    if (list.rows.empty()) return tr("No bought-in items.");
    return tr("%n bought-in item(s) to order, total area %1 m2", nullptr, list.itemCount)
        .arg(QString::fromStdString(core::formatLength(list.totalAreaM2, 4)));
}

std::string boughtInCsv(const production::BoughtInList& list) {
    production::CsvOptions options;
    options.utf8Bom = true;
    return production::toCsv(BoughtInText::table(list), options);
}

} // namespace fos::app
