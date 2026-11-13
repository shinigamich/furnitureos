// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Session: the open project document of the application window plus the derived data the
/// views display. All edits go through project::Document commands; everything shown is
/// regenerated from project::resolve() after each change (nothing is computed here).
#include "fos/app/templates.hpp"
#include "fos/project/document.hpp"
#include "fos/project/file.hpp"
#include "fos/project/outputs.hpp"
#include "fos/validation/validation.hpp"

#include <QObject>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fos::app {

/// What is selected: an item (cabinet or panel) and optionally parts of its assembly.
struct Selection {
    std::string itemId;                ///< Empty = nothing selected.
    std::vector<std::string> partIds;  ///< Part ids (domain::PartId::str()), may be empty.
    bool operator==(const Selection&) const = default;
};

class Session : public QObject {
    Q_OBJECT
public:
    explicit Session(QObject* parent = nullptr);
    ~Session() override;

    // --- lifecycle -------------------------------------------------------------------
    /// Empty new project (generic default libraries), untitled, clean.
    void newProject();
    /// New untitled project containing one cabinet from the template (clean: it is the
    /// starting point, nothing to save yet). Error = the furniture module's message.
    core::Result<void> newFromTemplate(TemplateKind kind, const std::optional<TemplateSize>& size = std::nullopt);
    /// Adds a cabinet from the template to the current project (undoable command), placed
    /// to the right of the existing items. Returns the new item id.
    core::Result<std::string> addFromTemplate(TemplateKind kind, const std::optional<TemplateSize>& size = std::nullopt);
    /// Loads a `.fosp`; on success it becomes the document (clean) and the load diagnostics
    /// are returned. On error the current document is unchanged.
    core::Result<core::Diagnostics> open(const std::filesystem::path& path, const formats::LoadOptions& options = {});
    /// Opens `path` from its newest valid autosave/backup (project::recoverProject). The
    /// document is associated with `path` and marked modified (the recovered state is not
    /// what is on disk); the replacement is one undoable step when `path` itself loads.
    core::Result<core::Diagnostics> openRecovered(const std::filesystem::path& path);
    /// Saves to filePath() (error when untitled). Discards the autosave on success.
    core::Result<void> save();
    core::Result<void> saveAs(const std::filesystem::path& path);
    /// Document::autosave to autosaveTarget(); records the revision written.
    core::Result<void> autosave();
    /// filePath() or, for an untitled document, untitledAutosaveBase(untitledDir, project id).
    std::filesystem::path autosaveTarget() const;
    void setUntitledAutosaveDirectory(std::filesystem::path directory);
    /// Deletes the autosave of the current document (after save, or when the user discards).
    void discardAutosave();

    // --- editing ---------------------------------------------------------------------
    core::Result<void> execute(const project::Command& command, project::Merge merge = project::Merge::Never);
    core::Result<void> undo();
    core::Result<void> redo();
    /// removeItem command; the selection is cleared when it pointed into the item.
    core::Result<void> deleteItem(const std::string& itemId);
    /// duplicateItem command with a fresh id, a unique name and the copy placed right of
    /// all items. Returns the new id.
    core::Result<std::string> duplicateItem(const std::string& itemId);

    // --- state -----------------------------------------------------------------------
    const project::Document& document() const noexcept { return *document_; }
    const project::Project& project() const noexcept { return document_->project(); }
    bool isDirty() const noexcept;
    const std::filesystem::path& filePath() const noexcept { return path_; }
    /// Increases with every change of the project (execute / undo / redo / replace).
    std::uint64_t revision() const noexcept { return revision_; }
    std::optional<std::uint64_t> lastAutosavedRevision() const noexcept { return autosavedRevision_; }

    /// Fresh resolve() of the current project; nullptr when resolve() failed (resolveError()).
    const project::Resolved* resolved() const noexcept { return resolved_ ? &*resolved_ : nullptr; }
    const std::string& resolveError() const noexcept { return resolveError_; }
    /// Lazily computed from resolved() (empty when there is no resolved model).
    const project::ProductionReports& productionReports();
    const project::CuttingPlans& cuttingPlans();
    const validation::ValidationReport& validationReport();

    // --- selection -------------------------------------------------------------------
    const Selection& selection() const noexcept { return selection_; }
    /// Normalises (item of the selected parts; unknown ids dropped) and emits
    /// selectionChanged() when it differs.
    void setSelection(Selection selection);
    /// Selects the part (and its item); empty id clears the selection.
    void selectPart(const std::string& partId);
    void selectItem(const std::string& itemId);
    /// Id of the item whose generated assembly contains `partId`, empty when none.
    std::string itemOfPart(const std::string& partId) const;
    const domain::Part* findPart(const std::string& partId) const;

signals:
    /// The whole document was replaced (new / open / recovered).
    void documentReplaced();
    /// The project changed through a command, undo or redo (also after documentReplaced).
    void projectChanged();
    void selectionChanged();
    /// Saved, or the dirty flag / file path changed.
    void savedStateChanged();

private:
    void replaceDocument(project::Project project, std::filesystem::path path);
    void attachObserver();
    void refresh();
    void pruneSelection();
    double rightEdge() const;

    std::unique_ptr<project::Document> document_;
    std::filesystem::path path_;
    std::filesystem::path untitledDir_;
    bool forcedDirty_ = false;
    std::uint64_t revision_ = 0;
    std::optional<std::uint64_t> autosavedRevision_;
    std::optional<project::Resolved> resolved_;
    std::string resolveError_;
    std::optional<project::ProductionReports> production_;
    std::optional<project::CuttingPlans> cutting_;
    std::optional<validation::ValidationReport> validation_;
    Selection selection_;
};

} // namespace fos::app
