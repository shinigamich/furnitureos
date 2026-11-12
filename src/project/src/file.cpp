// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/project/file.hpp"

#include "fos/project/codec.hpp"

#ifndef FOS_PROJECT_APP_VERSION
#define FOS_PROJECT_APP_VERSION "0.0.0"
#endif

namespace fos::project {

void registerProjectMigrations(formats::MigrationRegistry& /*registry*/) {
    // Version 1 is the first released payload version: no steps yet. A future step is added as
    //   registry.add(1, [](formats::Json& payload) -> core::Result<void> { ...; return {}; },
    //                "1 -> 2: <what changed>");
}

formats::FormatSpec projectFormatSpec() {
    formats::FormatSpec spec;
    spec.kind = kProjectFormatKind;
    spec.currentVersion = kProjectFormatVersion;
    registerProjectMigrations(spec.migrations);
    return spec;
}

formats::Document toDocument(const Project& project, std::string savedAt) {
    formats::Document d;
    d.format = kProjectFormatKind;
    d.formatVersion = kProjectFormatVersion;
    d.app.name = "FurnitureOS";
    d.app.version = FOS_PROJECT_APP_VERSION;
    d.savedAt = std::move(savedAt);
    d.payload = projectToJson(project);
    return d;
}

namespace {

/// Encodes and decodes again so that a file that would not load back is never written.
core::Result<formats::Document> checkedDocument(const Project& project, std::string savedAt) {
    formats::Document d = toDocument(project, std::move(savedAt));
    auto back = projectFromJson(d.payload, "/payload");
    if (!back)
        return core::makeError(back.error().code,
                               "The project cannot be saved because it would not load back: " + back.error().message,
                               back.error().context);
    return d;
}

core::Result<LoadedProject> finish(formats::LoadedDocument loaded) {
    auto project = projectFromJson(loaded.document.payload, "/payload");
    if (!project) return project.error();
    auto resolved = resolve(project.value());
    if (!resolved) return resolved.error();
    LoadedProject out;
    out.project = std::move(project.value());
    out.resolved = std::move(resolved.value());
    out.fileVersion = loaded.fileVersion;
    out.migrations = std::move(loaded.migrations);
    out.diagnostics = std::move(loaded.diagnostics);
    out.diagnostics.insert(out.diagnostics.end(), out.resolved.diagnostics.begin(), out.resolved.diagnostics.end());
    return out;
}

} // namespace

core::Result<std::string> serializeProject(const Project& project, const std::string& savedAt) {
    auto d = checkedDocument(project, savedAt);
    if (!d) return d.error();
    return formats::serializeDocument(d.value());
}

core::Result<LoadedProject> parseProject(std::string_view text, const formats::LoadOptions& options,
                                         std::string_view sourceName) {
    auto loaded = formats::parseDocument(text, projectFormatSpec(), options, sourceName);
    if (!loaded) return loaded.error();
    return finish(std::move(loaded.value()));
}

core::Result<LoadedProject> loadProject(const std::filesystem::path& path, const formats::LoadOptions& options) {
    auto loaded = formats::loadDocument(path, projectFormatSpec(), options);
    if (!loaded) return loaded.error();
    return finish(std::move(loaded.value()));
}

core::Result<formats::SaveReport> saveProject(const std::filesystem::path& path, const Project& project,
                                              formats::SaveOptions options) {
    auto d = checkedDocument(project, {});
    if (!d) return d.error();
    options.backupCount = project.settings.backupCount;
    return formats::saveDocument(path, std::move(d.value()), options);
}

core::Result<formats::SaveReport> writeProjectAutosave(const std::filesystem::path& path, const Project& project,
                                                       const formats::SaveOptions& options) {
    auto d = checkedDocument(project, {});
    if (!d) return d.error();
    return formats::writeAutosave(path, std::move(d.value()), options);
}

core::Result<LoadedProject> recoverProject(const std::filesystem::path& path, const formats::LoadOptions& options) {
    auto rec = formats::loadNewestValid(path, projectFormatSpec(), options);
    if (!rec) return rec.error();
    return finish(std::move(rec.value().loaded));
}

} // namespace fos::project
