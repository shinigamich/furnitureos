// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// View-model of the project library editor (Project > Libraries…): list rows, form
/// validation, New / Duplicate templates and the undoable edits. Every change is one of the
/// project library commands (commands::upsertMaterial / upsertEdgeBand / upsertHardware /
/// remove*), so each Apply or Delete is exactly one undo step; removal of an entry still in
/// use is refused by the command (its message names the items). Library files (`.foslib`)
/// are read and written with formats/library.hpp. Lengths are mm, costs are in the project
/// currency (Metadata::currency).
#include "fos/formats/library.hpp"
#include "fos/project/commands.hpp"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fos::app {

class Session;

using LibraryCategory = formats::LibraryCategory;

/// One entry of a library list, in library order.
struct LibraryRow {
    std::string id;
    std::string name;
    /// Ids of the project items that reference the entry (project order); empty = unused.
    std::vector<std::string> usedBy;
};
std::vector<LibraryRow> libraryRows(const project::Project& project, LibraryCategory category);

/// What the hardware form may change; kind, dimensionsVerified and the drilling parameters
/// (`params`) of the definition are never edited here.
struct HardwareInput {
    std::string name;
    std::string manufacturer;
    std::string code; ///< HardwareDefinition::article.
    double unitCost = 0.0;
    bool operator==(const HardwareInput&) const = default;
};
HardwareInput hardwareInput(const domain::HardwareDefinition& definition);
/// `definition` with only the HardwareInput fields replaced.
domain::HardwareDefinition applyHardwareInput(domain::HardwareDefinition definition, const HardwareInput& input);

/// "#rrggbb" (lower case) of a display colour, and the inverse (also "rrggbb", any case;
/// std::nullopt for anything else).
std::string colorText(const domain::Color& color);
std::optional<domain::Color> parseColorText(const std::string& text);

/// Unused id "<prefix><n>" (n = 1, 2, ...) among `taken`.
std::string uniqueLibraryId(const std::string& prefix, const std::vector<std::string>& taken);
/// Ids of one library list.
std::vector<std::string> libraryIds(const project::Project& project, LibraryCategory category);

/// Form contents for New: domain defaults with a fresh id ("material<n>", "edge<n>") and the
/// name "New material" / "New edge band" (translated).
domain::Material newMaterial(const project::Project& project);
domain::EdgeBand newEdgeBand(const project::Project& project);
/// Copies of an entry under a fresh id ("<id>-copy", "<id>-copy2", ...) and the name
/// "<name> (copy)"; every other field (hardware: kind, params, verification) is kept.
domain::Material duplicateMaterial(const project::Project& project, const domain::Material& source);
domain::EdgeBand duplicateEdgeBand(const project::Project& project, const domain::EdgeBand& source);
domain::HardwareDefinition duplicateHardware(const project::Project& project, const domain::HardwareDefinition& source);

/// Validation of a form before Apply. `isNew` = the entry is not yet in the library: its id
/// must be non-empty, without leading/trailing spaces and unused in that list. Returns the
/// translated problems (empty = valid): empty name, lengths that are not finite and > 0 mm,
/// costs and a material density (kg/m³, 0 = unknown) that are not finite and >= 0, an edge band narrower than its thickness.
QStringList validateMaterial(const project::Project& project, const domain::Material& material, bool isNew);
QStringList validateEdgeBand(const project::Project& project, const domain::EdgeBand& edgeBand, bool isNew);
QStringList validateHardware(const project::Project& project, const domain::HardwareDefinition& definition, bool isNew);

/// Outcome of an Apply / Delete: empty = done (one undo step), else the messages to show
/// (validation problems, or the command's error verbatim).
using LibraryEditResult = QStringList;
/// Validate, then execute commands::upsertMaterial / upsertEdgeBand / upsertHardware.
LibraryEditResult applyMaterial(Session& session, const domain::Material& material, bool isNew);
LibraryEditResult applyEdgeBand(Session& session, const domain::EdgeBand& edgeBand, bool isNew);
/// The caller builds `definition` with applyHardwareInput() from the stored definition (or from
/// duplicateHardware()), so kind, params and verification come from the library.
LibraryEditResult applyHardware(Session& session, const domain::HardwareDefinition& definition, bool isNew);
/// commands::removeMaterial / removeEdgeBand / removeHardware; refused while in use.
LibraryEditResult removeLibraryEntry(Session& session, LibraryCategory category, const std::string& id);

/// The project libraries as a formats::Library.
formats::Library projectLibrary(const project::Project& project);
/// Writes the project libraries to a `.foslib` file (formats::exportLibrary).
core::Result<void> exportProjectLibrary(const project::Project& project, const std::filesystem::path& path);

/// What importing `incoming` with `policy` would do to the project libraries (formats::mergeLibrary
/// on a copy; renamed entries get "<id>-imported", "<id>-imported2", ...).
core::Result<formats::MergeReport> previewLibraryImport(const project::Project& project, const formats::Library& incoming,
                                                        formats::ConflictPolicy policy);
/// ONE undoable command (label "Import library {0}", `sourceName`) merging `incoming` into the
/// project libraries: every added, replaced or renamed entry is written through
/// commands::upsertMaterial / upsertEdgeBand / upsertHardware; skipped entries are untouched.
project::Command importLibraryCommand(formats::Library incoming, formats::ConflictPolicy policy, std::string sourceName);

/// Translated texts of the library editor.
class LibraryText {
    Q_DECLARE_TR_FUNCTIONS(LibraryText)
public:
    static QString materialKind(domain::MaterialKind kind);
    static QString edgeBandKind(domain::EdgeBandKind kind);
    static QString hardwareKind(domain::HardwareKind kind);
    static std::vector<std::pair<QString, domain::MaterialKind>> materialKinds();
    static std::vector<std::pair<QString, domain::EdgeBandKind>> edgeBandKinds();
    /// "Name (id)", plus " — used by n item(s)" when in use.
    static QString rowCaption(const LibraryRow& row);
    /// "n added, n replaced, n renamed, n skipped".
    static QString mergeSummary(const formats::MergeReport& report);
    static QString newMaterialName();
    static QString newEdgeBandName();
    static QString copyName(const std::string& name);
};

} // namespace fos::app
