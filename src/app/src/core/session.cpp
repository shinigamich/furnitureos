// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/session.hpp"

#include "fos/app/autosave.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QUuid>

#include <algorithm>
#include <limits>

namespace fos::app {

namespace fs = std::filesystem;

namespace {

project::Metadata freshMetadata() {
    project::Metadata m;
    m.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    m.name = QCoreApplication::translate("fos::app::Session", "Untitled project").toStdString();
    m.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
    m.modifiedAt = m.createdAt;
    return m;
}

core::Error notOpen() {
    return core::makeError(core::ErrorCode::InvalidArgument,
                           "The project has not been saved yet; choose a file name with Save As.");
}

} // namespace

Session::Session(QObject* parent) : QObject(parent) {
    document_ = std::make_unique<project::Document>(project::newProject(freshMetadata()));
    attachObserver();
    refresh();
}

Session::~Session() = default;

void Session::attachObserver() {
    document_->addObserver([this](const project::ChangeNotice&) {
        ++revision_;
        refresh();
        pruneSelection();
        emit projectChanged();
        emit savedStateChanged();
    });
}

void Session::refresh() {
    production_.reset();
    cutting_.reset();
    validation_.reset();
    auto r = document_->resolve();
    if (r) {
        resolved_ = std::move(r).value();
        resolveError_.clear();
    } else {
        resolved_.reset();
        resolveError_ = r.error().message;
    }
}

void Session::replaceDocument(project::Project project, fs::path path) {
    document_ = std::make_unique<project::Document>(std::move(project));
    attachObserver();
    path_ = std::move(path);
    forcedDirty_ = false;
    ++revision_;
    autosavedRevision_.reset();
    selection_ = {};
    refresh();
    emit documentReplaced();
    emit projectChanged();
    emit selectionChanged();
    emit savedStateChanged();
}

void Session::newProject() { replaceDocument(project::newProject(freshMetadata()), {}); }

core::Result<void> Session::newFromTemplate(TemplateKind kind, const std::optional<TemplateSize>& size) {
    project::Project p = project::newProject(freshMetadata());
    auto item = makeTemplateItem(kind, nextItemId(p), "", p.libraries, size);
    if (!item) return item.error();
    p.items.push_back(std::move(item).value());
    const std::string id = p.items.back().id();
    replaceDocument(std::move(p), {});
    selectItem(id);
    return {};
}

double Session::rightEdge() const {
    double edge = 0;
    bool any = false;
    if (resolved_)
        for (const auto& a : resolved_->model.assemblies) {
            const auto box = a.bounds().transformed(a.placement);
            if (box.empty()) continue;
            edge = any ? std::max(edge, box.max.x) : box.max.x;
            any = true;
        }
    return edge;
}

core::Result<std::string> Session::addFromTemplate(TemplateKind kind, const std::optional<TemplateSize>& size) {
    const auto& p = project();
    const std::string id = nextItemId(p);
    auto item = makeTemplateItem(kind, id, uniqueItemName(p, templateTitle(kind).toStdString()), p.libraries, size);
    if (!item) return item.error();
    item.value().cabinet()->placement = geometry::Transform::translation({rightEdge(), 0, 0});
    if (auto r = execute(project::commands::addItem(std::move(item).value())); !r) return r.error();
    selectItem(id);
    return id;
}

core::Result<core::Diagnostics> Session::open(const fs::path& path, const formats::LoadOptions& options) {
    auto loaded = project::loadProject(path, options);
    if (!loaded) return loaded.error();
    core::Diagnostics diagnostics = loaded.value().diagnostics;
    replaceDocument(std::move(loaded.value().project), path);
    return diagnostics;
}

core::Result<core::Diagnostics> Session::openRecovered(const fs::path& path) {
    auto recovered = project::recoverProject(path);
    if (!recovered) return recovered.error();
    core::Diagnostics diagnostics = recovered.value().diagnostics;
    auto onDisk = project::loadProject(path);
    if (onDisk) {
        // The recovered state replaces the file's state as one undoable step.
        replaceDocument(std::move(onDisk.value().project), path);
        project::Project state = std::move(recovered.value().project);
        project::Command cmd;
        cmd.label = {"Recover {0} from autosave or backup", {path.filename().string()}};
        cmd.apply = [state](project::Project& p) -> core::Result<project::Affected> {
            p = state;
            project::Affected a;
            for (const auto& item : p.items) a.items.push_back(item.id());
            a.libraries = true;
            a.settings = true;
            return a;
        };
        if (auto r = document_->execute(cmd); !r) return r.error();
    } else {
        replaceDocument(std::move(recovered.value().project), path);
        forcedDirty_ = true;
        emit savedStateChanged();
    }
    return diagnostics;
}

core::Result<void> Session::save() {
    if (path_.empty()) return notOpen();
    return saveAs(path_);
}

core::Result<void> Session::saveAs(const fs::path& path) {
    const fs::path oldAutosave = autosaveTarget();
    auto r = document_->save(path);
    if (!r) return r.error();
    const bool wasUntitled = path_.empty();
    path_ = path;
    forcedDirty_ = false;
    (void)formats::discardAutosave(path);
    if (wasUntitled && !untitledDir_.empty()) (void)formats::discardAutosave(oldAutosave);
    emit savedStateChanged();
    return {};
}

fs::path Session::autosaveTarget() const {
    if (!path_.empty()) return path_;
    return untitledAutosaveBase(untitledDir_.empty() ? fs::temp_directory_path() : untitledDir_, project().metadata.id);
}

void Session::setUntitledAutosaveDirectory(fs::path directory) { untitledDir_ = std::move(directory); }

core::Result<void> Session::autosave() {
    const fs::path target = autosaveTarget();
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    auto r = document_->autosave(target);
    if (!r) return r.error();
    autosavedRevision_ = revision_;
    return {};
}

void Session::discardAutosave() { (void)formats::discardAutosave(autosaveTarget()); }

core::Result<void> Session::execute(const project::Command& command, project::Merge merge) {
    return document_->execute(command, merge);
}

core::Result<void> Session::undo() { return document_->undo(); }
core::Result<void> Session::redo() { return document_->redo(); }

core::Result<void> Session::deleteItem(const std::string& itemId) {
    return execute(project::commands::removeItem(itemId));
}

core::Result<std::string> Session::duplicateItem(const std::string& itemId) {
    const project::Item* item = project().findItem(itemId);
    if (!item) return core::makeError(core::ErrorCode::NotFound, "Item '" + itemId + "' does not exist.", itemId);
    const std::string id = nextItemId(project());
    const std::string name = uniqueItemName(project(), item->name());
    // The copy keeps the original's orientation and is moved to the right of every item.
    geometry::Transform placement = item->placement();
    double shift = 0;
    if (resolved_)
        if (const auto* a = resolved_->model.findAssembly(domain::AssemblyId(itemId))) {
            const auto box = a->bounds().transformed(a->placement);
            if (!box.empty()) shift = rightEdge() - box.min.x;
        }
    placement = geometry::Transform::translation({shift, 0, 0}) * placement;
    if (auto r = execute(project::commands::duplicateItem(itemId, id, name, placement)); !r) return r.error();
    selectItem(id);
    return id;
}

bool Session::isDirty() const noexcept { return forcedDirty_ || document_->isDirty(); }

const project::ProductionReports& Session::productionReports() {
    if (!production_)
        production_ = resolved_ ? project::productionReports(*resolved_, project::productionOptions(project()))
                                : project::ProductionReports{};
    return *production_;
}

const project::CuttingPlans& Session::cuttingPlans() {
    if (!cutting_) cutting_ = resolved_ ? project::cuttingPlans(*resolved_, project().cutting) : project::CuttingPlans{};
    return *cutting_;
}

const validation::ValidationReport& Session::validationReport() {
    if (!validation_) validation_ = resolved_ ? validation::validateModel(resolved_->model) : validation::ValidationReport{};
    return *validation_;
}

std::string Session::itemOfPart(const std::string& partId) const {
    if (!resolved_) return {};
    for (const auto& a : resolved_->model.assemblies)
        if (a.findPart(domain::PartId(partId))) return a.id.str();
    return {};
}

const domain::Part* Session::findPart(const std::string& partId) const {
    if (!resolved_) return nullptr;
    for (const auto& a : resolved_->model.assemblies)
        if (const auto* p = a.findPart(domain::PartId(partId))) return p;
    return nullptr;
}

void Session::setSelection(Selection s) {
    std::vector<std::string> parts;
    for (const auto& id : s.partIds) {
        const std::string owner = itemOfPart(id);
        if (owner.empty()) continue;
        if (s.itemId.empty()) s.itemId = owner;
        if (owner == s.itemId && std::find(parts.begin(), parts.end(), id) == parts.end()) parts.push_back(id);
    }
    s.partIds = std::move(parts);
    if (!s.itemId.empty() && !project().findItem(s.itemId)) s = {};
    if (s == selection_) return;
    selection_ = std::move(s);
    emit selectionChanged();
}

void Session::selectPart(const std::string& partId) {
    if (partId.empty()) return setSelection({});
    setSelection({{}, {partId}});
}

void Session::selectItem(const std::string& itemId) { setSelection({itemId, {}}); }

void Session::pruneSelection() { setSelection(selection_); }

} // namespace fos::app
