// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Session: the open project document of the application window plus the derived data the
/// views display. All edits go through project::Document commands; everything shown is
/// regenerated from project::resolve() after each change (nothing is computed here).
#include "fos/app/recovery_store.hpp"
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
    /// Opens a recovery copy from the crash-recovery store: the document becomes the recovered
    /// project, associated with the entry's original path (empty = untitled) and marked
    /// modified, so Save writes back to the original. The recovery copy is kept until the next
    /// save / discard; when its key differs from the document's key (unreadable sidecar), it is
    /// rewritten under the document's key first and the old copy removed.
    core::Result<core::Diagnostics> restoreRecovery(const RecoveryEntry& entry);
    /// Saves to filePath() (error when untitled). Discards the recovery copy on success.
    core::Result<void> save();
    /// Saves to `path`; on success the recovery copies of the previous and of the new key are
    /// deleted (as is a legacy `<path>.autosave`).
    core::Result<void> saveAs(const std::filesystem::path& path);
    /// Writes the project to the recovery store (RecoveryStore::write under recoveryKey());
    /// the user's file is never touched. Records the revision written.
    core::Result<void> autosave();
    /// Key of the current document in the recovery store: app::recoveryKey(filePath()) or
    /// untitledRecoveryKey(project id) while untitled.
    std::string recoveryKey() const;
    /// File autosave() writes: recoveryStore().fileFor(recoveryKey()).
    std::filesystem::path autosaveTarget() const;
    /// Directory of the recovery store (the app: AppDataLocation/recovery). Default when never
    /// set: <system temp>/furnitureos-recovery.
    void setRecoveryDirectory(std::filesystem::path directory);
    RecoveryStore recoveryStore() const;
    /// Deletes the recovery copy of the current document (after save, clean close, discard).
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
    /// Largest x (mm, project frame) of the resolved assemblies' bounding boxes, 0 for an empty
    /// project: where a new item is placed (display layout only).
    double rightEdge() const;
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

    std::unique_ptr<project::Document> document_;
    std::filesystem::path path_;
    std::filesystem::path recoveryDir_;
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
