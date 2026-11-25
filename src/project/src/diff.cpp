// SPDX-License-Identifier: AGPL-3.0-only
// Project-level diff: generic comparison of the codec JSON, grouped by meaning.
#include "fos/project/diff.hpp"

#include "diff_internal.hpp"
#include "fos/project/codec.hpp"
#include "fos/project/resolve.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace fos::project {
namespace {

using formats::Json;

/// Key used to match the elements of two arrays: "id" when every element of both arrays is an
/// object with a string "id" and the values are unique on each side, else "name" likewise,
/// else "" (match by index).
std::string arrayKey(const Json& a, const Json& b) {
    for (const char* key : {"id", "name"}) {
        bool ok = !(a.empty() && b.empty());
        for (const Json* arr : {&a, &b}) {
            std::set<std::string> seen;
            for (const auto& e : *arr) {
                if (!e.is_object() || !e.contains(key) || !e[key].is_string() || !seen.insert(e[key].get<std::string>()).second) {
                    ok = false;
                    break;
                }
            }
            if (!ok) break;
        }
        if (ok) return key;
    }
    return "";
}

const Json* findByKey(const Json& arr, const std::string& key, const std::string& value) {
    for (const auto& e : arr)
        if (e[key].get<std::string>() == value) return &e;
    return nullptr;
}

bool numbersEqual(const Json& a, const Json& b, double tolerance) {
    if (a.is_number_integer() && b.is_number_integer() && !a.is_number_float() && !b.is_number_float())
        return a == b;
    const double x = a.get<double>(), y = b.get<double>();
    if (x == y) return true;
    return std::isfinite(x) && std::isfinite(y) && std::abs(x - y) <= tolerance;
}

/// Object keys of `a` in order, then keys only in `b` in order.
std::vector<std::string> unionKeys(const Json& a, const Json& b) {
    std::vector<std::string> keys;
    if (a.is_object())
        for (auto it = a.begin(); it != a.end(); ++it) keys.push_back(it.key());
    if (b.is_object())
        for (auto it = b.begin(); it != b.end(); ++it)
            if (!a.is_object() || !a.contains(it.key())) keys.push_back(it.key());
    return keys;
}

std::optional<Json> member(const Json& obj, const std::string& key) {
    if (obj.is_object() && obj.contains(key)) return obj[key];
    return std::nullopt;
}

} // namespace

namespace detail {

void diffJson(const Json& a, const Json& b, const std::string& path, double tolerance, std::vector<ValueChange>& out) {
    if (a.is_number() && b.is_number()) {
        if (!numbersEqual(a, b, tolerance)) out.push_back({path, a, b});
        return;
    }
    if (a.is_object() && b.is_object()) {
        for (const auto& key : unionKeys(a, b)) {
            const std::string sub = path + "/" + key;
            const bool inA = a.contains(key), inB = b.contains(key);
            if (inA && inB) diffJson(a[key], b[key], sub, tolerance, out);
            else if (inA) out.push_back({sub, a[key], std::nullopt});
            else out.push_back({sub, std::nullopt, b[key]});
        }
        return;
    }
    if (a.is_array() && b.is_array()) {
        const std::string key = arrayKey(a, b);
        if (key.empty()) {
            const std::size_t n = std::max(a.size(), b.size());
            for (std::size_t i = 0; i < n; ++i) {
                const std::string sub = path + "[" + std::to_string(i) + "]";
                if (i < a.size() && i < b.size()) diffJson(a[i], b[i], sub, tolerance, out);
                else if (i < a.size()) out.push_back({sub, a[i], std::nullopt});
                else out.push_back({sub, std::nullopt, b[i]});
            }
            return;
        }
        for (const auto& e : a) {
            const std::string id = e[key].get<std::string>();
            const std::string sub = path + "[" + id + "]";
            if (const Json* other = findByKey(b, key, id)) diffJson(e, *other, sub, tolerance, out);
            else out.push_back({sub, e, std::nullopt});
        }
        for (const auto& e : b) {
            const std::string id = e[key].get<std::string>();
            if (findByKey(a, key, id) == nullptr) out.push_back({path + "[" + id + "]", std::nullopt, e});
        }
        return;
    }
    if (a != b) out.push_back({path, a, b});
}

} // namespace detail

namespace {

using detail::diffJson;

std::string parameterBase(const Json& def) {
    if (def.contains("expression")) return "= " + def["expression"].get<std::string>();
    if (def.contains("value")) return detail::valueText(def["value"]);
    return "?";
}

/// Display texts of a changed parameter: the value / expression, plus the other changed
/// fields (kind, bounds, integer, description) in parentheses.
ParameterChange parameterChange(const std::string& name, const Json* a, const Json* b, double tolerance) {
    ParameterChange c;
    c.name = name;
    if (a == nullptr || b == nullptr) {
        if (a != nullptr) c.before = parameterBase(*a);
        if (b != nullptr) c.after = parameterBase(*b);
        return c;
    }
    std::vector<ValueChange> fields;
    diffJson(*a, *b, "", tolerance, fields);
    std::string detailA, detailB;
    for (const auto& f : fields) {
        const std::string key = f.path.substr(1);
        if (key == "value" || key == "expression") continue;
        const auto add = [&](std::string& s, const std::optional<Json>& v) {
            s += (s.empty() ? "" : ", ") + key + " " + detail::valueText(v);
        };
        add(detailA, f.before);
        add(detailB, f.after);
    }
    c.before = parameterBase(*a) + (detailA.empty() ? "" : " (" + detailA + ")");
    c.after = parameterBase(*b) + (detailB.empty() ? "" : " (" + detailB + ")");
    return c;
}

std::vector<ParameterChange> diffParameters(const Json& a, const Json& b, double tolerance) {
    std::vector<ParameterChange> out;
    const auto byName = [](const Json& arr) {
        std::map<std::string, const Json*> m;
        if (arr.is_array())
            for (const auto& e : arr)
                if (e.is_object() && e.contains("name") && e["name"].is_string()) m.emplace(e["name"].get<std::string>(), &e);
        return m;
    };
    const auto ma = byName(a), mb = byName(b);
    std::set<std::string> names;
    for (const auto& [n, _] : ma) names.insert(n);
    for (const auto& [n, _] : mb) names.insert(n);
    for (const auto& n : names) { // sorted by name (the codec order)
        const auto ia = ma.find(n), ib = mb.find(n);
        const Json* pa = ia == ma.end() ? nullptr : ia->second;
        const Json* pb = ib == mb.end() ? nullptr : ib->second;
        if (pa != nullptr && pb != nullptr) {
            std::vector<ValueChange> f;
            diffJson(*pa, *pb, "", tolerance, f);
            if (f.empty()) continue;
        }
        out.push_back(parameterChange(n, pa, pb, tolerance));
    }
    return out;
}

bool isFrontPath(const std::string& path) {
    const auto pos = path.find("/front");
    return pos != std::string::npos && (pos + 6 == path.size() || path[pos + 6] == '/');
}

bool differs(const geometry::Vec3& a, const geometry::Vec3& b, double tolerance) {
    return std::abs(a.x - b.x) > tolerance || std::abs(a.y - b.y) > tolerance || std::abs(a.z - b.z) > tolerance;
}

std::optional<ItemChange> diffItem(const Item& ia, const Json& a, const Item& ib, const Json& b, double tolerance) {
    ItemChange c;
    c.id = ib.id();
    c.name = ib.name();
    if (ia.name() != ib.name()) c.renamedFrom = ia.name();
    const std::string typeA = a.value("type", ""), typeB = b.value("type", "");
    if (typeA != typeB) c.typeBefore = typeA;
    const auto& ta = ia.placement();
    const auto& tb = ib.placement();
    PlacementChange pc{differs(ta.origin, tb.origin, tolerance),
                       differs(ta.xAxis, tb.xAxis, tolerance) || differs(ta.yAxis, tb.yAxis, tolerance) ||
                           differs(ta.zAxis, tb.zAxis, tolerance),
                       ta.origin, tb.origin};
    if (pc.moved || pc.rotated) c.placement = pc;

    for (const auto& key : unionKeys(a, b)) {
        if (key == "id" || key == "type" || key == "name" || key == "placement") continue;
        const auto va = member(a, key), vb = member(b, key);
        if (key == "parameters") {
            c.parameters = diffParameters(va.value_or(Json::array()), vb.value_or(Json::array()), tolerance);
            continue;
        }
        std::vector<ValueChange> changes;
        const bool relative = key == "materials" || key == "hardware" || key == "construction" || key == "interior";
        const std::string base = relative ? "" : "/" + key;
        if (va && vb) diffJson(*va, *vb, base, tolerance, changes);
        else changes.push_back({base.empty() ? "/" : base, va, vb});
        std::vector<ValueChange>* target = &c.other;
        if (key == "materials" || key == "material" || key == "edges") target = &c.materials;
        else if (key == "hardware") target = &c.hardware;
        else if (key == "construction") target = &c.construction;
        for (auto& ch : changes) {
            if (key == "interior") target = isFrontPath(ch.path) ? &c.fronts : &c.interior;
            target->push_back(std::move(ch));
        }
    }
    const bool same = !c.renamedFrom && !c.typeBefore && !c.placement && c.parameters.empty() && c.materials.empty() &&
                      c.hardware.empty() && c.construction.empty() && c.interior.empty() && c.fronts.empty() &&
                      c.other.empty();
    if (same) return std::nullopt;
    return c;
}

ItemRef itemRef(const Item& item) { return {item.id(), item.name(), item.isCabinet() ? "cabinet" : "panel"}; }

void diffItems(const Project& pa, const Json& ja, const Project& pb, const Json& jb, double tolerance, ProjectDiff& d) {
    std::map<std::string, std::size_t> indexB;
    for (std::size_t i = 0; i < pb.items.size(); ++i) indexB.emplace(pb.items[i].id(), i);
    std::map<std::string, std::size_t> indexA;
    for (std::size_t i = 0; i < pa.items.size(); ++i) indexA.emplace(pa.items[i].id(), i);
    std::vector<std::string> commonA, commonB;
    for (std::size_t i = 0; i < pa.items.size(); ++i) {
        const auto& item = pa.items[i];
        if (indexA.at(item.id()) != i) continue; // duplicate id: the first one is compared
        const auto it = indexB.find(item.id());
        if (it == indexB.end()) {
            d.itemsRemoved.push_back(itemRef(item));
            continue;
        }
        commonA.push_back(item.id());
        if (auto c = diffItem(item, ja[i], pb.items[it->second], jb[it->second], tolerance))
            d.itemsChanged.push_back(std::move(*c));
    }
    for (std::size_t i = 0; i < pb.items.size(); ++i) {
        const auto& item = pb.items[i];
        if (indexB.at(item.id()) != i) continue;
        if (indexA.count(item.id()) == 0) d.itemsAdded.push_back(itemRef(item));
        else commonB.push_back(item.id());
    }
    d.itemOrderChanged = commonA != commonB;
}

void diffLibraries(const Json& la, const Json& lb, double tolerance, ProjectDiff& d) {
    for (const char* cat : {"materials", "edgeBands", "hardware"}) {
        LibraryCategoryDiff c;
        c.category = cat;
        const Json a = la.value(cat, Json::array()), b = lb.value(cat, Json::array());
        const auto idOf = [](const Json& e) { return e.value("id", std::string()); };
        const auto find = [&](const Json& arr, const std::string& id) -> const Json* {
            for (const auto& e : arr)
                if (idOf(e) == id) return &e;
            return nullptr;
        };
        std::set<std::string> seen;
        for (const auto& e : a) {
            const std::string id = idOf(e);
            if (!seen.insert(id).second) continue;
            const Json* other = find(b, id);
            if (other == nullptr) {
                c.removed.push_back(id);
                continue;
            }
            LibraryEntryChange ch{id, {}};
            diffJson(e, *other, "", tolerance, ch.fields);
            if (!ch.fields.empty()) c.changed.push_back(std::move(ch));
        }
        seen.clear();
        for (const auto& e : b) {
            const std::string id = idOf(e);
            if (seen.insert(id).second && find(a, id) == nullptr) c.added.push_back(id);
        }
        if (!c.empty()) d.libraries.push_back(std::move(c));
    }
}

void diffSettings(const Json& pa, const Json& pb, double tolerance, ProjectDiff& d) {
    const auto add = [&](const std::string& group, const Json& a, const Json& b) {
        SettingsDiff s{group, {}};
        diffJson(a, b, "", tolerance, s.changes);
        if (!s.changes.empty()) d.settings.push_back(std::move(s));
    };
    add("cutting", pa.value("cutting", Json::object()), pb.value("cutting", Json::object()));
    Json sa = pa.value("settings", Json::object()), sb = pb.value("settings", Json::object());
    for (const char* group : {"pricing", "room"}) {
        const auto va = member(sa, group), vb = member(sb, group);
        if (va || vb) {
            SettingsDiff s{group, {}};
            if (va && vb) diffJson(*va, *vb, "", tolerance, s.changes);
            else s.changes.push_back({"/", va, vb}); // configured on one side only
            if (!s.changes.empty()) d.settings.push_back(std::move(s));
        }
        sa.erase(group);
        sb.erase(group);
    }
    add("settings", sa, sb);
}

} // namespace

bool ModelDiff::empty() const {
    return added.empty() && removed.empty() && changed.empty() && hardware.empty() && notGeneratedBefore.empty() &&
           notGeneratedAfter.empty();
}

bool ProjectDiff::empty() const {
    return metadata.empty() && libraries.empty() && itemsAdded.empty() && itemsRemoved.empty() &&
           itemsChanged.empty() && !itemOrderChanged && settings.empty() && other.empty() && (!model || model->empty());
}

core::Result<ProjectDiff> diffProjects(const Project& before, const Project& after, const DiffOptions& options) {
    const double tol = options.tolerance;
    const Json pa = projectToJson(before), pb = projectToJson(after);
    ProjectDiff d;

    Json ma = pa.value("project", Json::object()), mb = pb.value("project", Json::object());
    if (options.ignoreTimestamps)
        for (const char* k : {"createdAt", "modifiedAt"}) {
            ma.erase(k);
            mb.erase(k);
        }
    diffJson(ma, mb, "", tol, d.metadata);
    diffLibraries(pa.value("libraries", Json::object()), pb.value("libraries", Json::object()), tol, d);
    diffItems(before, pa.value("items", Json::array()), after, pb.value("items", Json::array()), tol, d);
    diffSettings(pa, pb, tol, d);
    for (const auto& key : unionKeys(pa, pb)) {
        if (key == "project" || key == "libraries" || key == "items" || key == "cutting" || key == "settings") continue;
        const auto va = member(pa, key), vb = member(pb, key);
        if (va && vb) diffJson(*va, *vb, "/" + key, tol, d.other);
        else d.other.push_back({"/" + key, va, vb});
    }

    if (options.model) {
        auto ra = resolve(before);
        if (!ra) return core::Error{ra.error().code, "A: " + ra.error().message, ra.error().context};
        auto rb = resolve(after);
        if (!rb) return core::Error{rb.error().code, "B: " + rb.error().message, rb.error().context};
        ModelDiff m = diffModels(ra.value().model, rb.value().model, tol);
        for (const auto& it : ra.value().items)
            if (!it.generated) m.notGeneratedBefore.push_back(it.itemId);
        for (const auto& it : rb.value().items)
            if (!it.generated) m.notGeneratedAfter.push_back(it.itemId);
        d.model = std::move(m);
    }
    return d;
}

} // namespace fos::project
