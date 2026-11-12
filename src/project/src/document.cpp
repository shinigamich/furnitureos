// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/project/document.hpp"

#include "fos/project/codec.hpp"
#include "fos/project/file.hpp"

#include <algorithm>

namespace fos::project {
namespace {

bool generates(const Project& p, const std::string& itemId) {
    return p.findItem(itemId) != nullptr && generateItem(p, itemId).ok();
}

} // namespace

Document::Document(Project project) : project_(std::move(project)) {}

core::Result<void> Document::execute(const Command& command, Merge merge) {
    if (!command.apply)
        return core::makeError(core::ErrorCode::InvalidArgument, "Command \"" + command.label.english() + "\" has no action.");
    Project next = project_;
    auto affected = command.apply(next);
    if (!affected) return affected.error();
    if (auto s = validateStructure(next); !s) return s.error();
    for (const auto& id : affected.value().items) {
        if (next.findItem(id) == nullptr) continue; // removed
        const bool before = project_.findItem(id) == nullptr || generates(project_, id);
        if (!before) continue;
        auto gen = generateItem(next, id);
        if (!gen)
            return core::makeError(gen.error().code,
                                   "\"" + command.label.english() + "\" was not applied: item \"" + id +
                                       "\" could not be generated afterwards: " + gen.error().message,
                                   id);
    }
    if (projectToJson(next) == projectToJson(project_)) return {}; // no change, no step

    const bool canMerge = merge == Merge::WithPrevious && !command.mergeKey.empty() && undoCount_ > 0 &&
                          undoCount_ == history_.size() && history_.back().mergeKey == command.mergeKey &&
                          savePoint_ != undoCount_;
    if (canMerge) {
        Record& top = history_.back();
        top.label = command.label;
        top.after = next;
        for (const auto& id : affected.value().items)
            if (std::find(top.affected.items.begin(), top.affected.items.end(), id) == top.affected.items.end())
                top.affected.items.push_back(id);
    } else {
        history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(undoCount_), history_.end());
        if (savePoint_ && *savePoint_ > undoCount_) savePoint_.reset();
        history_.push_back({command.label, command.mergeKey, affected.value(), project_, next});
        ++undoCount_;
        while (history_.size() > undoLimit_) {
            history_.pop_front();
            --undoCount_;
            if (savePoint_) {
                if (*savePoint_ == 0) savePoint_.reset();
                else --*savePoint_;
            }
        }
    }
    project_ = std::move(next);
    notify({ChangeKind::Execute, command.label, affected.value()});
    return {};
}

std::optional<CommandLabel> Document::undoLabel() const {
    if (!canUndo()) return std::nullopt;
    return history_[undoCount_ - 1].label;
}

std::optional<CommandLabel> Document::redoLabel() const {
    if (!canRedo()) return std::nullopt;
    return history_[undoCount_].label;
}

core::Result<void> Document::undo() {
    if (!canUndo()) return core::makeError(core::ErrorCode::NotFound, "There is nothing to undo.");
    const Record& r = history_[--undoCount_];
    project_ = r.before;
    notify({ChangeKind::Undo, r.label, r.affected});
    return {};
}

core::Result<void> Document::redo() {
    if (!canRedo()) return core::makeError(core::ErrorCode::NotFound, "There is nothing to redo.");
    const Record& r = history_[undoCount_++];
    project_ = r.after;
    notify({ChangeKind::Redo, r.label, r.affected});
    return {};
}

void Document::setUndoLimit(std::size_t limit) {
    undoLimit_ = std::max<std::size_t>(limit, 1);
    while (history_.size() > undoLimit_) {
        if (undoCount_ == 0) { // drop redo steps from the far end first
            history_.pop_back();
            if (savePoint_ && *savePoint_ > history_.size()) savePoint_.reset();
            continue;
        }
        history_.pop_front();
        --undoCount_;
        if (savePoint_) {
            if (*savePoint_ == 0) savePoint_.reset();
            else --*savePoint_;
        }
    }
}

void Document::clearHistory() {
    const bool clean = !isDirty();
    history_.clear();
    undoCount_ = 0;
    savePoint_ = clean ? std::optional<std::size_t>(0) : std::nullopt;
}

bool Document::isDirty() const noexcept { return savePoint_ != undoCount_; }

void Document::markSaved() noexcept { savePoint_ = undoCount_; }

core::Result<formats::SaveReport> Document::save(const std::filesystem::path& path, const formats::SaveOptions& options) {
    auto r = saveProject(path, project_, options);
    if (r) markSaved();
    return r;
}

core::Result<formats::SaveReport> Document::autosave(const std::filesystem::path& path,
                                                     const formats::SaveOptions& options) const {
    return writeProjectAutosave(path, project_, options);
}

std::size_t Document::addObserver(Observer observer) {
    observers_.emplace_back(nextObserverId_, std::move(observer));
    return nextObserverId_++;
}

void Document::removeObserver(std::size_t id) {
    observers_.erase(std::remove_if(observers_.begin(), observers_.end(), [id](const auto& o) { return o.first == id; }),
                     observers_.end());
}

void Document::notify(const ChangeNotice& notice) const {
    const auto copy = observers_; // an observer may remove itself
    for (const auto& [id, observer] : copy)
        if (observer) observer(notice);
}

} // namespace fos::project
