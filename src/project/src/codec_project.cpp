// SPDX-License-Identifier: AGPL-3.0-only
// Codecs of items, cutting settings, project settings, metadata and the whole payload.
#include "codec_common.hpp"
#include "fos/project/codec.hpp"
#include "fos/project/resolve.hpp"

#include <set>

namespace fos::project {
using namespace detail;
using formats::Json;

namespace {

// Stable file tokens of the cutting enumerations (independent of cutting::toString).
constexpr std::pair<cutting::FirstCut, const char*> kFirstCut[] = {
    {cutting::FirstCut::Free, "free"}, {cutting::FirstCut::Lengthwise, "lengthwise"}, {cutting::FirstCut::Crosswise, "crosswise"}};
constexpr std::pair<cutting::Objective, const char*> kObjective[] = {
    {cutting::Objective::MinimizeSheets, "minimize_sheets"},
    {cutting::Objective::MinimizeWaste, "minimize_waste"},
    {cutting::Objective::PreferRemnants, "prefer_remnants"},
    {cutting::Objective::MinimizeCuts, "minimize_cuts"}};

template <class E, std::size_t N> const char* tokenOf(const std::pair<E, const char*> (&table)[N], E e) {
    for (const auto& [v, t] : table)
        if (v == e) return t;
    return "";
}
template <class E, std::size_t N> auto parserOf(const std::pair<E, const char*> (&table)[N]) {
    return [&table](std::string_view s) -> std::optional<E> {
        for (const auto& [v, t] : table)
            if (s == t) return v;
        return std::nullopt;
    };
}
template <class E, std::size_t N> std::string tokensOf(const std::pair<E, const char*> (&table)[N]) {
    std::string out;
    for (const auto& [v, t] : table) out += (out.empty() ? "\"" : ", \"") + std::string(t) + "\"";
    return out;
}

template <class T> std::optional<core::Error> duplicateId(const std::vector<T>& list, const std::string& base) {
    std::map<std::string, std::size_t> seen;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto [it, inserted] = seen.emplace(list[i].id.str(), i);
        if (!inserted)
            return schemaError(joinPath(joinPath(base, i), "id"), "duplicate id \"" + list[i].id.str() +
                                                                      "\" (already used by " + joinPath(base, it->second) + ")");
    }
    return std::nullopt;
}

core::Result<Remnant> remnantFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "a remnant");
    Remnant m;
    r.text("id", m.id, true, true);
    r.text("label", m.label);
    r.number("length", m.length, Num::Positive, true);
    r.number("width", m.width, Num::Positive, true);
    r.integer("quantity", m.quantity, 1);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return m;
}

core::Result<cutting::Trim> trimFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "an edge trim");
    cutting::Trim t;
    r.number("left", t.left, Num::NonNegative, true);
    r.number("right", t.right, Num::NonNegative, true);
    r.number("top", t.top, Num::NonNegative, true);
    r.number("bottom", t.bottom, Num::NonNegative, true);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return t;
}

Json panelJson(const PanelItem& p) {
    Json j = Json::object();
    j["id"] = p.id.str();
    j["name"] = p.panel.name;
    j["placement"] = formats::toJson(p.placement);
    j["partId"] = p.panel.id.str();
    j["role"] = std::string(formats::enumToToken(p.panel.role));
    j["orientation"] = furniture::toString(p.panel.orientation);
    j["material"] = p.panel.material.str();
    j["edges"] = writeEdges(p.panel.edges);
    writeGraph(j, p.panel.parameters);
    return j;
}

void readPanelFields(Reader& r, PanelItem& p) {
    using furniture::PanelOrientation;
    std::string id, partId, material;
    r.text("id", id, true, true);
    p.id = domain::AssemblyId(id);
    r.text("name", p.panel.name, true);
    r.nested("placement", p.placement, formats::transformFromJson);
    r.text("partId", partId, true, true);
    p.panel.id = domain::PartId(partId);
    r.token("role", p.panel.role, formatsEnum<domain::PartRole>(), formats::allowedTokens<domain::PartRole>());
    r.token("orientation", p.panel.orientation, furnitureEnum<PanelOrientation>(),
            furnitureTokens({PanelOrientation::Horizontal, PanelOrientation::VerticalSide, PanelOrientation::VerticalFront}));
    r.text("material", material, true);
    p.panel.material = domain::MaterialId(material);
    r.nested("edges", p.panel.edges, readEdges);
    readGraph(r, p.panel.parameters);
}

} // namespace

// ------------------------------------------------------------------ items

Json toJson(const Item& item) {
    Json j = Json::object();
    if (const auto* c = item.cabinet()) {
        j["type"] = "cabinet";
        const Json body = toJson(*c);
        for (auto it = body.begin(); it != body.end(); ++it) j[it.key()] = it.value();
    } else {
        j["type"] = "panel";
        const Json body = panelJson(*item.panel());
        for (auto it = body.begin(); it != body.end(); ++it) j[it.key()] = it.value();
    }
    appendExtra(j, item.extra);
    return j;
}

core::Result<Item> itemFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "a furniture item");
    std::string type;
    r.text("type", type, true);
    Item item;
    if (r.ok()) {
        if (type == "cabinet") {
            furniture::CabinetDefinition c;
            readCabinetFields(r, c);
            item.definition = std::move(c);
        } else if (type == "panel") {
            PanelItem p;
            readPanelFields(r, p);
            item.definition = std::move(p);
        } else {
            r.failAt("type", "expected one of \"cabinet\", \"panel\", got " + describe(json["type"]));
        }
    }
    if (!r.ok()) return r.error();
    item.extra = r.unknown();
    return item;
}

// ------------------------------------------------------------------ cutting

Json toJson(const CuttingSettings& s) {
    Json j = Json::object();
    j["kerf"] = s.kerf;
    j["trim"] = {{"left", s.trim.left}, {"right", s.trim.right}, {"top", s.trim.top}, {"bottom", s.trim.bottom}};
    j["minRemnantLength"] = s.minRemnantLength;
    j["minRemnantWidth"] = s.minRemnantWidth;
    j["maxStages"] = s.maxStages;
    j["firstCut"] = tokenOf(kFirstCut, s.firstCut);
    j["objective"] = tokenOf(kObjective, s.objective);
    Json remnants = Json::array();
    for (const auto& m : s.remnants)
        remnants.push_back({{"id", m.id}, {"label", m.label}, {"length", m.length}, {"width", m.width},
                            {"quantity", m.quantity}});
    j["remnants"] = std::move(remnants);
    return j;
}

core::Result<CuttingSettings> cuttingSettingsFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "cutting settings");
    CuttingSettings s;
    r.number("kerf", s.kerf, Num::NonNegative);
    r.nested("trim", s.trim, trimFromJson);
    r.number("minRemnantLength", s.minRemnantLength, Num::NonNegative);
    r.number("minRemnantWidth", s.minRemnantWidth, Num::NonNegative);
    r.integer("maxStages", s.maxStages, 0);
    if (r.ok() && s.maxStages == 1) r.failAt("maxStages", "expected 0 (unlimited) or at least 2, got 1");
    r.token("firstCut", s.firstCut, parserOf(kFirstCut), tokensOf(kFirstCut));
    r.token("objective", s.objective, parserOf(kObjective), tokensOf(kObjective));
    r.array("remnants", s.remnants, remnantFromJson);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    std::set<std::string> seen;
    for (std::size_t i = 0; i < s.remnants.size(); ++i)
        if (!seen.insert(s.remnants[i].id).second)
            return schemaError(joinPath(joinPath(joinPath(path, "remnants"), i), "id"),
                               "duplicate remnant id \"" + s.remnants[i].id + "\"");
    return s;
}

// ------------------------------------------------------------------ payload

namespace {

Json metadataJson(const Metadata& m) {
    Json j = Json::object();
    j["id"] = m.id;
    j["name"] = m.name;
    j["description"] = m.description;
    j["author"] = m.author;
    j["currency"] = m.currency;
    j["units"] = "mm";
    j["createdAt"] = m.createdAt;
    j["modifiedAt"] = m.modifiedAt;
    appendExtra(j, m.extra);
    return j;
}

core::Result<Metadata> metadataFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "project metadata");
    Metadata m;
    std::string units;
    r.text("id", m.id, true, true);
    r.text("name", m.name, true);
    r.text("description", m.description);
    r.text("author", m.author);
    r.text("currency", m.currency);
    r.text("units", units, true);
    if (r.ok() && units != "mm") r.failAt("units", "expected \"mm\" (the only unit of FurnitureOS files), got \"" + units + "\"");
    r.text("createdAt", m.createdAt);
    r.text("modifiedAt", m.modifiedAt);
    if (!r.ok()) return r.error();
    m.extra = r.unknown();
    return m;
}

Json settingsJson(const ProjectSettings& s) {
    Json j = Json::object();
    j["backupCount"] = s.backupCount;
    j["edgeBanding"] = {{"overhangPerEnd", s.edgeBanding.overhangPerEnd}, {"wastePercent", s.edgeBanding.wastePercent}};
    j["cutListGroupAcrossAssemblies"] = s.cutListGroupAcrossAssemblies;
    appendExtra(j, s.extra);
    return j;
}

core::Result<production::EdgeBandingOptions> edgeBandingFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "edge banding options");
    production::EdgeBandingOptions o;
    r.number("overhangPerEnd", o.overhangPerEnd, Num::NonNegative);
    r.number("wastePercent", o.wastePercent, Num::NonNegative);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return o;
}

core::Result<ProjectSettings> settingsFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "project settings");
    ProjectSettings s;
    r.integer("backupCount", s.backupCount, 0);
    r.nested("edgeBanding", s.edgeBanding, edgeBandingFromJson);
    r.boolean("cutListGroupAcrossAssemblies", s.cutListGroupAcrossAssemblies);
    if (!r.ok()) return r.error();
    s.extra = r.unknown();
    return s;
}

core::Result<furniture::Libraries> librariesFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "the libraries");
    furniture::Libraries l;
    r.array("materials", l.materials, formats::materialFromJson);
    r.array("edgeBands", l.edgeBands, formats::edgeBandFromJson);
    r.array("hardware", l.hardware, formats::hardwareDefinitionFromJson);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    if (auto e = duplicateId(l.materials, joinPath(path, "materials"))) return *e;
    if (auto e = duplicateId(l.edgeBands, joinPath(path, "edgeBands"))) return *e;
    if (auto e = duplicateId(l.hardware, joinPath(path, "hardware"))) return *e;
    return l;
}

template <class T, class Decode>
void readKeyedObject(Reader& r, const char* key, std::map<std::string, T>& out, Decode decode, const char* what) {
    const Json* v = r.find(key);
    if (v == nullptr) return;
    if (!v->is_object()) return r.failAt(key, std::string("expected an object keyed by ") + what + ", got " + describe(*v));
    for (auto it = v->begin(); it != v->end() && r.ok(); ++it) {
        if (it.key().empty()) return r.fail(schemaError(r.at(key), std::string("empty ") + what));
        auto one = decode(it.value(), joinPath(r.at(key), it.key()));
        if (!one) return r.fail(one.error());
        out[it.key()] = std::move(one.value());
    }
}

core::Result<ProjectCuttingSettings> projectCuttingFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "the cutting settings");
    ProjectCuttingSettings c;
    r.nested("defaults", c.defaults, cuttingSettingsFromJson);
    readKeyedObject(r, "materials", c.materials, cuttingSettingsFromJson, "material id");
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return c;
}

} // namespace

Json projectToJson(const Project& p) {
    Json j = Json::object();
    j["project"] = metadataJson(p.metadata);
    Json libs = Json::object();
    Json mats = Json::array(), bands = Json::array(), hw = Json::array();
    for (const auto& m : p.libraries.materials) mats.push_back(formats::toJson(m));
    for (const auto& e : p.libraries.edgeBands) bands.push_back(formats::toJson(e));
    for (const auto& h : p.libraries.hardware) hw.push_back(formats::toJson(h));
    libs["materials"] = std::move(mats);
    libs["edgeBands"] = std::move(bands);
    libs["hardware"] = std::move(hw);
    j["libraries"] = std::move(libs);
    Json items = Json::array();
    for (const auto& item : p.items) items.push_back(toJson(item));
    j["items"] = std::move(items);
    Json edits = Json::object();
    for (const auto& [id, e] : p.drawingEdits) edits[id] = toJson(e);
    j["drawingEdits"] = std::move(edits);
    Json cut = Json::object();
    cut["defaults"] = toJson(p.cutting.defaults);
    Json perMaterial = Json::object();
    for (const auto& [id, s] : p.cutting.materials) perMaterial[id] = toJson(s);
    cut["materials"] = std::move(perMaterial);
    j["cutting"] = std::move(cut);
    j["settings"] = settingsJson(p.settings);
    j["custom"] = p.custom.is_object() ? p.custom : Json::object();
    appendExtra(j, p.extra);
    return j;
}

core::Result<Project> projectFromJson(const Json& payload, std::string_view path) {
    Reader r(payload, path, "a project payload");
    Project p;
    r.nested("project", p.metadata, metadataFromJson, true);
    r.nested("libraries", p.libraries, librariesFromJson);
    r.array("items", p.items, itemFromJson);
    readKeyedObject(r, "drawingEdits", p.drawingEdits, drawingEditsFromJson, "drawing id");
    r.nested("cutting", p.cutting, projectCuttingFromJson);
    r.nested("settings", p.settings, settingsFromJson);
    if (const Json* c = r.find("custom"); c != nullptr) {
        if (!c->is_object()) r.failAt("custom", "expected an object, got " + describe(*c));
        else p.custom = *c;
    }
    if (!r.ok()) return r.error();
    p.extra = r.unknown();
    std::map<std::string, std::size_t> seen;
    for (std::size_t i = 0; i < p.items.size(); ++i) {
        const auto [it, inserted] = seen.emplace(p.items[i].id(), i);
        if (!inserted)
            return schemaError(joinPath(joinPath(joinPath(path, "items"), i), "id"),
                               "duplicate item id \"" + p.items[i].id() + "\" (already used by " +
                                   joinPath(joinPath(path, "items"), it->second) + ")");
    }
    if (auto s = validateStructure(p); !s) return s.error();
    return p;
}

} // namespace fos::project
