// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// PRODUCTION BATCHES (roadmap phase 18): several projects (orders) produced together.
///
/// A batch document (`.fosbatch`, container kind "furnitureos-batch", payload version 1, schema
/// in src/project/README.md "Production batches") lists project files by RELATIVE path (relative
/// to the directory of the batch file) with a quantity multiplier each, plus batch-wide cutting /
/// nesting settings (the project structures), edge banding allowances and an optional, explicit
/// material substitution map.
///
/// loadBatch() loads and resolves every listed project independently: a missing or broken project
/// is an Error diagnostic of that project and never aborts the others. mergeBatch() builds ONE
/// domain::Model of all usable projects:
///   * project key   = BatchEntry::key, or the file stem made file-name safe ([A-Za-z0-9._-]);
///   * instance key  = "<key>" for quantity 1, "<key>#<n>" (n = 1..quantity) otherwise;
///   * every assembly, part and hardware instance id becomes "<instance key>/<original id>"
///     (hardware part references follow), so a quantity K project contributes K copies;
///   * libraries are merged by id; the same id with different data in two projects is an Error
///     (batch.library.conflict, naming both projects) and the later project is left out;
///   * a material substitution "a" -> "b" re-assigns every part made of "a" to "b". It is never
///     silent: each project in which it changed parts gets an Info batch.substitution.applied;
///     a target missing from the merged libraries or of a different thickness is an Error and the
///     substitution is not applied.
/// The batch outputs reuse the project output glue (outputs.hpp) on the merged model.
/// All lengths are mm.
#include "fos/core/result.hpp"
#include "fos/formats/atomic_file.hpp"
#include "fos/formats/container.hpp"
#include "fos/production/labels.hpp"
#include "fos/production/tables.hpp"
#include "fos/project/file.hpp"
#include "fos/project/outputs.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fos::project {

inline constexpr const char* kBatchFormatKind = "furnitureos-batch";
inline constexpr int kBatchFormatVersion = 1;
inline constexpr const char* kBatchFileExtension = ".fosbatch";
/// Limits of a batch document (checked on decode and by checkBatch).
inline constexpr std::size_t kMaxBatchProjects = 200;
inline constexpr int kMaxBatchQuantity = 500;      ///< Per entry.
inline constexpr int kMaxBatchTotalQuantity = 1000; ///< Sum of all entry quantities.

/// One project (order) of a batch.
struct BatchEntry {
    std::string path;  ///< UTF-8, '/'-separated, relative to the batch file directory (absolute accepted).
    int quantity = 1;  ///< Copies produced, 1 ..= kMaxBatchQuantity.
    std::string key;   ///< Optional explicit project key ([A-Za-z0-9._-]+); empty = derived (batchKey()).
    std::string note;  ///< Free text (e.g. customer order number).
};

struct Batch {
    std::string name;
    std::string date;  ///< Free text, by convention ISO-8601 "YYYY-MM-DD"; supplied by the caller.
    std::string notes;
    std::vector<BatchEntry> entries;                 ///< Batch order (= order of all outputs).
    ProjectCuttingSettings cutting;                  ///< Batch-wide cutting + nesting settings.
    production::EdgeBandingOptions edgeBanding;      ///< Allowances of the edge banding figures.
    bool cutListGroupAcrossAssemblies = true;        ///< production::CutListOptions::groupAcrossAssemblies.
    std::map<std::string, std::string> materialSubstitutions; ///< material id -> stock material id.
    formats::Json extra = formats::Json::object();   ///< Unknown payload keys (preserved).
};

/// Effective project key of `entry`: entry.key, else the file stem of entry.path with every
/// character outside [A-Za-z0-9._-] replaced by '_' ("_" when empty).
std::string batchKey(const BatchEntry& entry);

/// InvalidArgument naming the entry when: no/empty path, quantity outside 1 ..= kMaxBatchQuantity,
/// total quantity > kMaxBatchTotalQuantity, more than kMaxBatchProjects entries, an explicit key
/// with characters outside [A-Za-z0-9._-], two entries with the same effective key (message says
/// to set "key"), an empty substitution id or a substitution chain ("a"->"b" and "b"->"c").
core::Result<void> checkBatch(const Batch& batch);

formats::Json batchToJson(const Batch& batch);
/// Strict decoding (closed objects except the payload root) + checkBatch. ParseError messages
/// start with a JSON-pointer path ("/payload/projects/1/quantity: ...").
core::Result<Batch> batchFromJson(const formats::Json& payload, std::string_view path = "");

formats::FormatSpec batchFormatSpec();
core::Result<std::string> serializeBatch(const Batch& batch, const std::string& savedAt);
/// Container load + batchFromJson; does NOT load the projects (see loadBatch).
core::Result<Batch> parseBatchDocument(std::string_view text, const formats::LoadOptions& options = {},
                                       std::string_view sourceName = "batch");
core::Result<Batch> loadBatchDocument(const std::filesystem::path& path, const formats::LoadOptions& options = {});
/// Atomic save (formats::saveDocument with `options`, incl. its rotating backups). The batch is encoded and decoded again first;
/// a batch that would not load back is refused and nothing is written.
core::Result<formats::SaveReport> saveBatch(const std::filesystem::path& path, const Batch& batch,
                                            formats::SaveOptions options = {});

/// One listed project after loading.
struct BatchProject {
    BatchEntry entry;
    std::string key;                     ///< batchKey(entry).
    std::filesystem::path file;          ///< Resolved path of the project file.
    std::optional<LoadedProject> loaded; ///< Empty when the file could not be loaded.
    /// batch.project.load_failed (Error, message has the file and the reason) or, for a loaded
    /// project, its own load/resolve diagnostics (entityId prefixed with "<key>/").
    core::Diagnostics diagnostics;
    std::string name() const; ///< Project name, or the key when it has none / was not loaded.
};

struct LoadedBatch {
    Batch batch;
    std::filesystem::path baseDir;     ///< Directory the entry paths are relative to.
    std::vector<BatchProject> projects; ///< One per entry, batch order.
};

/// Loads + resolves each entry's project (relative to `baseDir`). Never fails as a whole.
LoadedBatch loadBatchProjects(const Batch& batch, const std::filesystem::path& baseDir,
                              const formats::LoadOptions& options = {});
/// loadBatchDocument(path) + loadBatchProjects(batch, path.parent_path()). Fails only when the
/// batch document itself cannot be loaded.
core::Result<LoadedBatch> loadBatch(const std::filesystem::path& path, const formats::LoadOptions& options = {});

/// One project inside the merged model.
struct BatchProjectModel {
    std::string key;
    std::string name;
    std::string note;
    int quantity = 1;
    bool included = false;                 ///< False: not loaded or left out (library conflict).
    std::vector<std::string> instanceKeys; ///< "<key>" or "<key>#1" .. "<key>#K".
    /// The project's copies only (namespaced, substitutions applied) with the merged libraries.
    domain::Model model;
};

struct BatchModel {
    domain::Model model;                     ///< All included projects, batch order.
    std::vector<BatchProjectModel> projects; ///< One per entry, batch order.
    /// Every project's diagnostics (BatchProject::diagnostics), then batch.library.conflict
    /// (Error), batch.substitution.unknown_target / batch.substitution.thickness_mismatch (Error),
    /// batch.substitution.applied (Info), batch.empty (Warning: no usable project).
    core::Diagnostics diagnostics;

    /// Project of a namespaced id ("<instance key>/..."), or nullptr.
    const BatchProjectModel* projectOf(const std::string& namespacedId) const;
};

BatchModel mergeBatch(const LoadedBatch& loaded);

/// Production options of the batch (edge banding allowances, cut-list grouping).
ProductionOptions batchProductionOptions(const Batch& batch);
/// Combined cut list of the merged model (production::cutList).
production::CutList batchCutList(const BatchModel& model, const Batch& batch);
/// Guillotine + nesting plans of the merged model with the batch cutting settings (cuttingPlans).
CuttingPlans batchCuttingPlans(const BatchModel& model, const Batch& batch, const cutting::Options& options = {});
/// production::labels of the merged model; each label's projectName is
/// "<batch name> | <project name> | order <instance key>" (the order part names the copy).
production::LabelSet batchLabels(const BatchModel& model, const Batch& batch);

/// Batch summary figures of one project (all copies) or of the whole batch.
struct BatchSummaryRow {
    std::string key;   ///< "TOTAL" for the total row.
    std::string name;
    int quantity = 0;  ///< Copies (total row: sum).
    bool included = false;
    int partCount = 0;
    /// Sum over materials of production::MaterialRow::sheetsLowerBoundByArea. The total row is
    /// computed on the merged model, so it can be LOWER than the sum of the project rows (parts
    /// of several orders share sheets).
    int sheetsLowerBound = 0;
    double edgeBandingAppliedM = 0;  ///< production::EdgeBandingRow::appliedLengthM summed.
    double edgeBandingPurchaseM = 0; ///< appliedLengthM + allowances (batch edgeBanding options).
    int hardwareQuantity = 0;        ///< production::HardwareRow::quantity summed.
};

struct BatchSummary {
    std::vector<BatchSummaryRow> projects; ///< Batch order.
    BatchSummaryRow total;
    production::HardwareReport hardware;   ///< Hardware totals of the merged model.
    core::Diagnostics diagnostics;         ///< Report diagnostics of the merged model.
};

BatchSummary batchSummary(const BatchModel& model, const Batch& batch);
/// Columns: Key, Project, Qty, Included, Parts, Sheets (lower bound), Edge banding applied (m),
/// Edge banding purchase (m), Hardware; one row per project, then the total row.
production::Table batchSummaryTable(const BatchSummary& summary);

} // namespace fos::project
