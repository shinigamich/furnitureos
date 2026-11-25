// SPDX-License-Identifier: AGPL-3.0-only
// Text and JSON renderings of a ProjectDiff.
#include "diff_internal.hpp"
#include "fos/project/diff.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace fos::project {

namespace detail {

std::string numberText(double value) {
    if (!std::isfinite(value)) return std::isnan(value) ? "nan" : (value > 0 ? "inf" : "-inf");
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.12g", value);
    std::string s = buf;
    return s == "-0" ? "0" : s;
}

std::string valueText(const std::optional<formats::Json>& value) {
    if (!value) return "(none)";
    const formats::Json& v = *value;
    if (v.is_number()) return numberText(v.get<double>());
    std::string s = v.dump();
    if (s.size() > 80) s = s.substr(0, 77) + "...";
    return s;
}

} // namespace detail

namespace {

using detail::valueText;
using formats::Json;

std::string change(const ValueChange& c) {
    return (c.path.empty() ? "/" : c.path) + ": " + valueText(c.before) + " -> " + valueText(c.after);
}

std::string vecText(const geometry::Vec3& v) {
    return "(" + detail::numberText(v.x) + ", " + detail::numberText(v.y) + ", " + detail::numberText(v.z) + ")";
}

std::string sizeText(const PartRef& p) {
    return detail::numberText(p.length) + " x " + detail::numberText(p.width) + " x " + detail::numberText(p.thickness);
}

std::string optText(const std::optional<std::string>& s) { return s ? *s : "(none)"; }

void appendModel(std::ostringstream& o, const ModelDiff& m) {
    o << "Production model (generated parts)\n";
    if (m.empty()) o << "  no differences\n";
    for (const auto& id : m.notGeneratedBefore) o << "  ! item " << id << " could not be generated in A\n";
    for (const auto& id : m.notGeneratedAfter) o << "  ! item " << id << " could not be generated in B\n";
    for (const auto& p : m.added)
        o << "  + part " << p.id << " \"" << p.name << "\" " << sizeText(p) << " mm, " << p.material << "\n";
    for (const auto& p : m.removed)
        o << "  - part " << p.id << " \"" << p.name << "\" " << sizeText(p) << " mm, " << p.material << "\n";
    for (const auto& c : m.changed) {
        o << "  ~ part " << c.after.id << " \"" << c.after.name << "\":";
        std::string sep = " ";
        if (c.sizeChanged) {
            o << sep << "size " << sizeText(c.before) << " -> " << sizeText(c.after) << " mm";
            sep = "; ";
        }
        if (c.materialChanged) {
            o << sep << "material " << c.before.material << " -> " << c.after.material;
            sep = "; ";
        }
        if (c.edgesChanged) {
            o << sep << "edge bands changed";
            sep = "; ";
        }
        if (c.grainChanged) {
            o << sep << "grain changed";
            sep = "; ";
        }
        if (c.operationsBefore != c.operationsAfter)
            o << sep << "operations " << c.operationsBefore << " -> " << c.operationsAfter;
        o << "\n";
    }
    for (const auto& h : m.hardware) o << "  hardware " << h.definition << ": " << h.before << " -> " << h.after << "\n";
}

Json changeJson(const ValueChange& c) {
    Json j = Json::object();
    j["path"] = c.path;
    if (c.before) j["before"] = *c.before;
    if (c.after) j["after"] = *c.after;
    return j;
}

Json changesJson(const std::vector<ValueChange>& list) {
    Json a = Json::array();
    for (const auto& c : list) a.push_back(changeJson(c));
    return a;
}

Json partJson(const PartRef& p) {
    return {{"id", p.id}, {"name", p.name}, {"length", p.length}, {"width", p.width}, {"thickness", p.thickness},
            {"material", p.material}};
}

Json itemRefJson(const ItemRef& r) { return {{"id", r.id}, {"name", r.name}, {"type", r.type}}; }

Json modelJson(const ModelDiff& m) {
    Json j = Json::object();
    j["added"] = Json::array();
    for (const auto& p : m.added) j["added"].push_back(partJson(p));
    j["removed"] = Json::array();
    for (const auto& p : m.removed) j["removed"].push_back(partJson(p));
    j["changed"] = Json::array();
    for (const auto& c : m.changed)
        j["changed"].push_back({{"id", c.after.id},
                                {"before", partJson(c.before)},
                                {"after", partJson(c.after)},
                                {"sizeChanged", c.sizeChanged},
                                {"materialChanged", c.materialChanged},
                                {"edgesChanged", c.edgesChanged},
                                {"grainChanged", c.grainChanged},
                                {"operationsBefore", c.operationsBefore},
                                {"operationsAfter", c.operationsAfter}});
    j["hardware"] = Json::array();
    for (const auto& h : m.hardware)
        j["hardware"].push_back({{"definition", h.definition}, {"before", h.before}, {"after", h.after}});
    j["notGeneratedBefore"] = m.notGeneratedBefore;
    j["notGeneratedAfter"] = m.notGeneratedAfter;
    return j;
}

} // namespace

std::string diffText(const ProjectDiff& d) {
    if (d.empty()) return "No differences.\n";
    std::ostringstream o;
    if (!d.metadata.empty()) {
        o << "Metadata\n";
        for (const auto& c : d.metadata) o << "  " << change(c) << "\n";
    }
    if (!d.libraries.empty()) {
        o << "Libraries\n";
        for (const auto& cat : d.libraries) {
            for (const auto& id : cat.added) o << "  + " << cat.category << " " << id << "\n";
            for (const auto& id : cat.removed) o << "  - " << cat.category << " " << id << "\n";
            for (const auto& e : cat.changed) {
                o << "  ~ " << cat.category << " " << e.id << "\n";
                for (const auto& f : e.fields) o << "      " << change(f) << "\n";
            }
        }
    }
    if (!d.itemsAdded.empty() || !d.itemsRemoved.empty() || !d.itemsChanged.empty() || d.itemOrderChanged) {
        o << "Items\n";
        for (const auto& r : d.itemsAdded) o << "  + " << r.id << " \"" << r.name << "\" (" << r.type << ")\n";
        for (const auto& r : d.itemsRemoved) o << "  - " << r.id << " \"" << r.name << "\" (" << r.type << ")\n";
        for (const auto& c : d.itemsChanged) {
            o << "  ~ " << c.id << " \"" << c.name << "\"\n";
            if (c.renamedFrom) o << "      renamed from \"" << *c.renamedFrom << "\"\n";
            if (c.typeBefore) o << "      type changed from " << *c.typeBefore << "\n";
            if (c.placement) {
                o << "      placement:";
                if (c.placement->moved)
                    o << " moved " << vecText(c.placement->beforeOrigin) << " -> " << vecText(c.placement->afterOrigin);
                if (c.placement->rotated) o << (c.placement->moved ? ";" : "") << " rotated";
                o << "\n";
            }
            for (const auto& p : c.parameters)
                o << "      parameter " << p.name << ": " << optText(p.before) << " -> " << optText(p.after) << "\n";
            const auto group = [&](const char* label, const std::vector<ValueChange>& list) {
                for (const auto& v : list) o << "      " << label << " " << change(v) << "\n";
            };
            group("materials", c.materials);
            group("hardware", c.hardware);
            group("construction", c.construction);
            group("interior", c.interior);
            group("front", c.fronts);
            group("other", c.other);
        }
        if (d.itemOrderChanged) o << "  item order changed\n";
    }
    if (!d.settings.empty()) {
        o << "Settings\n";
        for (const auto& s : d.settings)
            for (const auto& c : s.changes) o << "  " << s.group << " " << change(c) << "\n";
    }
    if (!d.other.empty()) {
        o << "Other\n";
        for (const auto& c : d.other) o << "  " << change(c) << "\n";
    }
    if (d.model) appendModel(o, *d.model);
    return o.str();
}

Json diffToJson(const ProjectDiff& d) {
    Json j = Json::object();
    j["identical"] = d.empty();
    j["metadata"] = changesJson(d.metadata);
    j["libraries"] = Json::array();
    for (const auto& cat : d.libraries) {
        Json c = {{"category", cat.category}, {"added", cat.added}, {"removed", cat.removed}};
        c["changed"] = Json::array();
        for (const auto& e : cat.changed) c["changed"].push_back({{"id", e.id}, {"fields", changesJson(e.fields)}});
        j["libraries"].push_back(std::move(c));
    }
    Json items = Json::object();
    items["added"] = Json::array();
    for (const auto& r : d.itemsAdded) items["added"].push_back(itemRefJson(r));
    items["removed"] = Json::array();
    for (const auto& r : d.itemsRemoved) items["removed"].push_back(itemRefJson(r));
    items["changed"] = Json::array();
    for (const auto& c : d.itemsChanged) {
        Json ic = {{"id", c.id}, {"name", c.name}};
        if (c.renamedFrom) ic["renamedFrom"] = *c.renamedFrom;
        if (c.typeBefore) ic["typeBefore"] = *c.typeBefore;
        if (c.placement) {
            const auto& p = *c.placement;
            ic["placement"] = {{"moved", p.moved},
                               {"rotated", p.rotated},
                               {"beforeOrigin", {p.beforeOrigin.x, p.beforeOrigin.y, p.beforeOrigin.z}},
                               {"afterOrigin", {p.afterOrigin.x, p.afterOrigin.y, p.afterOrigin.z}}};
        }
        ic["parameters"] = Json::array();
        for (const auto& p : c.parameters) {
            Json pj = {{"name", p.name}};
            if (p.before) pj["before"] = *p.before;
            if (p.after) pj["after"] = *p.after;
            ic["parameters"].push_back(std::move(pj));
        }
        ic["materials"] = changesJson(c.materials);
        ic["hardware"] = changesJson(c.hardware);
        ic["construction"] = changesJson(c.construction);
        ic["interior"] = changesJson(c.interior);
        ic["fronts"] = changesJson(c.fronts);
        ic["other"] = changesJson(c.other);
        items["changed"].push_back(std::move(ic));
    }
    items["orderChanged"] = d.itemOrderChanged;
    j["items"] = std::move(items);
    j["settings"] = Json::array();
    for (const auto& s : d.settings) j["settings"].push_back({{"group", s.group}, {"changes", changesJson(s.changes)}});
    j["other"] = changesJson(d.other);
    if (d.model) j["model"] = modelJson(*d.model);
    return j;
}

} // namespace fos::project
