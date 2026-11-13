// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/plugins/manifest.hpp"

#include "fos/formats/json.hpp"

#include <set>

namespace fos::plugins {
namespace {

using formats::Json;

bool isLower(char c) { return c >= 'a' && c <= 'z'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlnum(char c) { return isLower(c) || isDigit(c) || (c >= 'A' && c <= 'Z'); }

const char* typeName(const Json& v) {
    switch (v.type()) {
    case Json::value_t::null: return "null";
    case Json::value_t::object: return "an object";
    case Json::value_t::array: return "an array";
    case Json::value_t::string: return "a string";
    case Json::value_t::boolean: return "a boolean";
    case Json::value_t::number_integer:
    case Json::value_t::number_unsigned: return "an integer";
    case Json::value_t::number_float: return "a number";
    default: return "an unsupported value";
    }
}

/// Maximum nesting of arrays/objects, scanning the raw text (strings skipped). Runs before
/// the parser so that pathological nesting is rejected without building it.
int nestingDepth(std::string_view text) {
    int depth = 0, maxDepth = 0;
    bool inString = false, escape = false;
    for (char c : text) {
        if (inString) {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') inString = true;
        else if (c == '[' || c == '{') maxDepth = std::max(maxDepth, ++depth);
        else if (c == ']' || c == '}') --depth;
    }
    return maxDepth;
}

/// Collects diagnostics with JSON-pointer prefixes.
class Checker {
public:
    explicit Checker(std::string entity) : entity_(std::move(entity)) {}

    void error(const std::string& path, const std::string& code, const std::string& message) {
        add(core::Severity::Error, path, code, message);
    }
    void warning(const std::string& path, const std::string& code, const std::string& message) {
        add(core::Severity::Warning, path, code, message);
    }
    void setEntity(std::string entity) {
        entity_ = std::move(entity);
        for (auto& d : diagnostics) d.entityId = entity_;
    }

    /// Returns the string at obj[key] or nullopt (with an error when required or ill-typed).
    std::optional<std::string> string(const Json& obj, const std::string& base, const char* key, bool required) {
        const std::string path = base + "/" + key;
        const auto it = obj.find(key);
        if (it == obj.end()) {
            if (required) error(path, "plugins.manifest.missing_key", std::string("required key \"") + key + "\" is missing.");
            return std::nullopt;
        }
        if (!it->is_string()) {
            error(path, "plugins.manifest.wrong_type", std::string("expected a string, found ") + typeName(*it) + ".");
            return std::nullopt;
        }
        return it->get<std::string>();
    }

    std::optional<std::string> nonEmptyString(const Json& obj, const std::string& base, const char* key, bool required) {
        auto s = string(obj, base, key, required);
        if (s && s->find_first_not_of(" \t\r\n") == std::string::npos) {
            error(base + "/" + key, "plugins.manifest.empty", "must not be empty.");
            return std::nullopt;
        }
        return s;
    }

    std::optional<SemVer> version(const Json& obj, const std::string& base, const char* key, bool required,
                                  std::string* textOut = nullptr) {
        auto s = string(obj, base, key, required);
        if (!s) return std::nullopt;
        if (textOut) *textOut = *s;
        auto v = parseSemVer(*s);
        if (!v.ok()) {
            error(base + "/" + key, "plugins.manifest.bad_version", v.error().message);
            return std::nullopt;
        }
        return v.value();
    }

    void unknownKeys(const Json& obj, const std::string& base, const std::set<std::string>& known) {
        for (auto it = obj.begin(); it != obj.end(); ++it)
            if (!known.count(it.key()))
                warning(base + "/" + it.key(), "plugins.manifest.unknown_key",
                        "unknown key \"" + it.key() + "\" is ignored by this version of FurnitureOS.");
    }

    core::Diagnostics diagnostics;

private:
    void add(core::Severity s, const std::string& path, const std::string& code, const std::string& message) {
        diagnostics.push_back(core::Diagnostic{s, code, (path.empty() ? std::string("/") : path) + ": " + message, entity_});
    }
    std::string entity_;
};

/// SPDX license-id token: [A-Za-z0-9.-]+ with an optional trailing '+'.
bool isSpdxIdToken(std::string_view t) {
    if (t.empty()) return false;
    if (t.back() == '+') t.remove_suffix(1);
    if (t.empty()) return false;
    for (char c : t)
        if (!isAlnum(c) && c != '.' && c != '-') return false;
    return true;
}

/// "MIT", "Apache-2.0", "GPL-2.0+", "MIT OR Apache-2.0", "GPL-2.0-only WITH Classpath-exception-2.0".
std::string checkLicense(const std::string& text) {
    std::vector<std::string> tokens;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == ' ') {
            ++i;
            continue;
        }
        const auto end = text.find(' ', i);
        tokens.push_back(text.substr(i, end == std::string::npos ? std::string::npos : end - i));
        i = end == std::string::npos ? text.size() : end;
    }
    if (tokens.empty()) return "must not be empty.";
    for (std::size_t k = 0; k < tokens.size(); ++k) {
        const bool operatorSlot = k % 2 == 1;
        const auto& t = tokens[k];
        if (operatorSlot) {
            if (t != "AND" && t != "OR" && t != "WITH")
                return "\"" + text + "\" is not an SPDX license identifier or a simple expression (expected AND, OR or WITH, found \"" + t + "\"; parentheses are not supported).";
        } else if (!isSpdxIdToken(t)) {
            return "\"" + text + "\" is not an SPDX license identifier (\"" + t + "\" may contain only letters, digits, '.', '-' and a trailing '+').";
        }
    }
    if (tokens.size() % 2 == 0) return "\"" + text + "\" ends with an operator.";
    return {};
}

std::string checkHomepage(const std::string& url) {
    constexpr std::string_view scheme = "https://";
    if (url.compare(0, scheme.size(), scheme) != 0)
        return "\"" + url + "\" must be an https:// URL (other schemes are refused).";
    for (unsigned char c : url)
        if (c <= 0x20 || c == 0x7f) return "\"" + url + "\" contains whitespace or control characters.";
    const auto host = url.substr(scheme.size(), url.find_first_of("/?#", scheme.size()) - scheme.size());
    if (host.empty()) return "\"" + url + "\" has no host name.";
    if (host.find('@') != std::string::npos) return "\"" + url + "\" must not contain user information ('@').";
    return {};
}

/// BCP-47-like tag: 2-3 lower-case letters, then optional "-xx" subtags of 2-8 alphanumerics.
bool isLanguageTag(std::string_view tag) {
    std::size_t i = 0;
    while (i < tag.size() && isLower(tag[i])) ++i;
    if (i < 2 || i > 3) return false;
    while (i < tag.size()) {
        if (tag[i] != '-') return false;
        std::size_t j = ++i;
        while (j < tag.size() && isAlnum(tag[j])) ++j;
        if (j - i < 2 || j - i > 8) return false;
        i = j;
    }
    return true;
}

} // namespace

const char* toString(ExtensionKind kind) noexcept {
    switch (kind) {
    case ExtensionKind::Libraries: return "libraries";
    case ExtensionKind::Templates: return "templates";
    case ExtensionKind::Reports: return "reports";
    case ExtensionKind::Postprocessors: return "postprocessors";
    case ExtensionKind::Importers: return "importers";
    }
    return "unknown";
}

bool isSupported(ExtensionKind kind) noexcept { return kind == ExtensionKind::Libraries; }

std::optional<ExtensionKind> extensionKindFromKey(std::string_view key) noexcept {
    for (auto k : {ExtensionKind::Libraries, ExtensionKind::Templates, ExtensionKind::Reports,
                   ExtensionKind::Postprocessors, ExtensionKind::Importers})
        if (key == toString(k)) return k;
    return std::nullopt;
}

std::string checkPluginId(std::string_view id) {
    if (id.size() < 3) return "a plugin id must be at least 3 characters long";
    if (id.size() > 128) return "a plugin id must be at most 128 characters long";
    std::size_t segments = 0;
    std::size_t i = 0;
    while (true) {
        // segment: [a-z][a-z0-9]*(-[a-z0-9]+)*
        if (i >= id.size() || !isLower(id[i]))
            return "each dot-separated segment must start with a lower-case letter a-z";
        ++i;
        while (i < id.size() && id[i] != '.') {
            const char c = id[i];
            if (c == '-') {
                if (i + 1 >= id.size() || !(isLower(id[i + 1]) || isDigit(id[i + 1])))
                    return "a hyphen must be followed by a lower-case letter or digit";
            } else if (!isLower(c) && !isDigit(c)) {
                return std::string("character '") + c + "' is not allowed (use a-z, 0-9, '-' and '.')";
            }
            ++i;
        }
        ++segments;
        if (i >= id.size()) break;
        ++i; // skip '.'
    }
    if (segments < 2) return "a plugin id needs at least two dot-separated segments (reverse-DNS, e.g. \"org.example.oak-boards\")";
    return {};
}

std::string checkRelativePath(std::string_view path) {
    if (path.empty()) return "the path is empty";
    for (unsigned char c : path)
        if (c < 0x20 || c == 0x7f) return "the path contains control characters";
    if (path.find('\\') != std::string_view::npos) return "the path contains a backslash; use '/' as separator";
    if (path.front() == '/') return "the path is absolute; it must be relative to the plugin directory";
    if (path.find(':') != std::string_view::npos) return "the path contains ':' (drive letters and URLs are not allowed)";
    std::size_t start = 0;
    while (true) {
        const auto end = path.find('/', start);
        const auto part = path.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (part.empty()) return "the path contains an empty component ('//' or a trailing '/')";
        if (part == "..") return "the path contains '..'; it must stay inside the plugin directory";
        if (part == ".") return "the path contains a '.' component";
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return {};
}

bool isCompatible(const Compatibility& c, const SemVer& app) noexcept {
    if (app < c.minAppVersion) return false;
    if (c.maxAppVersion && app > *c.maxAppVersion) return false;
    return true;
}

ManifestValidation parseManifest(std::string_view text, const PluginLimits& limits, std::string_view sourceName) {
    ManifestValidation out;
    Checker ck{std::string(sourceName)};
    auto finish = [&] {
        out.diagnostics = std::move(ck.diagnostics);
        return out;
    };

    if (text.size() > limits.maxManifestBytes) {
        ck.error("", "plugins.manifest.too_large",
                 "the manifest is " + std::to_string(text.size()) + " bytes; the limit is " +
                     std::to_string(limits.maxManifestBytes) + " bytes.");
        return finish();
    }
    if (const int depth = nestingDepth(text); depth > limits.maxJsonDepth) {
        ck.error("", "plugins.manifest.too_deep",
                 "the manifest nests arrays/objects " + std::to_string(depth) + " levels deep; the limit is " +
                     std::to_string(limits.maxJsonDepth) + ".");
        return finish();
    }
    Json root;
    try {
        root = Json::parse(text.begin(), text.end());
    } catch (const Json::exception& e) {
        ck.error("", "plugins.manifest.parse_error", std::string("the manifest is not valid JSON (") + e.what() + ").");
        return finish();
    }
    if (!root.is_object()) {
        ck.error("", "plugins.manifest.wrong_type", std::string("the manifest must be a JSON object, found ") + typeName(root) + ".");
        return finish();
    }
    auto& m = out.manifest;

    // id first, so that every diagnostic can name the plugin.
    if (auto id = ck.string(root, "", "id", true)) {
        m.id = *id;
        if (auto why = checkPluginId(*id); !why.empty())
            ck.error("/id", "plugins.manifest.bad_id", "\"" + *id + "\" is not a valid plugin id: " + why + ".");
        else
            ck.setEntity(*id);
    }

    if (auto it = root.find("manifestVersion"); it == root.end()) {
        ck.error("/manifestVersion", "plugins.manifest.missing_key", "required key \"manifestVersion\" is missing.");
    } else if (!it->is_number_integer()) {
        ck.error("/manifestVersion", "plugins.manifest.wrong_type", std::string("expected the integer 1, found ") + typeName(*it) + ".");
    } else {
        const auto v = it->get<std::int64_t>();
        if (v > kManifestVersion)
            ck.error("/manifestVersion", "plugins.manifest.unsupported_version",
                     "manifest version " + std::to_string(v) + " is newer than this version of FurnitureOS supports (1).");
        else if (v < 1)
            ck.error("/manifestVersion", "plugins.manifest.bad_value", "manifest version " + std::to_string(v) + " is invalid; use 1.");
        else
            m.manifestVersion = static_cast<int>(v);
    }

    if (auto s = ck.nonEmptyString(root, "", "name", true)) m.name = *s;
    if (auto v = ck.version(root, "", "version", true, &m.versionText)) m.version = *v;
    if (auto s = ck.string(root, "", "description", true)) m.description = *s;
    if (auto s = ck.nonEmptyString(root, "", "author", true)) m.author = *s;
    if (auto s = ck.string(root, "", "license", true)) {
        if (auto why = checkLicense(*s); !why.empty()) ck.error("/license", "plugins.manifest.bad_license", why);
        else m.license = *s;
    }
    if (auto s = ck.string(root, "", "homepage", false)) {
        if (auto why = checkHomepage(*s); !why.empty()) ck.error("/homepage", "plugins.manifest.bad_homepage", why);
        else m.homepage = *s;
    }

    // compatibility
    if (auto it = root.find("compatibility"); it == root.end()) {
        ck.error("/compatibility", "plugins.manifest.missing_key", "required key \"compatibility\" is missing.");
    } else if (!it->is_object()) {
        ck.error("/compatibility", "plugins.manifest.wrong_type", std::string("expected an object, found ") + typeName(*it) + ".");
    } else {
        auto minV = ck.version(*it, "/compatibility", "minAppVersion", true);
        auto maxV = ck.version(*it, "/compatibility", "maxAppVersion", false);
        if (minV) m.compatibility.minAppVersion = *minV;
        if (maxV) m.compatibility.maxAppVersion = *maxV;
        if (minV && maxV && *maxV < *minV)
            ck.error("/compatibility/maxAppVersion", "plugins.manifest.bad_range",
                     "maxAppVersion " + maxV->toString() + " is lower than minAppVersion " + minV->toString() + ".");
        ck.unknownKeys(*it, "/compatibility", {"minAppVersion", "maxAppVersion"});
    }

    // permissions
    if (auto it = root.find("permissions"); it == root.end()) {
        ck.error("/permissions", "plugins.manifest.missing_key", "required key \"permissions\" is missing (use [\"data\"]).");
    } else if (!it->is_array()) {
        ck.error("/permissions", "plugins.manifest.wrong_type", std::string("expected an array of strings, found ") + typeName(*it) + ".");
    } else {
        std::set<std::string> seen;
        for (std::size_t i = 0; i < it->size(); ++i) {
            const auto& p = (*it)[i];
            const std::string path = "/permissions/" + std::to_string(i);
            if (!p.is_string()) {
                ck.error(path, "plugins.manifest.wrong_type", std::string("expected a string, found ") + typeName(p) + ".");
                continue;
            }
            const auto name = p.get<std::string>();
            if (!seen.insert(name).second) {
                ck.error(path, "plugins.manifest.duplicate", "permission \"" + name + "\" is listed twice.");
            } else if (name != kDataPermission) {
                ck.error(path, "plugins.manifest.unsupported_permission",
                         "permission \"" + name + "\" is not granted by this version of FurnitureOS; only \"data\" "
                         "(read-only data files) is defined (ADR-0007 phase 1). The plugin cannot be loaded.");
            }
            m.permissions.push_back(name);
        }
        if (it->empty())
            ck.error("/permissions", "plugins.manifest.empty", "at least the permission \"data\" must be listed.");
    }

    // contributes
    if (auto it = root.find("contributes"); it == root.end()) {
        ck.error("/contributes", "plugins.manifest.missing_key", "required key \"contributes\" is missing.");
    } else if (!it->is_object()) {
        ck.error("/contributes", "plugins.manifest.wrong_type", std::string("expected an object, found ") + typeName(*it) + ".");
    } else {
        for (auto c = it->begin(); c != it->end(); ++c) {
            const std::string base = "/contributes/" + c.key();
            const auto kind = extensionKindFromKey(c.key());
            if (!kind) {
                ck.warning(base, "plugins.manifest.unknown_key",
                           "unknown contribution kind \"" + c.key() + "\" is ignored by this version of FurnitureOS.");
                continue;
            }
            if (!isSupported(*kind)) {
                ck.warning(base, "plugins.manifest.unsupported_contribution",
                           std::string("contribution kind \"") + toString(*kind) +
                               "\" is not supported by this version of FurnitureOS and is ignored.");
                m.ignoredContributions.push_back(*kind);
                continue;
            }
            const Json& list = c.value();
            if (!list.is_array()) {
                ck.error(base, "plugins.manifest.wrong_type", std::string("expected an array of relative paths, found ") + typeName(list) + ".");
                continue;
            }
            if (list.size() > limits.maxLibraries) {
                ck.error(base, "plugins.manifest.too_many",
                         std::to_string(list.size()) + " libraries are listed; the limit is " +
                             std::to_string(limits.maxLibraries) + ".");
                continue;
            }
            std::set<std::string> seen;
            for (std::size_t i = 0; i < list.size(); ++i) {
                const std::string path = base + "/" + std::to_string(i);
                if (!list[i].is_string()) {
                    ck.error(path, "plugins.manifest.wrong_type", std::string("expected a string, found ") + typeName(list[i]) + ".");
                    continue;
                }
                const auto rel = list[i].get<std::string>();
                if (auto why = checkRelativePath(rel); !why.empty()) {
                    ck.error(path, "plugins.manifest.bad_path", "\"" + rel + "\" is not allowed: " + why + ".");
                    continue;
                }
                constexpr std::string_view ext = ".foslib";
                if (rel.size() <= ext.size() || rel.compare(rel.size() - ext.size(), ext.size(), ext) != 0) {
                    ck.error(path, "plugins.manifest.bad_path", "\"" + rel + "\" must name a .foslib library file.");
                    continue;
                }
                if (!seen.insert(rel).second) {
                    ck.error(path, "plugins.manifest.duplicate", "\"" + rel + "\" is listed twice.");
                    continue;
                }
                m.libraries.push_back(rel);
            }
        }
    }

    // localizedNames
    if (auto it = root.find("localizedNames"); it != root.end()) {
        if (!it->is_object()) {
            ck.error("/localizedNames", "plugins.manifest.wrong_type", std::string("expected an object, found ") + typeName(*it) + ".");
        } else {
            for (auto e = it->begin(); e != it->end(); ++e) {
                const std::string path = "/localizedNames/" + e.key();
                if (!isLanguageTag(e.key()))
                    ck.error(path, "plugins.manifest.bad_language", "\"" + e.key() + "\" is not a language tag (e.g. \"uk\", \"en-GB\").");
                else if (!e->is_string())
                    ck.error(path, "plugins.manifest.wrong_type", std::string("expected a string, found ") + typeName(*e) + ".");
                else if (e->get<std::string>().find_first_not_of(" \t\r\n") == std::string::npos)
                    ck.error(path, "plugins.manifest.empty", "must not be empty.");
                else
                    m.localizedNames[e.key()] = e->get<std::string>();
            }
        }
    }

    ck.unknownKeys(root, "", {"manifestVersion", "id", "name", "version", "description", "author", "license",
                              "homepage", "compatibility", "permissions", "contributes", "localizedNames"});
    return finish();
}

} // namespace fos::plugins
