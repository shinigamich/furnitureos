// SPDX-License-Identifier: AGPL-3.0-only
// Codecs of parameters, constraints, edge maps and cabinets (fronts/sections in
// codec_cabinet_sections.cpp, contours in codec_cabinet_contour.cpp).
#include "codec_common.hpp"
#include "fos/furniture/contour.hpp"
#include "fos/furniture/limits.hpp"
#include "fos/project/codec.hpp"

namespace fos::project {
using namespace detail;
using formats::Json;

// ------------------------------------------------------------------ parameters

Json toJson(const parametric::ParameterDef& p) {
    Json j = Json::object();
    j["name"] = p.name;
    j["kind"] = parametric::toString(p.kind);
    if (p.isExpression()) j["expression"] = p.expression;
    else j["value"] = p.value;
    j["description"] = p.description;
    if (p.lowerBound) j["lowerBound"] = *p.lowerBound;
    if (p.upperBound) j["upperBound"] = *p.upperBound;
    j["integer"] = p.integer;
    return j;
}

core::Result<parametric::ParameterDef> parameterFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "a parameter");
    parametric::ParameterDef p;
    r.text("name", p.name, true, true);
    r.token("kind", p.kind, [](std::string_view s) { return parametric::parameterKindFromString(s); },
            "\"Number\", \"Length\", \"Count\", \"Angle\", \"Ratio\", \"Boolean\"");
    const bool hasExpression = r.find("expression") != nullptr;
    const bool hasValue = r.find("value") != nullptr;
    if (r.ok() && hasExpression == hasValue)
        r.fail(schemaError(path, "expected exactly one of \"expression\" (formula) or \"value\" (constant)"));
    if (hasExpression) r.text("expression", p.expression, true, true);
    if (hasValue) r.number("value", p.value, Num::Any, true);
    r.text("description", p.description);
    r.optionalNumber("lowerBound", p.lowerBound, Num::Any);
    r.optionalNumber("upperBound", p.upperBound, Num::Any);
    r.boolean("integer", p.integer);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return p;
}

Json toJson(const parametric::ConstraintDef& c) {
    Json j = Json::object();
    j["name"] = c.name;
    j["expression"] = c.expression;
    j["message"] = c.message;
    j["severity"] = c.severity == parametric::ConstraintSeverity::Error ? "error" : "warning";
    return j;
}

core::Result<parametric::ConstraintDef> constraintFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "a constraint");
    parametric::ConstraintDef c;
    r.text("name", c.name, true, true);
    r.text("expression", c.expression, true, true);
    r.text("message", c.message);
    r.token("severity", c.severity,
            [](std::string_view s) -> std::optional<parametric::ConstraintSeverity> {
                if (s == "error") return parametric::ConstraintSeverity::Error;
                if (s == "warning") return parametric::ConstraintSeverity::Warning;
                return std::nullopt;
            },
            "\"error\", \"warning\"");
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return c;
}

namespace detail {

void writeGraph(Json& j, const parametric::ParameterGraph& graph) {
    Json params = Json::array();
    for (const auto& d : graph.definitions()) params.push_back(toJson(d));
    Json cons = Json::array();
    for (const auto& c : graph.constraints()) cons.push_back(toJson(c));
    j["parameters"] = std::move(params);
    j["constraints"] = std::move(cons);
}

void readGraph(Reader& r, parametric::ParameterGraph& graph) {
    std::vector<parametric::ParameterDef> defs;
    std::vector<parametric::ConstraintDef> cons;
    if (r.find("parameters", true) != nullptr) r.array("parameters", defs, parameterFromJson);
    r.array("constraints", cons, constraintFromJson);
    if (!r.ok()) return;
    auto g = parametric::ParameterGraph::load(defs, cons);
    if (!g) return r.failAt("parameters", "the parameter graph cannot be built: " + g.error().message);
    graph = std::move(g.value());
}

Json writeEdges(const std::array<std::optional<domain::EdgeBandId>, 4>& edges) {
    Json j = Json::object();
    for (const auto side : domain::kAllEdges) {
        const auto& e = edges[static_cast<std::size_t>(side)];
        j[std::string(formats::enumToToken(side))] = e ? Json(e->str()) : Json(nullptr);
    }
    return j;
}

core::Result<std::array<std::optional<domain::EdgeBandId>, 4>> readEdges(const Json& json, std::string_view path) {
    Reader r(json, path, "an edge map");
    std::array<std::optional<domain::EdgeBandId>, 4> out{};
    for (const auto side : domain::kAllEdges) {
        std::optional<std::string> id;
        r.optionalText(formats::enumToToken(side), id, true);
        if (id) out[static_cast<std::size_t>(side)] = domain::EdgeBandId(*id);
    }
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return out;
}

} // namespace detail

// ------------------------------------------------------------------ cabinet

namespace {

Json overrideJson(const furniture::PartOverride& o) {
    Json j = Json::object();
    j["material"] = o.material ? Json(o.material->str()) : Json(nullptr);
    Json edges = Json::object();
    for (const auto& [side, band] : o.edges)
        edges[std::string(formats::enumToToken(side))] = band ? Json(band->str()) : Json(nullptr);
    j["edges"] = std::move(edges);
    if (o.contour) j["contour"] = writeContour(*o.contour); // absent key = rectangle (older files)
    return j;
}

core::Result<furniture::PartOverride> overrideFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "a part override");
    furniture::PartOverride o;
    std::optional<std::string> material;
    r.optionalText("material", material, true);
    if (material) o.material = domain::MaterialId(*material);
    if (const Json* edges = r.find("edges"); edges != nullptr) {
        Reader er(*edges, r.at("edges"), "an edge override map");
        for (const auto side : domain::kAllEdges) {
            const auto token = formats::enumToToken(side);
            const Json* v = er.find(token);
            if (v == nullptr) continue;
            std::optional<std::string> id;
            er.optionalText(token, id, true);
            o.edges[side] = id ? std::optional<domain::EdgeBandId>(domain::EdgeBandId(*id)) : std::nullopt;
        }
        er.rejectUnknown();
        if (!er.ok()) r.fail(er.error());
    }
    if (const Json* c = r.find("contour"); c != nullptr && !c->is_null()) r.nested("contour", o.contour, readContour);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return o;
}

Json materialsJson(const furniture::MaterialAssignment& m) {
    Json j = Json::object();
    j["carcass"] = m.carcass.str();
    j["back"] = m.back.str();
    j["front"] = m.front.str();
    j["visibleEdge"] = m.visibleEdge.str();
    j["hiddenEdge"] = m.hiddenEdge ? Json(m.hiddenEdge->str()) : Json(nullptr);
    j["frontEdge"] = m.frontEdge ? Json(m.frontEdge->str()) : Json(nullptr);
    // Written only when assigned, so files without drawer-box materials stay byte-identical.
    if (!m.drawer.empty()) j["drawer"] = m.drawer.str();
    if (!m.drawerBottom.empty()) j["drawerBottom"] = m.drawerBottom.str();
    Json overrides = Json::object();
    for (const auto& [slot, o] : m.overrides) overrides[slot] = overrideJson(o);
    j["overrides"] = std::move(overrides);
    return j;
}

core::Result<furniture::MaterialAssignment> materialsFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "a material assignment");
    furniture::MaterialAssignment m;
    std::string carcass, back, front, visible;
    std::optional<std::string> hidden, frontEdge, drawer, drawerBottom;
    r.text("carcass", carcass);
    r.text("back", back);
    r.text("front", front);
    r.text("visibleEdge", visible);
    r.optionalText("hiddenEdge", hidden, true);
    r.optionalText("frontEdge", frontEdge, true);
    r.optionalText("drawer", drawer, true);
    r.optionalText("drawerBottom", drawerBottom, true);
    m.carcass = domain::MaterialId(carcass);
    m.back = domain::MaterialId(back);
    m.front = domain::MaterialId(front);
    m.visibleEdge = domain::EdgeBandId(visible);
    if (hidden) m.hiddenEdge = domain::EdgeBandId(*hidden);
    if (frontEdge) m.frontEdge = domain::EdgeBandId(*frontEdge);
    if (drawer) m.drawer = domain::MaterialId(*drawer);
    if (drawerBottom) m.drawerBottom = domain::MaterialId(*drawerBottom);
    if (const Json* ov = r.find("overrides"); ov != nullptr) {
        if (!ov->is_object()) r.failAt("overrides", "expected an object keyed by slot id, got " + describe(*ov));
        else
            for (auto it = ov->begin(); it != ov->end() && r.ok(); ++it) {
                auto o = overrideFromJson(it.value(), joinPath(r.at("overrides"), it.key()));
                if (!o) r.fail(o.error());
                else m.overrides[it.key()] = std::move(o.value());
            }
    }
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return m;
}

Json constructionJson(const furniture::ConstructionOptions& o) {
    using furniture::toString;
    Json j = Json::object();
    j["topJoint"] = toString(o.topJoint);
    j["bottomJoint"] = toString(o.bottomJoint);
    j["top"] = toString(o.top);
    j["back"] = toString(o.back);
    j["plinth"] = toString(o.plinth);
    j["topFastener"] = toString(o.topFastener);
    j["bottomFastener"] = toString(o.bottomFastener);
    j["dividerFastener"] = toString(o.dividerFastener);
    j["shelfSupports"] = o.shelfSupports;
    return j;
}

core::Result<furniture::ConstructionOptions> constructionFromJson(const Json& json, std::string_view path) {
    using namespace furniture;
    Reader r(json, path, "construction options");
    ConstructionOptions o;
    const std::string joints = furnitureTokens({JointKind::BetweenSides, JointKind::OverSides});
    const std::string fasteners = furnitureTokens({JointFastener::None, JointFastener::Confirmat, JointFastener::Dowel});
    r.token("topJoint", o.topJoint, furnitureEnum<JointKind>(), joints);
    r.token("bottomJoint", o.bottomJoint, furnitureEnum<JointKind>(), joints);
    r.token("top", o.top, furnitureEnum<TopKind>(), furnitureTokens({TopKind::Panel, TopKind::Rails}));
    r.token("back", o.back, furnitureEnum<BackMount>(),
            furnitureTokens({BackMount::None, BackMount::Overlay, BackMount::Grooved, BackMount::Inset}));
    r.token("plinth", o.plinth, furnitureEnum<PlinthKind>(),
            furnitureTokens({PlinthKind::None, PlinthKind::SidesToFloor, PlinthKind::Feet}));
    r.token("topFastener", o.topFastener, furnitureEnum<JointFastener>(), fasteners);
    r.token("bottomFastener", o.bottomFastener, furnitureEnum<JointFastener>(), fasteners);
    r.token("dividerFastener", o.dividerFastener, furnitureEnum<JointFastener>(), fasteners);
    r.boolean("shelfSupports", o.shelfSupports);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return o;
}

Json hardwareJson(const furniture::HardwareSelection& h) {
    Json j = Json::object();
    j["hinge"] = h.hinge.str();
    j["handle"] = h.handle.str();
    j["shelfPin"] = h.shelfPin.str();
    j["confirmat"] = h.confirmat.str();
    j["dowel"] = h.dowel.str();
    j["foot"] = h.foot.str();
    // Written only when changed from the default so older files stay byte-identical.
    const furniture::HardwareSelection defaults;
    if (h.hingeHalf != defaults.hingeHalf) j["hingeHalf"] = h.hingeHalf.str();
    if (h.hingeInset != defaults.hingeInset) j["hingeInset"] = h.hingeInset.str();
    if (h.lift != defaults.lift) j["lift"] = h.lift.str();
    if (h.slidingSystem != defaults.slidingSystem) j["slidingSystem"] = h.slidingSystem.str();
    return j;
}

core::Result<furniture::HardwareSelection> hardwareFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "a hardware selection");
    furniture::HardwareSelection h;
    auto id = [&r](const char* key, domain::HardwareId& out) {
        std::string s = out.str();
        r.text(key, s);
        out = domain::HardwareId(s);
    };
    id("hinge", h.hinge);
    id("handle", h.handle);
    id("shelfPin", h.shelfPin);
    id("confirmat", h.confirmat);
    id("dowel", h.dowel);
    id("foot", h.foot);
    id("hingeHalf", h.hingeHalf);
    id("hingeInset", h.hingeInset);
    id("lift", h.lift);
    id("slidingSystem", h.slidingSystem);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return h;
}

/// Corner construction: written only for a corner cabinet (files of ordinary cabinets stay
/// byte-identical); "blindSide" only for a blind corner.
Json cornerJson(const furniture::CornerSpec& c) {
    Json j = Json::object();
    j["kind"] = furniture::toString(c.kind);
    if (c.kind == furniture::CornerKind::Blind) j["blindSide"] = furniture::toString(c.blindSide);
    return j;
}

core::Result<furniture::CornerSpec> cornerFromJson(const Json& json, std::string_view path) {
    using namespace furniture;
    Reader r(json, path, "a corner construction");
    CornerSpec c;
    r.token("kind", c.kind, furnitureEnum<CornerKind>(),
            furnitureTokens({CornerKind::None, CornerKind::Blind, CornerKind::LShape}), true);
    r.token("blindSide", c.blindSide, furnitureEnum<CornerSide>(), furnitureTokens({CornerSide::Left, CornerSide::Right}));
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return c;
}

/// Appliance gap: written only when set (files of ordinary cabinets stay byte-identical).
Json applianceGapJson(const furniture::ApplianceGapSpec& a) {
    Json j = Json::object();
    j["panels"] = furniture::toString(a.panels);
    return j;
}

core::Result<furniture::ApplianceGapSpec> applianceGapFromJson(const Json& json, std::string_view path) {
    using namespace furniture;
    Reader r(json, path, "an appliance gap");
    ApplianceGapSpec a;
    r.token("panels", a.panels, furnitureEnum<GapPanels>(),
            furnitureTokens({GapPanels::None, GapPanels::Left, GapPanels::Right, GapPanels::Both}), true);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return a;
}

} // namespace

Json toJson(const furniture::CabinetDefinition& c) {
    Json j = Json::object();
    j["id"] = c.id.str();
    j["name"] = c.name;
    j["placement"] = formats::toJson(c.placement);
    j["construction"] = constructionJson(c.construction);
    j["materials"] = materialsJson(c.materials);
    j["hardware"] = hardwareJson(c.hardware);
    writeGraph(j, c.parameters);
    j["pinnedParameters"] = Json(std::vector<std::string>(c.pinnedParameters.begin(), c.pinnedParameters.end()));
    j["interior"] = toJson(c.interior);
    if (c.corner != furniture::CornerSpec{}) j["corner"] = cornerJson(c.corner);
    if (c.applianceGap) j["applianceGap"] = applianceGapJson(*c.applianceGap);
    return j;
}

namespace detail {

void readCabinetFields(Reader& r, furniture::CabinetDefinition& c) {
    std::string id;
    r.text("id", id, true, true);
    c.id = domain::AssemblyId(id);
    r.text("name", c.name, true);
    r.nested("placement", c.placement, formats::transformFromJson);
    r.nested("construction", c.construction, constructionFromJson);
    r.nested("materials", c.materials, materialsFromJson);
    r.nested("hardware", c.hardware, hardwareFromJson);
    readGraph(r, c.parameters);
    std::vector<std::string> pinned;
    r.array("pinnedParameters", pinned, [](const Json& v, std::string_view p) -> core::Result<std::string> {
        if (!v.is_string() || v.get_ref<const std::string&>().empty())
            return schemaError(p, "expected a non-empty parameter name, got " + describe(v));
        return v.get<std::string>();
    });
    c.pinnedParameters = std::set<std::string>(pinned.begin(), pinned.end());
    r.nested("interior", c.interior, sectionFromJson, true);
    r.nested("corner", c.corner, cornerFromJson);
    r.nested("applianceGap", c.applianceGap, applianceGapFromJson);
    // Hard limits (furniture/limits.hpp): a file the generator would refuse, or that would make
    // it run for minutes, is rejected on load with the generator's own message.
    if (r.ok())
        if (auto limits = furniture::checkDefinitionLimits(c); !limits) r.fail(schemaError(r.path(), limits.error().message));
}

} // namespace detail

core::Result<furniture::CabinetDefinition> cabinetFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "a cabinet");
    furniture::CabinetDefinition c;
    std::string type = "cabinet";
    r.text("type", type);
    if (r.ok() && type != "cabinet") r.failAt("type", "expected \"cabinet\", got " + describe(json["type"]));
    readCabinetFields(r, c);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return c;
}

} // namespace fos::project