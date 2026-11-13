// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/formats/container.hpp"

#include "fos/formats/sha256.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>

namespace fos::formats {
namespace {

using core::ErrorCode;
using core::makeError;

/// Deepest array/object nesting accepted from a file. Protects the recursive parts of
/// JSON handling (dump, key sorting) from stack exhaustion on hostile input.
constexpr int kMaxNesting = 128;

const char* jsonTypeName(const Json& j) {
    switch (j.type()) {
    case Json::value_t::null: return "null";
    case Json::value_t::object: return "an object";
    case Json::value_t::array: return "an array";
    case Json::value_t::string: return "a string";
    case Json::value_t::boolean: return "a boolean";
    case Json::value_t::number_integer:
    case Json::value_t::number_unsigned:
    case Json::value_t::number_float: return "a number";
    default: return "an unsupported value";
    }
}

struct LineColumn {
    std::size_t line = 1, column = 1;
};

/// 1-based line/column of the byte at `offset` (clamped to the text length).
LineColumn locate(std::string_view text, std::size_t offset) {
    LineColumn lc;
    offset = std::min(offset, text.size());
    for (std::size_t i = 0; i < offset; ++i) {
        if (text[i] == '\n') {
            ++lc.line;
            lc.column = 1;
        } else {
            ++lc.column;
        }
    }
    return lc;
}

std::string at(std::string_view text, std::size_t offset) {
    const LineColumn lc = locate(text, offset);
    return "line " + std::to_string(lc.line) + ", column " + std::to_string(lc.column);
}

/// Returns the byte offset where nesting first exceeds kMaxNesting, or npos.
std::size_t findExcessiveNesting(std::string_view text) {
    int depth = 0;
    bool inString = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (c == '\\') ++i;
            else if (c == '"') inString = false;
        } else if (c == '"') {
            inString = true;
        } else if (c == '{' || c == '[') {
            if (++depth > kMaxNesting) return i;
        } else if (c == '}' || c == ']') {
            --depth;
        }
    }
    return std::string_view::npos;
}

core::Result<Json> parseJsonText(std::string_view text, std::string_view sourceName) {
    const std::string name(sourceName);
    if (text.find_first_not_of(" \t\r\n") == std::string_view::npos)
        return makeError(ErrorCode::ParseError, "'" + name + "' is empty; expected a JSON document (line 1, column 1).",
                         name);
    if (const std::size_t deep = findExcessiveNesting(text); deep != std::string_view::npos)
        return makeError(ErrorCode::ParseError,
                         "'" + name + "' nests arrays/objects deeper than " + std::to_string(kMaxNesting) +
                             " levels (" + at(text, deep) + "); this is not a FurnitureOS document.",
                         name);
    try {
        return Json::parse(text.begin(), text.end());
    } catch (const Json::parse_error& e) {
        // e.byte is the 1-based offset of the last byte read.
        std::string detail = e.what();
        if (const auto column = detail.find("column "); column != std::string::npos)
            if (const auto colon = detail.find(": ", column); colon != std::string::npos)
                detail = detail.substr(colon + 2);
        const std::size_t offset = e.byte > 0 ? e.byte - 1 : 0;
        return makeError(ErrorCode::ParseError,
                         "'" + name + "' is not valid JSON (" + at(text, offset) + "): " + detail, name);
    } catch (const std::exception& e) {
        return makeError(ErrorCode::ParseError, "'" + name + "' could not be parsed as JSON: " + e.what(), name);
    }
}

core::Error corrupt(std::string_view sourceName, const std::string& what) {
    return makeError(ErrorCode::CorruptData, "'" + std::string(sourceName) + "' " + what, std::string(sourceName));
}

} // namespace

// ---------------------------------------------------------------- migrations

core::Result<void> MigrationRegistry::add(int fromVersion, MigrationStep step, std::string description) {
    if (fromVersion < 1)
        return makeError(ErrorCode::InvalidArgument,
                         "Migration source version must be >= 1, got " + std::to_string(fromVersion) + ".");
    if (!step)
        return makeError(ErrorCode::InvalidArgument,
                         "Migration " + std::to_string(fromVersion) + " -> " + std::to_string(fromVersion + 1) +
                             " has no function.");
    if (has(fromVersion))
        return makeError(ErrorCode::AlreadyExists,
                         "Migration " + std::to_string(fromVersion) + " -> " + std::to_string(fromVersion + 1) +
                             " is already registered.");
    steps_.emplace(fromVersion, Entry{std::move(step), std::move(description)});
    return {};
}

core::Result<std::vector<AppliedMigration>> MigrationRegistry::run(Json& payload, int fromVersion,
                                                                  int toVersion) const {
    std::vector<AppliedMigration> applied;
    if (fromVersion > toVersion)
        return makeError(ErrorCode::InvalidArgument,
                         "Cannot migrate backwards from version " + std::to_string(fromVersion) + " to " +
                             std::to_string(toVersion) + ".");
    // Check the whole chain first so that nothing runs when a step is missing.
    for (int v = fromVersion; v < toVersion; ++v)
        if (!has(v))
            return makeError(ErrorCode::UnsupportedVersion,
                             "No migration from format version " + std::to_string(v) + " to " +
                                 std::to_string(v + 1) + " is registered; cannot upgrade version " +
                                 std::to_string(fromVersion) + " to " + std::to_string(toVersion) + ".");
    Json working = payload;
    for (int v = fromVersion; v < toVersion; ++v) {
        const Entry& entry = steps_.at(v);
        const std::string label = "Migration " + std::to_string(v) + " -> " + std::to_string(v + 1);
        try {
            if (auto r = entry.step(working); !r) {
                core::Error e = r.error();
                e.message = label + " failed: " + e.message;
                return e;
            }
        } catch (const std::exception& e) {
            return makeError(ErrorCode::CorruptData, label + " failed: " + e.what());
        }
        applied.push_back({v, v + 1, entry.description});
    }
    payload = std::move(working);
    return applied;
}

// ---------------------------------------------------------------- checksum / time

core::Result<std::string> canonicalDump(const Json& value) {
    try {
        // nlohmann::json keeps object keys in a std::map: sorted byte-wise at every level.
        const nlohmann::json sorted = value;
        return sorted.dump(-1, ' ', false, nlohmann::json::error_handler_t::strict);
    } catch (const std::exception& e) {
        return makeError(ErrorCode::InvalidArgument,
                         std::string("The document contains text that is not valid UTF-8 and cannot be saved: ") +
                             e.what());
    }
}

core::Result<std::string> payloadChecksum(const Json& payload) {
    auto text = canonicalDump(payload);
    if (!text) return text.error();
    return sha256Hex(text.value());
}

std::string formatIso8601Utc(std::chrono::system_clock::time_point time) {
    using namespace std::chrono;
    const auto secs = floor<seconds>(time);
    const auto day = floor<days>(secs);
    const year_month_day ymd{day};
    const hh_mm_ss<seconds> hms{secs - day};
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02uT%02d:%02d:%02dZ", static_cast<int>(ymd.year()),
                  static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()),
                  static_cast<int>(hms.hours().count()), static_cast<int>(hms.minutes().count()),
                  static_cast<int>(hms.seconds().count()));
    return buf;
}

// ---------------------------------------------------------------- serialize

core::Result<std::string> serializeDocument(const Document& document) {
    if (document.format.empty())
        return makeError(ErrorCode::InvalidArgument, "Cannot save a document without a format kind.");
    if (document.formatVersion < 1)
        return makeError(ErrorCode::InvalidArgument,
                         "Cannot save '" + document.format + "' with formatVersion " +
                             std::to_string(document.formatVersion) + "; versions start at 1.");
    if (!document.payload.is_object())
        return makeError(ErrorCode::InvalidArgument,
                         "Cannot save '" + document.format + "': the payload must be a JSON object, got " +
                             jsonTypeName(document.payload) + ".");
    auto checksum = payloadChecksum(document.payload);
    if (!checksum) return checksum.error();

    Json envelope = Json::object();
    envelope["format"] = document.format;
    envelope["formatVersion"] = document.formatVersion;
    envelope["app"] = Json{{"name", document.app.name}, {"version", document.app.version}};
    envelope["savedAt"] = document.savedAt;
    envelope["checksum"] = Json{{"algorithm", "sha256"}, {"value", checksum.value()}};
    envelope["payload"] = document.payload;
    if (document.extra.is_object())
        for (const auto& [key, value] : document.extra.items())
            if (!envelope.contains(key)) envelope[key] = value;
    try {
        return envelope.dump(2, ' ', false, Json::error_handler_t::strict) + "\n";
    } catch (const std::exception& e) {
        return makeError(ErrorCode::InvalidArgument,
                         std::string("The document contains text that is not valid UTF-8 and cannot be saved: ") +
                             e.what());
    }
}

// ---------------------------------------------------------------- parse

core::Result<LoadedDocument> parseDocument(std::string_view text, const FormatSpec& spec,
                                           const LoadOptions& options, std::string_view sourceName) {
    auto parsed = parseJsonText(text, sourceName);
    if (!parsed) return parsed.error();
    Json& root = parsed.value();
    const std::string name(sourceName);

    if (!root.is_object())
        return corrupt(name, std::string("is not a FurnitureOS document: the top-level value is ") +
                                 jsonTypeName(root) + ", expected an object.");

    // --- format kind
    const auto format = root.find("format");
    if (format == root.end())
        return corrupt(name, "is not a FurnitureOS document: the \"format\" key is missing (expected \"" +
                                 spec.kind + "\").");
    if (!format->is_string())
        return corrupt(name, std::string("has a \"format\" that is ") + jsonTypeName(*format) +
                                 ", expected the string \"" + spec.kind + "\".");
    LoadedDocument out;
    out.document.format = format->get<std::string>();
    if (out.document.format != spec.kind)
        return makeError(ErrorCode::Unsupported,
                         "'" + name + "' is a \"" + out.document.format + "\" file, but a \"" + spec.kind +
                             "\" file is expected here.",
                         name);

    // --- writer (informational; needed for the version message)
    if (const auto app = root.find("app"); app != root.end()) {
        if (!app->is_object())
            return corrupt(name, std::string("has an \"app\" entry that is ") + jsonTypeName(*app) +
                                     ", expected an object.");
        const auto appText = [&](const char* key) {
            const auto it = app->find(key);
            return it != app->end() && it->is_string() ? it->get<std::string>() : std::string();
        };
        out.document.app.name = appText("name");
        out.document.app.version = appText("version");
    } else {
        out.document.app.name.clear();
    }

    // --- version
    const auto version = root.find("formatVersion");
    if (version == root.end()) return corrupt(name, "has no \"formatVersion\".");
    if (!version->is_number_integer())
        return corrupt(name, std::string("has a \"formatVersion\" that is ") +
                                 (version->is_number() ? "not a whole number" : jsonTypeName(*version)) +
                                 ", expected an integer >= 1.");
    const auto fileVersion = version->is_number_unsigned()
                                 ? static_cast<long long>(std::min<std::uint64_t>(
                                       version->get<std::uint64_t>(), std::uint64_t{1} << 62))
                                 : version->get<long long>();
    if (fileVersion < 1)
        return corrupt(name, "has formatVersion " + std::to_string(fileVersion) + "; versions start at 1.");
    if (fileVersion > spec.currentVersion) {
        std::string writer = "the application version that wrote it is not recorded in the file";
        if (!out.document.app.version.empty())
            writer = "it was saved by " +
                     (out.document.app.name.empty() ? std::string("an application") : out.document.app.name) +
                     " " + out.document.app.version;
        return makeError(ErrorCode::UnsupportedVersion,
                         "'" + name + "' uses " + spec.kind + " format version " + std::to_string(fileVersion) +
                             ", but this version of FurnitureOS supports only up to version " +
                             std::to_string(spec.currentVersion) + " (" + writer +
                             "). Nothing was loaded; open the file with a newer FurnitureOS.",
                         name);
    }
    out.fileVersion = static_cast<int>(fileVersion);

    if (const auto savedAt = root.find("savedAt"); savedAt != root.end()) {
        if (!savedAt->is_string())
            return corrupt(name, std::string("has a \"savedAt\" that is ") + jsonTypeName(*savedAt) +
                                     ", expected an ISO-8601 string.");
        out.document.savedAt = savedAt->get<std::string>();
    }

    // --- payload
    const auto payload = root.find("payload");
    if (payload == root.end()) return corrupt(name, "has no \"payload\"; the file is incomplete.");
    if (!payload->is_object())
        return corrupt(name, std::string("has a \"payload\" that is ") + jsonTypeName(*payload) +
                                 ", expected an object.");

    // --- checksum
    std::string problem; // empty = verified
    std::string problemCode = "formats.container.checksum_mismatch";
    const auto checksum = root.find("checksum");
    if (checksum == root.end()) {
        problem = "has no \"checksum\", so its integrity cannot be verified";
        problemCode = "formats.container.checksum_missing";
    } else if (!checksum->is_object() || !checksum->contains("algorithm") || !checksum->contains("value") ||
               !(*checksum)["algorithm"].is_string() || !(*checksum)["value"].is_string()) {
        problem = "has a malformed \"checksum\" entry (expected {\"algorithm\": \"sha256\", \"value\": \"<hex>\"})";
    } else if ((*checksum)["algorithm"].get<std::string>() != "sha256") {
        problem = "uses the unknown checksum algorithm \"" + (*checksum)["algorithm"].get<std::string>() +
                  "\" (only \"sha256\" is supported)";
    } else {
        auto actual = payloadChecksum(*payload);
        if (!actual) return actual.error();
        const std::string stored = (*checksum)["value"].get<std::string>();
        if (stored != actual.value())
            problem = "failed its integrity check: the stored SHA-256 is " + stored +
                      " but the payload hashes to " + actual.value() +
                      " (the file was damaged or edited outside FurnitureOS)";
    }
    if (!problem.empty()) {
        if (!options.allowChecksumMismatch) return corrupt(name, problem + ".");
        out.diagnostics.push_back({core::Severity::Warning, problemCode,
                                   "'" + name + "' " + problem + "; it was loaded anyway on request and may contain damaged data.",
                                   name});
    }

    // --- unknown envelope keys are kept
    for (const auto& [key, value] : root.items()) {
        if (key == "format" || key == "formatVersion" || key == "app" || key == "savedAt" || key == "checksum" ||
            key == "payload")
            continue;
        out.document.extra[key] = value;
    }

    // --- migrate
    out.document.payload = std::move(*payload);
    auto migrated = spec.migrations.run(out.document.payload, out.fileVersion, spec.currentVersion);
    if (!migrated) {
        core::Error e = migrated.error();
        e.message = "'" + name + "' (format version " + std::to_string(out.fileVersion) +
                    ") could not be upgraded: " + e.message;
        e.context = name;
        return e;
    }
    out.migrations = std::move(migrated.value());
    out.document.formatVersion = spec.currentVersion;
    return out;
}

} // namespace fos::formats
