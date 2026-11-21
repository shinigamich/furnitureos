// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/formats/component.hpp"

#include "codec_reader.hpp"

#include <cmath>
#include <set>

namespace fos::formats {

using detail::describe;
using detail::joinPath;
using detail::schemaError;

namespace {

constexpr std::string_view kComponentKeys[] = {"id",     "name",        "description", "category", "tags",
                                               "author", "previewSize", "library",     "definition"};

bool isKnownComponentKey(std::string_view key) {
    for (auto k : kComponentKeys)
        if (k == key) return true;
    return false;
}

Json previewToJson(const PreviewSize& p) {
    Json j = Json::object();
    j["width"] = p.width;
    j["height"] = p.height;
    j["depth"] = p.depth;
    return j;
}

core::Result<PreviewSize> previewFromJson(const Json& json, std::string_view path) {
    detail::ObjectReader r(json, path, "a preview size");
    PreviewSize p;
    r.number("width", p.width, detail::NumberRule::NonNegative, true);
    r.number("height", p.height, detail::NumberRule::NonNegative, true);
    r.number("depth", p.depth, detail::NumberRule::NonNegative, true);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return p;
}

core::Result<std::string> optionalText(const Json& object, std::string_view key, std::string_view path) {
    const auto it = object.find(std::string(key));
    if (it == object.end()) return std::string();
    return detail::readString(*it, joinPath(path, key), false);
}

core::Result<ComponentRecord> recordFromJson(const Json& json, std::string_view path) {
    if (!json.is_object()) return schemaError(path, "expected a component object, got " + describe(json));
    ComponentRecord rec;
    auto required = [&](std::string_view key) -> core::Result<std::string> {
        const auto it = json.find(std::string(key));
        if (it == json.end()) return schemaError(joinPath(path, key), "required key is missing");
        return detail::readString(*it, joinPath(path, key), true);
    };
    auto id = required("id");
    if (!id) return id.error();
    rec.meta.id = id.value();
    auto name = required("name");
    if (!name) return name.error();
    rec.meta.name = name.value();
    for (auto [key, out] : {std::pair<std::string_view, std::string*>{"description", &rec.meta.description},
                            {"category", &rec.meta.category},
                            {"author", &rec.meta.author}}) {
        auto text = optionalText(json, key, path);
        if (!text) return text.error();
        *out = text.value();
    }
    if (const auto it = json.find("tags"); it != json.end()) {
        const std::string base = joinPath(path, "tags");
        if (!it->is_array()) return schemaError(base, "expected an array of strings, got " + describe(*it));
        std::set<std::string> seen;
        for (std::size_t i = 0; i < it->size(); ++i) {
            auto tag = detail::readString((*it)[i], joinPath(base, i), true);
            if (!tag) return tag.error();
            if (!seen.insert(tag.value()).second)
                return schemaError(joinPath(base, i), "tag \"" + tag.value() + "\" is listed twice");
            rec.meta.tags.push_back(tag.value());
        }
    }
    if (const auto it = json.find("previewSize"); it != json.end()) {
        auto p = previewFromJson(*it, joinPath(path, "previewSize"));
        if (!p) return p.error();
        rec.meta.previewSize = p.value();
    }
    const auto lib = json.find("library");
    if (lib == json.end()) return schemaError(joinPath(path, "library"), "required key is missing");
    auto library = libraryFromJson(*lib, joinPath(path, "library"));
    if (!library) return library.error();
    rec.library = std::move(library.value());
    const auto def = json.find("definition");
    const std::string defPath = joinPath(path, "definition");
    if (def == json.end()) return schemaError(defPath, "required key is missing");
    if (!def->is_object()) return schemaError(defPath, "expected an item object, got " + describe(*def));
    const auto type = def->find("type");
    if (type == def->end() || !type->is_string() ||
        (type->get<std::string>() != "cabinet" && type->get<std::string>() != "panel"))
        return schemaError(joinPath(defPath, "type"), "expected \"cabinet\" or \"panel\", got " +
                                                          (type == def->end() ? std::string("nothing") : describe(*type)));
    rec.definition = *def;
    for (const auto& [key, value] : json.items())
        if (!isKnownComponentKey(key)) rec.extra[key] = value;
    return rec;
}

} // namespace

const FormatSpec& componentFormatSpec() {
    static const FormatSpec spec{std::string(kComponentFormatKind), kComponentFormatVersion, {}};
    return spec;
}

Json componentFileToJson(const ComponentFile& file) {
    Json payload = file.extra.is_object() ? file.extra : Json::object();
    Json list = Json::array();
    for (const auto& c : file.components) {
        Json j = c.extra.is_object() ? c.extra : Json::object();
        j["id"] = c.meta.id;
        j["name"] = c.meta.name;
        j["description"] = c.meta.description;
        j["category"] = c.meta.category;
        j["tags"] = c.meta.tags;
        j["author"] = c.meta.author;
        j["previewSize"] = previewToJson(c.meta.previewSize);
        j["library"] = libraryToJson(c.library);
        j["definition"] = c.definition;
        list.push_back(std::move(j));
    }
    payload["components"] = std::move(list);
    return payload;
}

core::Result<ComponentFile> componentFileFromJson(const Json& payload, std::string_view path) {
    const std::string root = path.empty() ? std::string("/") : std::string(path);
    if (!payload.is_object()) return schemaError(root, "expected the component payload object, got " + describe(payload));
    const auto list = payload.find("components");
    const std::string base = joinPath(path, "components");
    if (list == payload.end()) return schemaError(base, "required key is missing");
    if (!list->is_array()) return schemaError(base, "expected an array of components, got " + describe(*list));
    if (list->empty()) return schemaError(base, "a component file must contain at least one component");
    ComponentFile file;
    std::set<std::string> ids, names;
    for (std::size_t i = 0; i < list->size(); ++i) {
        const std::string p = joinPath(base, i);
        auto rec = recordFromJson((*list)[i], p);
        if (!rec) return rec.error();
        if (!ids.insert(rec.value().meta.id).second)
            return schemaError(joinPath(p, "id"), "component id \"" + rec.value().meta.id + "\" is used twice in this file");
        if (!names.insert(rec.value().meta.name).second)
            return schemaError(joinPath(p, "name"),
                               "component name \"" + rec.value().meta.name + "\" is used twice in this file");
        file.components.push_back(std::move(rec.value()));
    }
    for (const auto& [key, value] : payload.items())
        if (key != "components") file.extra[key] = value;
    return file;
}

core::Result<SaveReport> exportComponentFile(const std::filesystem::path& path, const ComponentFile& file,
                                             const AppInfo& app, const SaveOptions& options) {
    Document document;
    document.format = std::string(kComponentFormatKind);
    document.formatVersion = kComponentFormatVersion;
    document.app = app;
    document.payload = componentFileToJson(file);
    if (auto check = componentFileFromJson(document.payload); !check) {
        core::Error error = check.error();
        error.code = core::ErrorCode::InvalidArgument;
        error.message = "The components cannot be saved to '" + path.string() +
                        "' because the file would not load again: " + error.message;
        return error;
    }
    return saveDocument(path, std::move(document), options);
}

core::Result<ComponentFile> importComponentFileText(std::string_view text, std::string_view sourceName,
                                                    const LoadOptions& options, core::Diagnostics* warnings) {
    const std::string name(sourceName);
    if (text.size() > options.maxFileBytes)
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               "'" + name + "' is " + std::to_string(text.size()) + " bytes; the limit is " +
                                   std::to_string(options.maxFileBytes) + " bytes. It was not parsed.",
                               name);
    auto loaded = parseDocument(text, componentFormatSpec(), options, name);
    if (!loaded) {
        core::Error error = loaded.error();
        if (error.message.find(name) == std::string::npos) error.message = "'" + name + "': " + error.message;
        return error;
    }
    auto file = componentFileFromJson(loaded.value().document.payload, "/payload");
    if (!file) {
        core::Error error = file.error();
        error.message = "'" + name + "' is not a valid component file: " + error.message;
        return error;
    }
    if (warnings != nullptr)
        warnings->insert(warnings->end(), loaded.value().diagnostics.begin(), loaded.value().diagnostics.end());
    return file;
}

core::Result<ComponentFile> importComponentFile(const std::filesystem::path& path, const LoadOptions& options,
                                                core::Diagnostics* warnings) {
    auto text = readFile(path, ReadLimits{options.maxFileBytes});
    if (!text) {
        core::Error error = text.error();
        if (error.message.find(path.string()) == std::string::npos)
            error.message = "'" + path.string() + "': " + error.message;
        return error;
    }
    return importComponentFileText(text.value(), path.string(), options, warnings);
}

} // namespace fos::formats
