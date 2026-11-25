// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Production batch window view-model (widget-free). Every figure comes from the project
/// module's batch engine (fos/project/batch.hpp: loadBatchProjects, mergeBatch, batchSummary,
/// batchCutList, batchCuttingPlans, batchLabels); nothing is computed here. Entry paths are
/// stored like `fos batch add`: relative to the directory of the batch file ('/'-separated),
/// absolute while the batch is untitled (they are made relative on the first Save As).
#include "fos/app/label_export.hpp"
#include "fos/project/batch.hpp"
#include "fos/project/commands.hpp"

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fos::app {

/// `projectFile` as stored in a batch whose file is `batchFile`: relative to the batch file's
/// directory (generic '/' form); absolute when `batchFile` is empty (untitled batch) or no
/// relative path exists (another drive).
std::string batchEntryPath(const std::filesystem::path& batchFile, const std::filesystem::path& projectFile);

/// Everything shown by the Summary tab, recomputed by BatchEditor::evaluate().
struct BatchEvaluation {
    project::LoadedBatch loaded;
    project::BatchModel model;
    project::BatchSummary summary;
    /// core::hasErrors(model.diagnostics): projects are missing ("LEFT OUT") or a substitution
    /// was refused, so the outputs are incomplete.
    bool incomplete = false;
};

/// One line of the Summary tab's diagnostics list: merge diagnostics, then report diagnostics.
struct BatchDiagnosticRow {
    core::Severity severity = core::Severity::Error;
    std::string code;
    std::string message;  ///< English text from the core.
    std::string entityId;
    std::string projectKey; ///< Batch project the finding belongs to ("" = batch-level).
};
std::vector<BatchDiagnosticRow> batchDiagnosticRows(const BatchEvaluation& evaluation);

/// Materials of the merged batch libraries ("id", name, thickness mm), id order: the
/// choices of the material substitution table.
struct BatchMaterialChoice {
    std::string id;
    std::string name;
    double thickness = 0;
};
std::vector<BatchMaterialChoice> batchMaterialChoices(const BatchEvaluation& evaluation);

/// Batch document being edited: the batch, its file and the modified flag. Every edit is
/// checked with project::checkBatch and refused (InvalidArgument / OutOfRange, batch
/// unchanged) when it would produce a batch that cannot be saved.
class BatchEditor {
public:
    BatchEditor();

    const project::Batch& batch() const noexcept { return batch_; }
    const std::filesystem::path& filePath() const noexcept { return path_; }
    bool isModified() const noexcept { return modified_; }
    /// Directory the entry paths are relative to (current directory while untitled; untitled
    /// entries are absolute anyway).
    std::filesystem::path baseDir() const;

    /// Untitled empty batch named `name`.
    void newBatch(std::string name, std::string date = {});
    /// project::loadBatchDocument; on error nothing changes.
    core::Result<void> open(const std::filesystem::path& path);
    /// Error (InvalidArgument) when untitled.
    core::Result<void> save();
    /// Saves to `path` (".fosbatch" appended when missing): relative entry paths are rewritten
    /// to stay valid from the new directory, untitled (absolute) ones become relative.
    core::Result<void> saveAs(std::filesystem::path path);

    core::Result<void> setInfo(std::string name, std::string date, std::string notes);
    /// Adds `projectFile` (`quantity` copies). A project that is already listed (same stored
    /// path) gets `quantity` added instead, as `fos batch add`. The project file must load
    /// (project::loadProject; its error is returned). Returns the entry index.
    core::Result<std::size_t> addProject(const std::filesystem::path& projectFile, int quantity = 1);
    core::Result<void> removeEntry(std::size_t index);
    core::Result<void> setQuantity(std::size_t index, int quantity);
    core::Result<void> setKey(std::size_t index, std::string key);
    core::Result<void> setNote(std::size_t index, std::string note);
    /// Replaces the whole substitution map (empty map = none).
    core::Result<void> setMaterialSubstitutions(std::map<std::string, std::string> substitutions);
    core::Result<void> setCutListGroupAcrossAssemblies(bool group);

    /// Scratch project carrying the batch cutting settings, so the project cutting editors'
    /// view-models (cuttingSettingsInput, nestingSettingsInput) can read them.
    project::Project settingsProject() const;
    /// Applies a cutting / nesting settings command (cuttingSettingsCommand,
    /// nestingSettingsCommand, ...) to the batch cutting settings; the command's own checks
    /// apply and its refusal is returned unchanged.
    core::Result<void> executeSettingsCommand(const project::Command& command);

    /// loadBatchProjects(batch, baseDir()) + mergeBatch + batchSummary.
    BatchEvaluation evaluate() const;

private:
    core::Result<void> commit(project::Batch candidate);

    project::Batch batch_;
    std::filesystem::path path_;
    bool modified_ = false;
};

/// Exports of the merged batch. Each returns the files written (in order).
/// Combined cut list (batchCutList, translated captions, UTF-8 BOM) to `file`.
core::Result<std::vector<std::filesystem::path>> exportBatchCutListCsv(const BatchEvaluation& evaluation,
                                                                      const std::filesystem::path& file);
/// Label sheets (batchLabels, A4 70 x 37 mm, barcode per `barcode`) as "<dir>/labels-<n>.svg".
/// Rendering warnings (e.g. barcode omitted on a too narrow label) are stored in `warnings` when
/// given. Error (NotFound) when the batch has no part to label.
core::Result<std::vector<std::filesystem::path>> exportBatchLabels(const BatchEvaluation& evaluation,
                                                                  const std::filesystem::path& dir,
                                                                  const LabelBarcodeSettings& barcode = {},
                                                                  core::Diagnostics* warnings = nullptr);
/// Cutting plans (batchCuttingPlans, heuristic): "<dir>/<material>-sheet-<n>.svg" per saw sheet
/// and "<dir>/<material>-nest-<n>.svg" per nesting sheet (material id made file-name safe).
/// Error (NotFound) when there is no sheet at all.
core::Result<std::vector<std::filesystem::path>> exportBatchCuttingPlans(const BatchEvaluation& evaluation,
                                                                        const std::filesystem::path& dir);

} // namespace fos::app
