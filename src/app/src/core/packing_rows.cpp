// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/packing_rows.hpp"

#include "fos/app/library_editor.hpp"
#include "fos/production/csv.hpp"

namespace fos::app {
namespace {

/// Up to 1 decimal, trailing zeros removed, locale-independent ("620", "620.5").
QString mm(double v) {
    QString t = QString::number(v, 'f', 1);
    if (t.endsWith(QStringLiteral(".0"))) t.chop(2);
    return t;
}

QString qs(const std::string& s) { return QString::fromStdString(s); }

void addBagLines(PackingRows& out, const production::AssemblyPacking& a, const std::vector<production::BagLine>& lines,
                 const QString& group) {
    for (const auto& l : lines) {
        BagRow r;
        r.assembly = qs(a.assemblyName);
        r.bag = qs(a.bag.id);
        r.group = group;
        r.kind = LibraryText::hardwareKind(l.kind);
        r.name = qs(l.name);
        r.article = qs(l.article);
        r.quantity = l.quantity;
        r.unknownDefinition = l.unknownDefinition;
        out.bags.push_back(std::move(r));
    }
}

} // namespace

production::PackingOptions packingOptions(const PackingInput& in) {
    production::PackingOptions o;
    o.maxPackageWeightKg = in.maxWeightKg;
    o.maxPackageLength = in.maxLength;
    o.maxPackageWidth = in.maxWidth;
    o.maxPackageHeight = in.maxHeight;
    o.padding = in.padding;
    return o;
}

core::Result<production::PackingList> projectPackingList(const domain::Model& model, const PackingInput& input) {
    return production::packingList(model, packingOptions(input));
}

QString PackingText::weight(const std::optional<double>& kg) {
    if (!kg) return tr("unknown");
    return tr("%1 kg").arg(QString::number(*kg, 'f', 2));
}

PackingRows PackingText::rows(const production::PackingList& list) {
    PackingRows out;
    for (const auto& a : list.assemblies) {
        const QString count = QString::number(a.packages.size());
        for (const auto& p : a.packages) {
            PackageRow r;
            r.assembly = qs(a.assemblyName);
            r.package = QStringLiteral("%1/%2").arg(p.number).arg(count);
            r.id = qs(p.id);
            r.size = tr("%1 × %2 × %3 mm").arg(mm(p.length), mm(p.width), mm(p.height));
            r.weight = weight(p.weightKg);
            r.weightUnknown = !p.weightKg.has_value();
            r.overweight = p.overweight;
            r.oversize = p.oversize;
            QStringList parts;
            for (const auto& part : p.parts) parts << QStringLiteral("#%1 %2").arg(part.partNumber).arg(qs(part.name));
            r.parts = parts.join(QStringLiteral(", "));
            out.packages.push_back(std::move(r));
        }
        addBagLines(out, a, a.bag.fittings, tr("Fittings"));
        addBagLines(out, a, a.bag.fasteners, tr("Fasteners"));
    }
    for (const auto& d : list.diagnostics)
        if (d.severity != core::Severity::Info) out.warnings << qs(d.message);
    return out;
}

QString PackingText::unknownWeightBanner(const production::PackingList& list) {
    int unknown = 0;
    for (const auto& a : list.assemblies)
        for (const auto& p : a.packages)
            if (!p.weightKg) ++unknown;
    if (unknown == 0) return {};
    return tr("Weight unknown for %n package(s): set the density of their materials in Project > Libraries "
              "(weights are never estimated).",
              nullptr, unknown);
}

QString PackingText::stepTitle(production::AssemblyStepKind kind) {
    using K = production::AssemblyStepKind;
    switch (kind) {
    case K::Carcass: return tr("Carcass");
    case K::Back: return tr("Back panel");
    case K::ShelvesAndDividers: return tr("Shelves and dividers");
    case K::DrawerBoxes: return tr("Drawer boxes");
    case K::Fronts: return tr("Doors and drawer fronts");
    case K::Handles: return tr("Handles");
    case K::Other: return tr("Other parts");
    }
    return {};
}

QString PackingText::stepInstruction(production::AssemblyStepKind kind) {
    using K = production::AssemblyStepKind;
    switch (kind) {
    case K::Carcass:
        return tr("Join the sides, bottom and top (and rails / plinth) with the listed fasteners; fit feet or legs. "
                  "Check that the carcass is square (equal diagonals).");
    case K::Back: return tr("Fit the back panel; it holds the carcass square, so check the diagonals first.");
    case K::ShelvesAndDividers: return tr("Insert the shelf supports, then fit the shelves and dividers.");
    case K::DrawerBoxes: return tr("Assemble the drawer boxes and mount the drawer slides.");
    case K::Fronts: return tr("Mount hinges / fittings, hang the doors and drawer fronts and adjust the gaps.");
    case K::Handles: return tr("Fit the handles.");
    case K::Other: return tr("Fit the remaining parts and hardware.");
    }
    return {};
}

QString PackingText::assemblyOrderNote() {
    return tr("Generic step order derived from part roles and hardware kinds (carcass, back, shelves/dividers, drawer "
              "boxes, fronts, handles); not a verified assembly instruction for this design.");
}

QString PackingText::assemblyHtml(const production::AssemblySheets& sheets) {
    QString h;
    const QString note = assemblyOrderNote().toHtmlEscaped();
    for (const auto& sheet : sheets.sheets) {
        h += QStringLiteral("<h2>%1</h2>").arg(qs(sheet.assemblyName).toHtmlEscaped());
        h += QStringLiteral("<table width=\"100%\" cellpadding=\"6\" bgcolor=\"#fff3cd\"><tr><td>"
                            "<b>%1</b> %2</td></tr></table>")
                 .arg(tr("Note:").toHtmlEscaped(), note);
        for (const auto& step : sheet.steps) {
            h += QStringLiteral("<h3>%1</h3>").arg(tr("Step %1: %2").arg(step.number).arg(stepTitle(step.kind)).toHtmlEscaped());
            h += QStringLiteral("<p>%1</p>").arg(stepInstruction(step.kind).toHtmlEscaped());
            if (!step.parts.empty()) {
                h += QStringLiteral("<p><b>%1</b></p><ul>").arg(tr("Parts:").toHtmlEscaped());
                for (const auto& p : step.parts)
                    h += QStringLiteral("<li>#%1 %2</li>").arg(p.partNumber).arg(qs(p.name).toHtmlEscaped());
                h += QStringLiteral("</ul>");
            }
            if (!step.hardware.empty()) {
                h += QStringLiteral("<p><b>%1</b></p><ul>").arg(tr("Hardware:").toHtmlEscaped());
                for (const auto& hw : step.hardware)
                    h += QStringLiteral("<li><b>%1 ×</b> %2 (%3)</li>")
                             .arg(hw.quantity)
                             .arg(qs(hw.name).toHtmlEscaped(), LibraryText::hardwareKind(hw.kind).toHtmlEscaped());
                h += QStringLiteral("</ul>");
            }
        }
    }
    if (sheets.sheets.empty()) h = QStringLiteral("<p>%1</p>").arg(tr("There are no assemblies with parts or hardware.").toHtmlEscaped());
    for (const auto& d : sheets.diagnostics)
        if (d.severity != core::Severity::Info)
            h += QStringLiteral("<p style=\"color:#c62828\">%1</p>").arg(qs(d.message).toHtmlEscaped());
    return h;
}

std::string packingCsv(const production::PackingList& list) {
    production::CsvOptions options;
    options.utf8Bom = true; // spreadsheet programs detect UTF-8
    return production::toCsv(production::packingTable(list), options);
}

core::Result<std::vector<std::filesystem::path>> writeSvgPages(const std::vector<std::string>& pages,
                                                               const std::filesystem::path& file) {
    std::vector<std::filesystem::path> written;
    for (std::size_t i = 0; i < pages.size(); ++i) {
        std::filesystem::path out = file;
        if (pages.size() > 1)
            out.replace_filename(file.stem().string() + "-" + std::to_string(i + 1) + file.extension().string());
        if (auto r = production::writeTextFile(out, pages[i]); !r) return r.error();
        written.push_back(out);
    }
    return written;
}

core::Result<std::vector<std::filesystem::path>> exportPackageLabels(const production::PackingList& list,
                                                                     const std::string& projectName,
                                                                     const std::filesystem::path& file) {
    auto pages = production::renderPackageLabelsSvg(list, projectName);
    if (!pages) return pages.error();
    if (pages.value().empty()) return core::makeError(core::ErrorCode::NotFound, "There are no packages to label.");
    return writeSvgPages(pages.value(), file);
}

core::Result<std::vector<std::filesystem::path>> exportAssemblySheets(const production::AssemblySheets& sheets,
                                                                      const std::string& projectName,
                                                                      const std::filesystem::path& file) {
    if (sheets.sheets.empty())
        return core::makeError(core::ErrorCode::NotFound, "There are no assemblies with parts or hardware.");
    std::vector<std::string> pages;
    for (const auto& sheet : sheets.sheets) pages.push_back(production::assemblySheetSvg(sheet, projectName));
    return writeSvgPages(pages, file);
}

} // namespace fos::app
