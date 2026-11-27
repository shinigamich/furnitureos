// SPDX-License-Identifier: AGPL-3.0-only
// `.fosremnants` workshop remnant store: codec, value rules, load/save.
#include "fos/project/remnant_store.hpp"

#include "fos/core/units.hpp"
#include "json_reader.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#ifndef FOS_PROJECT_APP_VERSION
#define FOS_PROJECT_APP_VERSION "0.0.0"
#endif

namespace fos::project {

using detail::Json;
using detail::Num;
using detail::Reader;

const char* remnantGrainToken(RemnantGrain grain) noexcept {
    switch (grain) {
    case RemnantGrain::AlongLength: return "length";
    case RemnantGrain::AlongWidth: return "width";
    case RemnantGrain::None: break;
    }
    return "none";
}

std::optional<RemnantGrain> remnantGrainFromToken(std::string_view token) noexcept {
    if (token == "none") return RemnantGrain::None;
    if (token == "length") return RemnantGrain::AlongLength;
    if (token == "width") return RemnantGrain::AlongWidth;
    return std::nullopt;
}

const StoredRemnant* RemnantStore::find(std::string_view id) const {
    for (const auto& r : remnants)
        if (r.id == id) return &r;
    return nullptr;
}

const formats::FormatSpec& remnantsFormatSpec() {
    static const formats::FormatSpec spec{kRemnantsFormatKind, kRemnantsFormatVersion, {}};
    return spec;
}

bool isIsoDate(std::string_view t) noexcept {
    if (t.size() != 10 || t[4] != '-' || t[7] != '-') return false;
    for (std::size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u})
        if (t[i] < '0' || t[i] > '9') return false;
    const auto num = [&](std::size_t from, std::size_t n) {
        int v = 0;
        for (std::size_t i = from; i < from + n; ++i) v = v * 10 + (t[i] - '0');
        return v;
    };
    const int year = num(0, 4), month = num(5, 2), day = num(8, 2);
    if (month < 1 || month > 12 || day < 1) return false;
    static constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    const int maxDay = month == 2 && leap ? 29 : kDays[month - 1];
    return day <= maxDay;
}

namespace {

Json entryToJson(const StoredRemnant& r) {
    Json j = Json::object();
    j["id"] = r.id;
    j["material"] = r.materialId;
    j["length"] = r.length;
    j["width"] = r.width;
    j["thickness"] = r.thickness;
    j["grain"] = remnantGrainToken(r.grain);
    j["dateAdded"] = r.dateAdded;
    if (!r.location.empty()) j["location"] = r.location;
    if (!r.note.empty()) j["note"] = r.note;
    if (!r.reservedBy.empty()) j["reservedBy"] = r.reservedBy;
    if (!r.source.empty()) j["source"] = r.source;
    return j;
}

core::Result<StoredRemnant> entryFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "a remnant");
    StoredRemnant m;
    r.text("id", m.id, true, true);
    r.text("material", m.materialId, true, true);
    r.number("length", m.length, Num::Positive, true);
    r.number("width", m.width, Num::Positive, true);
    r.number("thickness", m.thickness, Num::Positive, true);
    r.token("grain", m.grain, remnantGrainFromToken, "\"none\", \"length\", \"width\"");
    r.text("dateAdded", m.dateAdded, true, true);
    r.text("location", m.location);
    r.text("note", m.note);
    r.text("reservedBy", m.reservedBy);
    r.text("source", m.source);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    if (!isIsoDate(m.dateAdded))
        return detail::schemaError(r.at("dateAdded"), "expected a date YYYY-MM-DD, got \"" + m.dateAdded + "\"");
    return m;
}

core::Error invalid(const std::string& message, const std::string& entity) {
    return core::makeError(core::ErrorCode::InvalidArgument, message, entity);
}

} // namespace

Json remnantStoreToJson(const RemnantStore& store) {
    Json j = Json::object();
    Json list = Json::array();
    for (const auto& r : store.remnants) list.push_back(entryToJson(r));
    j["remnants"] = std::move(list);
    detail::appendExtra(j, store.extra);
    return j;
}

core::Result<RemnantStore> remnantStoreFromJson(const Json& payload, std::string_view path) {
    Reader r(payload, path, "a remnant store");
    RemnantStore store;
    r.limitArray("remnants", kMaxStoredRemnants, "remnants");
    r.array("remnants", store.remnants, entryFromJson);
    store.extra = r.unknown();
    if (!r.ok()) return r.error();
    std::set<std::string> seen;
    for (std::size_t i = 0; i < store.remnants.size(); ++i)
        if (!seen.insert(store.remnants[i].id).second)
            return detail::schemaError(detail::joinPath(detail::joinPath(detail::joinPath(path, "remnants"), i), "id"),
                                       "duplicate remnant id \"" + store.remnants[i].id + "\"");
    return store;
}

core::Diagnostics validateRemnantStore(const RemnantStore& store) {
    core::Diagnostics out;
    const auto error = [&](const std::string& message, const std::string& entity) {
        out.push_back({core::Severity::Error, "project.remnants.invalid", message, entity});
    };
    if (store.remnants.size() > kMaxStoredRemnants)
        error("The remnant store holds " + std::to_string(store.remnants.size()) + " remnants; at most " +
                  std::to_string(kMaxStoredRemnants) + " are allowed.",
              "");
    std::set<std::string> seen;
    for (const auto& r : store.remnants) {
        const std::string name = "Remnant '" + r.id + "'";
        if (r.id.empty()) error("A remnant has an empty id.", "");
        else if (!seen.insert(r.id).second) error(name + " is listed twice (ids must be unique).", r.id);
        if (r.materialId.empty()) error(name + " has no material id.", r.id);
        const std::pair<const char*, double> sizes[] = {{"length", r.length}, {"width", r.width}, {"thickness", r.thickness}};
        for (const auto& [what, value] : sizes)
            if (!std::isfinite(value) || !(value > 0))
                error(name + " must have a positive " + what + ", got " + core::formatLength(value) + " mm.", r.id);
        if (!isIsoDate(r.dateAdded))
            error(name + " has date added \"" + r.dateAdded + "\"; expected YYYY-MM-DD.", r.id);
    }
    return out;
}

core::Result<RemnantStore> loadRemnantStore(const std::filesystem::path& path, const formats::LoadOptions& options) {
    auto loaded = formats::loadDocument(path, remnantsFormatSpec(), options);
    if (!loaded) return loaded.error();
    auto store = remnantStoreFromJson(loaded.value().document.payload, "/payload");
    if (!store) {
        core::Error e = store.error();
        e.message = "'" + path.filename().string() + "' is not a valid remnant store: " + e.message;
        return e;
    }
    return store;
}

core::Result<formats::SaveReport> saveRemnantStore(const std::filesystem::path& path, const RemnantStore& store,
                                                   const formats::SaveOptions& options) {
    for (const auto& d : validateRemnantStore(store))
        return invalid("The remnant store was not written to '" + path.filename().string() + "': " + d.message,
                       d.entityId);
    formats::Document document;
    document.format = kRemnantsFormatKind;
    document.formatVersion = kRemnantsFormatVersion;
    document.app.name = "FurnitureOS";
    document.app.version = FOS_PROJECT_APP_VERSION;
    document.payload = remnantStoreToJson(store);
    return formats::saveDocument(path, std::move(document), options);
}

std::string nextRemnantId(const RemnantStore& store) {
    long long highest = 0;
    for (const auto& r : store.remnants) {
        if (r.id.size() < 2 || r.id.size() > 12 || r.id[0] != 'R') continue;
        if (!std::all_of(r.id.begin() + 1, r.id.end(), [](char c) { return c >= '0' && c <= '9'; })) continue;
        highest = std::max(highest, std::stoll(r.id.substr(1)));
    }
    std::string digits = std::to_string(highest + 1);
    if (digits.size() < 4) digits.insert(0, 4 - digits.size(), '0');
    return "R" + digits;
}

std::string describeRemnant(const StoredRemnant& r) {
    std::string s = r.id + "  " + r.materialId + "  " + core::formatLength(r.length) + " x " +
                    core::formatLength(r.width) + " x " + core::formatLength(r.thickness) + " mm, grain " +
                    remnantGrainToken(r.grain) + ", added " + r.dateAdded;
    if (!r.location.empty()) s += ", at " + r.location;
    if (!r.reservedBy.empty()) s += ", reserved by " + r.reservedBy;
    if (!r.note.empty()) s += " (" + r.note + ")";
    return s;
}

} // namespace fos::project
