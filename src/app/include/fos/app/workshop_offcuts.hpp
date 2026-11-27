// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Cutting workspace view-model for the workshop offcut stock (`.fosremnants`,
/// project/remnant_store.hpp and remnant_use.hpp): which store remnants a cutting run offered,
/// skipped or used, and the planned change of the stock after cutting ("Add offcuts to
/// workshop stock"). Every decision is made by project::cuttingPlansWithStore() and
/// project::commitStoreUse(); this file only presents them.
#include "fos/core/result.hpp"
#include "fos/project/remnant_use.hpp"

#include <QDate>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <string>
#include <vector>

namespace fos::app {

/// QSettings keys: path of the chosen `.fosremnants` file and whether the cutting workspace
/// offers its remnants.
inline constexpr const char* kWorkshopStoreFileKey = "cutting/workshopRemnantsFile";
inline constexpr const char* kUseWorkshopStoreKey = "cutting/useWorkshopRemnants";

/// Loads the stock file. A file that does not exist yet gives an empty store when
/// `allowMissing` (it is created by the first save); otherwise the loader's error.
core::Result<project::RemnantStore> loadWorkshopStore(const std::filesystem::path& path, bool allowMissing);

/// The cutting run with the store remnants offered: project::cuttingPlansWithStore() with the
/// project's cutting settings; remnants reserved by the project name may be used.
project::StoreCuttingRun workshopCuttingRun(const project::Resolved& resolved, const project::Project& project,
                                            const project::RemnantStore& store);

/// A run without store remnants (plain project::cuttingPlans() result): nothing offered or used.
project::StoreCuttingRun plainCuttingRun(const project::CuttingPlans& plans);

/// One store remnant of a material the project cuts, store order.
struct WorkshopRemnantRow {
    enum class Status { Used, OfferedUnused, Skipped };
    std::string id;
    std::string materialId;
    QString size;   ///< "L × W × T mm".
    Status status = Status::OfferedUnused;
    QString detail; ///< Translated: "used for 3 pieces (sheet 2)", "offered, not needed", the skip reason.
};
std::vector<WorkshopRemnantRow> workshopRemnantRows(const project::RemnantStore& store,
                                                    const project::StoreCuttingRun& run);
/// Translated one-line summary: "4 offcuts offered, 1 used, 2 skipped".
QString workshopRunSummary(const project::StoreCuttingRun& run);

/// Options for the new stock entries: dateAdded = `today` ("YYYY-MM-DD"), source "offcut of
/// '<project name>' (<file name>)" (without the file part for an untitled project); the
/// minimum offcut sizes keep the project::OffcutImportOptions defaults.
project::OffcutImportOptions offcutImportOptions(const project::Project& project,
                                                 const std::filesystem::path& projectFile, const QDate& today);

/// Translated listing of a planned stock change, removed entries first: "Remove (used): <line>"
/// and "Add: <line>" (project::describeRemnant). Empty when nothing changes.
QStringList storeChangeLines(const project::StoreCommit& commit);

} // namespace fos::app
