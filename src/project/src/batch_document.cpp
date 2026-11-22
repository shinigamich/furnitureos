// SPDX-License-Identifier: AGPL-3.0-only
// Batch document: checks, JSON codec and the `.fosbatch` container file.
#include "fos/project/batch.hpp"

#include "fos/project/codec.hpp"
#include "json_reader.hpp"

#include <set>

#ifndef FOS_PROJECT_APP_VERSION
#define FOS_PROJECT_APP_VERSION "0.0.0"
#endif

namespace fos::project {
using namespace detail;
using formats::Json;

namespace {

bool safeKeyChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
           c == '-';
}

std::filesystem::path utf8Path(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

std::string utf8Text(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

core::Error invalid(const std::string& message, const std::string& context = {}) {
    return core::makeError(core::ErrorCode::InvalidArgument, message, context);
}

core::Result<BatchEntry> entryFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "a batch project entry");
    BatchEntry e;
    r.text("path", e.path, true, true);
    r.integerInRange("quantity", e.quantity, 1, kMaxBatchQuantity);
    r.text("key", e.key);
    r.text("note", e.note);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return e;
}

core::Result<production::EdgeBandingOptions> edgeBandingFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "the edge banding options");
    production::EdgeBandingOptions o;
    r.number("overhangPerEnd", o.overhangPerEnd, Num::NonNegative);
    r.number("wastePercent", o.wastePercent, Num::NonNegative);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return o;
}

core::Result<std::map<std::string, std::string>> substitutionsFromJson(const Json& json, std::string_view path) {
    if (!json.is_object()) return schemaError(path, "expected an object of material id -> material id, got " + describe(json));
    std::map<std::string, std::string> out;
    for (auto it = json.begin(); it != json.end(); ++it) {
        if (!it.value().is_string())
            return schemaError(joinPath(path, it.key()), "expected a material id (string), got " + describe(it.value()));
        out[it.key()] = it.value().get<std::string>();
    }
    return out;
}

core::Result<Batch> metaInto(const Json& json, std::string_view path) {
    Reader r(json, path, "the batch description");
    Batch b;
    r.text("name", b.name);
    r.text("date", b.date);
    r.text("notes", b.notes);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return b;
}

} // namespace

std::string batchKey(const BatchEntry& entry) {
    if (!entry.key.empty()) return entry.key;
    std::string s = utf8Text(utf8Path(entry.path).stem());
    for (char& c : s)
        if (!safeKeyChar(c)) c = '_';
    return s.empty() ? std::string("_") : s;
}

core::Result<void> checkBatch(const Batch& batch) {
    if (batch.entries.size() > kMaxBatchProjects)
        return invalid("The batch lists " + std::to_string(batch.entries.size()) + " projects; at most " +
                       std::to_string(kMaxBatchProjects) + " are allowed.");
    long long total = 0;
    std::map<std::string, std::size_t> keys;
    for (std::size_t i = 0; i < batch.entries.size(); ++i) {
        const BatchEntry& e = batch.entries[i];
        const std::string where = "Batch entry " + std::to_string(i + 1) + " (\"" + e.path + "\")";
        if (e.path.empty()) return invalid("Batch entry " + std::to_string(i + 1) + " has no project path.");
        if (e.quantity < 1 || e.quantity > kMaxBatchQuantity)
            return invalid(where + " has quantity " + std::to_string(e.quantity) + "; allowed is 1 to " +
                               std::to_string(kMaxBatchQuantity) + ".",
                           e.path);
        total += e.quantity;
        for (const char c : e.key)
            if (!safeKeyChar(c))
                return invalid(where + " has key \"" + e.key + "\"; keys may only contain A-Z, a-z, 0-9, '.', '_' and '-'.",
                               e.path);
        const std::string key = batchKey(e);
        const auto [it, inserted] = keys.emplace(key, i);
        if (!inserted)
            return invalid(where + " has the same project key \"" + key + "\" as entry " + std::to_string(it->second + 1) +
                               " (\"" + batch.entries[it->second].path + "\"); set a distinct \"key\" on one of them.",
                           e.path);
    }
    if (total > kMaxBatchTotalQuantity)
        return invalid("The batch quantities add up to " + std::to_string(total) + " copies; at most " +
                       std::to_string(kMaxBatchTotalQuantity) + " are allowed.");
    for (const auto& [from, to] : batch.materialSubstitutions) {
        if (from.empty() || to.empty()) return invalid("A material substitution has an empty material id.");
        if (from == to) return invalid("Material substitution \"" + from + "\" -> \"" + to + "\" maps a material onto itself.");
        if (batch.materialSubstitutions.count(to) != 0)
            return invalid("Material substitution \"" + from + "\" -> \"" + to + "\" points at \"" + to +
                           "\", which is itself substituted; substitutions are not chained, name the final material.");
    }
    return {};
}

Json batchToJson(const Batch& b) {
    Json j = Json::object();
    j["batch"] = Json{{"name", b.name}, {"date", b.date}, {"notes", b.notes}};
    Json projects = Json::array();
    for (const auto& e : b.entries) {
        Json p = Json::object();
        p["path"] = e.path;
        p["quantity"] = e.quantity;
        if (!e.key.empty()) p["key"] = e.key;
        if (!e.note.empty()) p["note"] = e.note;
        projects.push_back(std::move(p));
    }
    j["projects"] = std::move(projects);
    j["cutting"] = toJson(b.cutting);
    j["edgeBanding"] = Json{{"overhangPerEnd", b.edgeBanding.overhangPerEnd}, {"wastePercent", b.edgeBanding.wastePercent}};
    j["cutListGroupAcrossAssemblies"] = b.cutListGroupAcrossAssemblies;
    Json subs = Json::object();
    for (const auto& [from, to] : b.materialSubstitutions) subs[from] = to;
    j["materialSubstitutions"] = std::move(subs);
    appendExtra(j, b.extra);
    return j;
}

core::Result<Batch> batchFromJson(const Json& payload, std::string_view path) {
    Reader r(payload, path, "a batch payload");
    Batch b;
    r.nested("batch", b, metaInto, true);
    r.limitArray("projects", kMaxBatchProjects, "batch projects");
    r.array("projects", b.entries, entryFromJson);
    r.nested("cutting", b.cutting, projectCuttingSettingsFromJson);
    r.nested("edgeBanding", b.edgeBanding, edgeBandingFromJson);
    r.boolean("cutListGroupAcrossAssemblies", b.cutListGroupAcrossAssemblies);
    r.nested("materialSubstitutions", b.materialSubstitutions, substitutionsFromJson);
    if (!r.ok()) return r.error();
    b.extra = r.unknown();
    if (auto ok = checkBatch(b); !ok) return schemaError(path.empty() ? "/" : path, ok.error().message);
    return b;
}

formats::FormatSpec batchFormatSpec() {
    formats::FormatSpec spec;
    spec.kind = kBatchFormatKind;
    spec.currentVersion = kBatchFormatVersion; // version 1: no migrations
    return spec;
}

namespace {

core::Result<formats::Document> checkedBatchDocument(const Batch& batch, std::string savedAt) {
    formats::Document d;
    d.format = kBatchFormatKind;
    d.formatVersion = kBatchFormatVersion;
    d.app.version = FOS_PROJECT_APP_VERSION;
    d.savedAt = std::move(savedAt);
    d.payload = batchToJson(batch);
    auto back = batchFromJson(d.payload, "/payload");
    if (!back)
        return core::makeError(back.error().code,
                               "The batch cannot be saved because it would not load back: " + back.error().message,
                               back.error().context);
    return d;
}

} // namespace

core::Result<std::string> serializeBatch(const Batch& batch, const std::string& savedAt) {
    auto d = checkedBatchDocument(batch, savedAt);
    if (!d) return d.error();
    return formats::serializeDocument(d.value());
}

core::Result<Batch> parseBatchDocument(std::string_view text, const formats::LoadOptions& options,
                                       std::string_view sourceName) {
    auto loaded = formats::parseDocument(text, batchFormatSpec(), options, sourceName);
    if (!loaded) return loaded.error();
    return batchFromJson(loaded.value().document.payload, "/payload");
}

core::Result<Batch> loadBatchDocument(const std::filesystem::path& path, const formats::LoadOptions& options) {
    auto loaded = formats::loadDocument(path, batchFormatSpec(), options);
    if (!loaded) return loaded.error();
    return batchFromJson(loaded.value().document.payload, "/payload");
}

core::Result<formats::SaveReport> saveBatch(const std::filesystem::path& path, const Batch& batch,
                                            formats::SaveOptions options) {
    auto d = checkedBatchDocument(batch, {});
    if (!d) return d.error();
    return formats::saveDocument(path, std::move(d.value()), options);
}

} // namespace fos::project
