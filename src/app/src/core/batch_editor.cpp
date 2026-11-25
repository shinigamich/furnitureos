// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/batch_editor.hpp"

#include "fos/app/production_text.hpp"
#include "fos/cutting/svg.hpp"
#include "fos/nesting/svg.hpp"
#include "fos/production/csv.hpp"

#include <algorithm>
#include <utility>

namespace fos::app {

namespace fs = std::filesystem;

namespace {

fs::path fromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string toUtf8(const fs::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

fs::path absoluteNormal(const fs::path& p) {
    std::error_code ec;
    fs::path a = fs::absolute(p, ec);
    return (ec ? p : a).lexically_normal();
}

core::Error badIndex(std::size_t index, std::size_t size) {
    return core::makeError(core::ErrorCode::NotFound, "Batch entry " + std::to_string(index + 1) +
                                                          " does not exist; the batch has " + std::to_string(size) +
                                                          " project(s).");
}

/// Characters outside [A-Za-z0-9._-] become '_' (file names of exported sheets).
std::string safeFileName(const std::string& s) {
    std::string out;
    for (char c : s) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
                        c == '_' || c == '-';
        out += ok ? c : '_';
    }
    return out.empty() ? std::string("_") : out;
}

core::Result<void> ensureDirectory(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec || !fs::is_directory(dir, ec))
        return core::makeError(core::ErrorCode::IoError,
                               "Cannot create the folder '" + toUtf8(dir) + "': " + (ec ? ec.message() : "not a folder") + ".");
    return {};
}

core::Result<std::vector<fs::path>> writeAll(const std::vector<std::pair<fs::path, std::string>>& files) {
    std::vector<fs::path> written;
    for (const auto& [path, content] : files) {
        if (auto r = production::writeTextFile(path, content); !r) return r.error();
        written.push_back(path);
    }
    return written;
}

} // namespace

std::string batchEntryPath(const fs::path& batchFile, const fs::path& projectFile) {
    const fs::path abs = absoluteNormal(projectFile);
    if (batchFile.empty()) return toUtf8(abs);
    const fs::path base = absoluteNormal(batchFile).parent_path();
    fs::path rel = abs.lexically_relative(base);
    if (rel.empty()) rel = abs; // e.g. another drive on Windows
    return toUtf8(rel);
}

std::vector<BatchDiagnosticRow> batchDiagnosticRows(const BatchEvaluation& evaluation) {
    std::vector<BatchDiagnosticRow> rows;
    auto keyOf = [&](const std::string& entity) {
        std::string best;
        for (const auto& p : evaluation.model.projects) {
            const std::string& k = p.key;
            const bool match = entity == k ||
                               (entity.size() > k.size() && entity.compare(0, k.size(), k) == 0 &&
                                (entity[k.size()] == '/' || entity[k.size()] == '#'));
            if (match && k.size() > best.size()) best = k;
        }
        return best;
    };
    auto add = [&](const core::Diagnostics& ds) {
        for (const auto& d : ds) rows.push_back({d.severity, d.code, d.message, d.entityId, keyOf(d.entityId)});
    };
    add(evaluation.model.diagnostics);
    add(evaluation.summary.diagnostics);
    return rows;
}

std::vector<BatchMaterialChoice> batchMaterialChoices(const BatchEvaluation& evaluation) {
    std::vector<BatchMaterialChoice> out;
    for (const auto& m : evaluation.model.model.materials) out.push_back({m.id.str(), m.name, m.thickness});
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return out;
}

// ------------------------------------------------------------------- BatchEditor

BatchEditor::BatchEditor() { batch_.name = "Batch"; }

fs::path BatchEditor::baseDir() const {
    if (!path_.empty()) return absoluteNormal(path_).parent_path();
    std::error_code ec;
    fs::path cwd = fs::current_path(ec);
    return ec ? fs::path(".") : cwd;
}

void BatchEditor::newBatch(std::string name, std::string date) {
    batch_ = project::Batch{};
    batch_.name = std::move(name);
    batch_.date = std::move(date);
    path_.clear();
    modified_ = false;
}

core::Result<void> BatchEditor::open(const fs::path& path) {
    auto b = project::loadBatchDocument(path);
    if (!b) return b.error();
    batch_ = std::move(b.value());
    path_ = path;
    modified_ = false;
    return {};
}

core::Result<void> BatchEditor::save() {
    if (path_.empty())
        return core::makeError(core::ErrorCode::InvalidArgument, "The batch has no file yet; use Save As.");
    if (auto r = project::saveBatch(path_, batch_); !r) return r.error();
    modified_ = false;
    return {};
}

core::Result<void> BatchEditor::saveAs(fs::path path) {
    if (path.extension() != project::kBatchFileExtension) path += project::kBatchFileExtension;
    project::Batch rebased = batch_;
    const fs::path oldBase = baseDir();
    for (auto& e : rebased.entries) {
        fs::path p = fromUtf8(e.path);
        if (p.is_relative()) p = oldBase / p;
        e.path = batchEntryPath(path, p);
    }
    if (auto r = project::saveBatch(path, rebased); !r) return r.error();
    batch_ = std::move(rebased);
    path_ = std::move(path);
    modified_ = false;
    return {};
}

core::Result<void> BatchEditor::commit(project::Batch candidate) {
    if (auto r = project::checkBatch(candidate); !r) return r.error();
    batch_ = std::move(candidate);
    modified_ = true;
    return {};
}

core::Result<void> BatchEditor::setInfo(std::string name, std::string date, std::string notes) {
    project::Batch b = batch_;
    b.name = std::move(name);
    b.date = std::move(date);
    b.notes = std::move(notes);
    return commit(std::move(b));
}

core::Result<std::size_t> BatchEditor::addProject(const fs::path& projectFile, int quantity) {
    if (quantity < 1 || quantity > project::kMaxBatchQuantity)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Quantity must be from 1 to " + std::to_string(project::kMaxBatchQuantity) + ", got " +
                                   std::to_string(quantity) + ".");
    auto loaded = project::loadProject(projectFile);
    if (!loaded)
        return core::makeError(loaded.error().code, "Cannot add project '" + toUtf8(projectFile) +
                                                        "': " + loaded.error().message);
    const std::string stored = batchEntryPath(path_, projectFile);
    project::Batch b = batch_;
    std::size_t index = b.entries.size();
    for (std::size_t i = 0; i < b.entries.size(); ++i)
        if (b.entries[i].path == stored) index = i;
    if (index < b.entries.size())
        b.entries[index].quantity += quantity;
    else
        b.entries.push_back({stored, quantity, {}, {}});
    if (auto r = commit(std::move(b)); !r) return r.error();
    return index;
}

core::Result<void> BatchEditor::removeEntry(std::size_t index) {
    if (index >= batch_.entries.size()) return badIndex(index, batch_.entries.size());
    project::Batch b = batch_;
    b.entries.erase(b.entries.begin() + static_cast<std::ptrdiff_t>(index));
    return commit(std::move(b));
}

core::Result<void> BatchEditor::setQuantity(std::size_t index, int quantity) {
    if (index >= batch_.entries.size()) return badIndex(index, batch_.entries.size());
    project::Batch b = batch_;
    b.entries[index].quantity = quantity;
    return commit(std::move(b));
}

core::Result<void> BatchEditor::setKey(std::size_t index, std::string key) {
    if (index >= batch_.entries.size()) return badIndex(index, batch_.entries.size());
    project::Batch b = batch_;
    b.entries[index].key = std::move(key);
    return commit(std::move(b));
}

core::Result<void> BatchEditor::setNote(std::size_t index, std::string note) {
    if (index >= batch_.entries.size()) return badIndex(index, batch_.entries.size());
    project::Batch b = batch_;
    b.entries[index].note = std::move(note);
    return commit(std::move(b));
}

core::Result<void> BatchEditor::setMaterialSubstitutions(std::map<std::string, std::string> substitutions) {
    project::Batch b = batch_;
    b.materialSubstitutions = std::move(substitutions);
    return commit(std::move(b));
}

core::Result<void> BatchEditor::setCutListGroupAcrossAssemblies(bool group) {
    project::Batch b = batch_;
    b.cutListGroupAcrossAssemblies = group;
    return commit(std::move(b));
}

project::Project BatchEditor::settingsProject() const {
    project::Project p;
    p.cutting = batch_.cutting;
    return p;
}

core::Result<void> BatchEditor::executeSettingsCommand(const project::Command& command) {
    project::Project p = settingsProject();
    auto r = command.apply(p);
    if (!r) return r.error();
    project::Batch b = batch_;
    b.cutting = std::move(p.cutting);
    return commit(std::move(b));
}

BatchEvaluation BatchEditor::evaluate() const {
    BatchEvaluation e;
    e.loaded = project::loadBatchProjects(batch_, baseDir());
    e.model = project::mergeBatch(e.loaded);
    e.summary = project::batchSummary(e.model, batch_);
    e.incomplete = core::hasErrors(e.model.diagnostics);
    return e;
}

// ------------------------------------------------------------------- exports

core::Result<std::vector<fs::path>> exportBatchCutListCsv(const BatchEvaluation& evaluation, const fs::path& file) {
    const auto list = project::batchCutList(evaluation.model, evaluation.loaded.batch);
    production::CsvOptions options;
    options.utf8Bom = true;
    const auto table = production::cutListTable(list, ProductionText::localisation());
    return writeAll({{file, production::toCsv(table, options)}});
}

core::Result<std::vector<fs::path>> exportBatchLabels(const BatchEvaluation& evaluation, const fs::path& dir,
                                                      const LabelBarcodeSettings& barcode, core::Diagnostics* warnings) {
    const auto set = project::batchLabels(evaluation.model, evaluation.loaded.batch);
    if (set.labels.empty())
        return core::makeError(core::ErrorCode::NotFound, "The batch has no parts to label.");
    auto sheets = production::renderLabelSheets(set.labels, labelSheetOptions(barcode));
    if (!sheets) return sheets.error();
    if (warnings) *warnings = sheets.value().diagnostics;
    if (auto r = ensureDirectory(dir); !r) return r.error();
    std::vector<std::pair<fs::path, std::string>> files;
    for (std::size_t i = 0; i < sheets.value().pages.size(); ++i)
        files.emplace_back(dir / ("labels-" + std::to_string(i + 1) + ".svg"), sheets.value().pages[i]);
    return writeAll(files);
}

core::Result<std::vector<fs::path>> exportBatchCuttingPlans(const BatchEvaluation& evaluation, const fs::path& dir) {
    const auto plans = project::batchCuttingPlans(evaluation.model, evaluation.loaded.batch);
    std::vector<std::pair<fs::path, std::string>> files;
    for (const auto& material : plans.materials) {
        const std::string stem = safeFileName(material.materialId);
        if (material.guillotine) {
            const auto& plan = plans.plans[*material.guillotine];
            if (plan.result)
                for (std::size_t i = 0; i < plan.result->sheets.size(); ++i)
                    files.emplace_back(dir / (stem + "-sheet-" + std::to_string(i + 1) + ".svg"),
                                       cutting::renderSheetSvg(plan.job, plan.result->sheets[i]));
        }
        if (material.nesting) {
            const auto& plan = plans.nestingPlans[*material.nesting];
            if (plan.result)
                for (std::size_t i = 0; i < plan.result->sheets.size(); ++i)
                    files.emplace_back(dir / (stem + "-nest-" + std::to_string(i + 1) + ".svg"),
                                       nesting::renderNestSvg(plan.job.job, plan.result->sheets[i]));
        }
    }
    if (files.empty())
        return core::makeError(core::ErrorCode::NotFound, "The batch cutting plans have no sheet to export.");
    if (auto r = ensureDirectory(dir); !r) return r.error();
    return writeAll(files);
}

} // namespace fos::app
