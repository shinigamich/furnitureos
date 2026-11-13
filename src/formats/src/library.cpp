// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/formats/library.hpp"

#include "codec_reader.hpp"
#include "fos/formats/codec.hpp"

#include <set>

namespace fos::formats {
using namespace detail;

const FormatSpec& libraryFormatSpec() {
    static const FormatSpec spec{std::string(kLibraryFormatKind), kLibraryFormatVersion, {}};
    return spec;
}

Library libraryOf(const domain::Model& model) {
    Library library;
    library.materials = model.materials;
    library.edgeBands = model.edgeBands;
    library.hardware = model.hardwareCatalog;
    return library;
}

Json libraryToJson(const Library& library) {
    Json j = Json::object();
    Json materials = Json::array();
    for (const auto& material : library.materials) materials.push_back(toJson(material));
    j["materials"] = std::move(materials);
    Json edgeBands = Json::array();
    for (const auto& band : library.edgeBands) edgeBands.push_back(toJson(band));
    j["edgeBands"] = std::move(edgeBands);
    Json hardware = Json::array();
    for (const auto& definition : library.hardware) hardware.push_back(toJson(definition));
    j["hardware"] = std::move(hardware);
    if (library.extra.is_object())
        for (const auto& [key, value] : library.extra.items())
            if (!j.contains(key)) j[key] = value;
    return j;
}

core::Result<Library> libraryFromJson(const Json& payload, std::string_view path) {
    ObjectReader r(payload, path, "a library");
    Library library;
    r.array("materials", library.materials, materialFromJson);
    r.array("edgeBands", library.edgeBands, edgeBandFromJson);
    r.array("hardware", library.hardware, hardwareDefinitionFromJson);
    if (r.ok())
        if (auto duplicate = findDuplicateId(library.materials, r.childPath("materials"))) r.fail(*duplicate);
    if (r.ok())
        if (auto duplicate = findDuplicateId(library.edgeBands, r.childPath("edgeBands"))) r.fail(*duplicate);
    if (r.ok())
        if (auto duplicate = findDuplicateId(library.hardware, r.childPath("hardware"))) r.fail(*duplicate);
    if (!r.ok()) return r.error();
    for (const auto& [key, value] : payload.items())
        if (key != "materials" && key != "edgeBands" && key != "hardware") library.extra[key] = value;
    return library;
}

core::Result<SaveReport> exportLibrary(const std::filesystem::path& path, const Library& library,
                                       const AppInfo& app, const SaveOptions& options) {
    Document document;
    document.format = std::string(kLibraryFormatKind);
    document.formatVersion = kLibraryFormatVersion;
    document.app = app;
    document.payload = libraryToJson(library);
    if (auto check = libraryFromJson(document.payload); !check) {
        core::Error error = check.error();
        error.code = core::ErrorCode::InvalidArgument;
        error.message = "The library cannot be exported to '" + path.string() +
                        "' because it would not load again: " + error.message;
        return error;
    }
    return saveDocument(path, std::move(document), options);
}

core::Result<Library> importLibraryText(std::string_view text, std::string_view sourceName,
                                        const LoadOptions& options, core::Diagnostics* warnings) {
    const std::string name(sourceName);
    if (text.size() > options.maxFileBytes)
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               "Library '" + name + "' is " + std::to_string(text.size()) + " bytes; the limit is " +
                                   std::to_string(options.maxFileBytes) + " bytes. It was not parsed.",
                               name);
    auto loaded = parseDocument(text, libraryFormatSpec(), options, name);
    if (!loaded) return loaded.error();
    auto library = libraryFromJson(loaded.value().document.payload, "/payload");
    if (!library) {
        core::Error error = library.error();
        error.message = "'" + name + "' is not a valid library: " + error.message;
        return error;
    }
    if (warnings != nullptr)
        warnings->insert(warnings->end(), loaded.value().diagnostics.begin(), loaded.value().diagnostics.end());
    return library;
}

core::Result<Library> importLibrary(const std::filesystem::path& path, const LoadOptions& options,
                                    core::Diagnostics* warnings) {
    auto text = readFile(path, ReadLimits{options.maxFileBytes});
    if (!text) return text.error();
    return importLibraryText(text.value(), path.filename().string(), options, warnings);
}

const char* toString(MergeAction action) noexcept {
    switch (action) {
    case MergeAction::Added: return "added";
    case MergeAction::Skipped: return "skipped";
    case MergeAction::Replaced: return "replaced";
    case MergeAction::Renamed: return "renamed";
    }
    return "unknown";
}

const char* toString(LibraryCategory category) noexcept {
    switch (category) {
    case LibraryCategory::Material: return "material";
    case LibraryCategory::EdgeBand: return "edge band";
    case LibraryCategory::Hardware: return "hardware definition";
    }
    return "unknown";
}

std::size_t MergeReport::count(MergeAction action) const noexcept {
    std::size_t n = 0;
    for (const auto& entry : entries)
        if (entry.action == action) ++n;
    return n;
}

namespace {

template <class T> core::Result<void> checkUnique(const std::vector<T>& items, LibraryCategory category,
                                                  const char* which) {
    std::set<std::string> seen;
    for (const auto& item : items)
        if (!seen.insert(item.id.str()).second)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   std::string("Cannot merge libraries: the ") + which + " library contains the " +
                                       toString(category) + " id \"" + item.id.str() + "\" more than once.",
                                   item.id.str());
    return {};
}

template <class T>
core::Result<void> mergeCategory(std::vector<T>& target, const std::vector<T>& incoming, LibraryCategory category,
                                 ConflictPolicy policy, const IdGenerator& newId, MergeReport& report) {
    if (auto r = checkUnique(target, category, "target"); !r) return r;
    if (auto r = checkUnique(incoming, category, "imported"); !r) return r;
    std::set<std::string> used;
    for (const auto& item : target) used.insert(item.id.str());
    for (const auto& item : incoming) used.insert(item.id.str());

    const std::size_t existingCount = target.size(); // conflicts are only with pre-existing entries
    for (const T& item : incoming) {
        MergeEntry entry{category, MergeAction::Added, item.id.str(), item.id.str(), item.name};
        T* existing = nullptr;
        for (std::size_t i = 0; i < existingCount; ++i)
            if (target[i].id == item.id) existing = &target[i];
        if (existing == nullptr) {
            target.push_back(item);
        } else if (policy == ConflictPolicy::Skip) {
            entry.action = MergeAction::Skipped;
        } else if (policy == ConflictPolicy::Replace) {
            *existing = item;
            entry.action = MergeAction::Replaced;
        } else {
            std::string fresh;
            for (int attempt = 0; attempt < 100 && fresh.empty(); ++attempt) {
                std::string candidate = newId ? newId() : core::newUuid();
                if (!candidate.empty() && used.insert(candidate).second) fresh = std::move(candidate);
            }
            if (fresh.empty())
                return core::makeError(core::ErrorCode::Internal,
                                       std::string("Cannot rename the imported ") + toString(category) + " \"" +
                                           item.id.str() + "\": the id generator returned 100 empty or already used ids.",
                                       item.id.str());
            T renamed = item;
            renamed.id.value = fresh;
            target.push_back(std::move(renamed));
            entry.action = MergeAction::Renamed;
            entry.resultId = fresh;
        }
        report.entries.push_back(std::move(entry));
    }
    return {};
}

} // namespace

core::Result<MergeReport> mergeLibrary(Library& target, const Library& incoming, ConflictPolicy policy,
                                       const IdGenerator& newId) {
    Library merged = target;
    MergeReport report;
    if (auto r = mergeCategory(merged.materials, incoming.materials, LibraryCategory::Material, policy, newId, report); !r)
        return r.error();
    if (auto r = mergeCategory(merged.edgeBands, incoming.edgeBands, LibraryCategory::EdgeBand, policy, newId, report); !r)
        return r.error();
    if (auto r = mergeCategory(merged.hardware, incoming.hardware, LibraryCategory::Hardware, policy, newId, report); !r)
        return r.error();
    target = std::move(merged);
    return report;
}

} // namespace fos::formats
