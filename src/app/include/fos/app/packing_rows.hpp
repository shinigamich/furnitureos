// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// View-model of the Packing workspace (packing lists and assembly sheets). Everything is
/// computed by the production module: production::packingList / packingTable /
/// renderPackageLabelsSvg (fos/production/packing.hpp) and production::assemblySheets /
/// assemblySheetSvg (fos/production/assembly_sheet.hpp). This file only converts editor
/// input to PackingOptions, lays the results out as translated display rows / rich text and
/// writes the export files. Weights are never estimated here: an unknown weight stays unknown.
#include "fos/production/assembly_sheet.hpp"
#include "fos/production/packing.hpp"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <vector>

namespace fos::app {

/// What the packing options bar edits: max weight per package (kg), max outer length / width /
/// height incl. padding (mm, 0 = no limit), padding per side (mm).
struct PackingInput {
    double maxWeightKg = 25.0;
    double maxLength = 0.0;
    double maxWidth = 0.0;
    double maxHeight = 0.0;
    double padding = 10.0;
    bool operator==(const PackingInput&) const = default;
};

production::PackingOptions packingOptions(const PackingInput& input);

/// production::packingList of the whole model. Error = the production module's message
/// (invalid options: max weight not positive finite, a limit or the padding negative).
core::Result<production::PackingList> projectPackingList(const domain::Model& model, const PackingInput& input);

/// One package of one assembly, ready for display.
struct PackageRow {
    QString assembly;   ///< Assembly name.
    QString package;    ///< "1/3" (number within the assembly / package count).
    QString id;         ///< Package id ("<assembly id>-P<n>").
    QString size;       ///< "L × W × H mm" (outer size incl. padding).
    QString weight;     ///< "12.35 kg" or "unknown" (translated).
    bool weightUnknown = false;
    bool overweight = false;
    bool oversize = false;
    QString parts;      ///< "#3 Left side, #4 Right side, ...".
};

/// One hardware bag line.
struct BagRow {
    QString assembly;
    QString bag;        ///< Bag id ("<assembly id>-H").
    QString group;      ///< "Fittings" / "Fasteners" (translated).
    QString kind;       ///< Hardware kind (translated).
    QString name;
    QString article;
    int quantity = 0;
    bool unknownDefinition = false;
};

struct PackingRows {
    std::vector<PackageRow> packages; ///< Assembly order, package order.
    std::vector<BagRow> bags;         ///< Assembly order; fittings then fasteners.
    QStringList warnings;             ///< Diagnostics above Info (production module messages).
};

class PackingText {
    Q_DECLARE_TR_FUNCTIONS(PackingText)
public:
    static PackingRows rows(const production::PackingList& list);
    /// "12.35 kg" or "unknown".
    static QString weight(const std::optional<double>& kg);
    /// "Weight unknown for n package(s): set the density of their materials ..." or empty.
    static QString unknownWeightBanner(const production::PackingList& list);
    static QString stepTitle(production::AssemblyStepKind kind);
    static QString stepInstruction(production::AssemblyStepKind kind);
    /// The generic-order caveat (production::kAssemblyOrderNote) in the UI language.
    static QString assemblyOrderNote();
    /// Rich text (HTML subset of QTextDocument) of all assembly sheets: per assembly the
    /// caveat in a highlighted box, then the numbered steps with their parts and hardware counts.
    static QString assemblyHtml(const production::AssemblySheets& sheets);
};

/// CSV of production::packingTable (UTF-8 with BOM).
std::string packingCsv(const production::PackingList& list);

/// Writes `pages` to `file`; with more than one page as "<stem>-1<ext>", "<stem>-2<ext>", ...
core::Result<std::vector<std::filesystem::path>> writeSvgPages(const std::vector<std::string>& pages,
                                                               const std::filesystem::path& file);
/// production::renderPackageLabelsSvg pages written with writeSvgPages.
core::Result<std::vector<std::filesystem::path>> exportPackageLabels(const production::PackingList& list,
                                                                     const std::string& projectName,
                                                                     const std::filesystem::path& file);
/// One production::assemblySheetSvg per sheet written with writeSvgPages (model order).
/// Error (NotFound) when there are no sheets.
core::Result<std::vector<std::filesystem::path>> exportAssemblySheets(const production::AssemblySheets& sheets,
                                                                      const std::string& projectName,
                                                                      const std::filesystem::path& file);

} // namespace fos::app
