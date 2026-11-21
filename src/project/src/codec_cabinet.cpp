// SPDX-License-Identifier: AGPL-3.0-only
// Codecs of parameters, constraints, the section tree, cabinets and free panels.
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

// ------------------------------------------------------------------ contours

namespace {

Json cutoutJson(const furniture::CutoutSpec& c) {
    Json j = Json::object();
    j["kind"] = furniture::toString(c.kind);
    j["x"] = c.x;
    j["y"] = c.y;
    j["length"] = c.length;
    j["width"] = c.width;
    j["radius"] = c.radius;
    return j;
}

core::Result<furniture::CutoutSpec> cutoutFromJson(const Json& json, std::string_view path) {
    using furniture::CutoutKind;
    Reader r(json, path, "a cut-out");
    furniture::CutoutSpec c;
    r.token("kind", c.kind, furnitureEnum<CutoutKind>(), furnitureTokens({CutoutKind::Rectangle, CutoutKind::Circle}), true);
    r.text("x", c.x);
    r.text("y", c.y);
    r.text("length", c.length);
    r.text("width", c.width);
    r.text("radius", c.radius);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return c;
}

Json vertexJson(const furniture::ContourVertexSpec& v) {
    Json j = Json::object();
    j["x"] = v.x;
    j["y"] = v.y;
    j["bulge"] = v.bulge;
    j["bandFrom"] = v.bandFrom ? Json(std::string(formats::enumToToken(*v.bandFrom))) : Json(nullptr);
    return j;
}

core::Result<furniture::ContourVertexSpec> vertexFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "a contour vertex");
    furniture::ContourVertexSpec v;
    r.text("x", v.x, true);
    r.text("y", v.y, true);
    r.number("bulge", v.bulge, Num::Any);
    if (const Json* b = r.find("bandFrom"); b != nullptr && !b->is_null()) {
        domain::EdgeSide side = domain::EdgeSide::L1;
        r.token("bandFrom", side, formatsEnum<domain::EdgeSide>(), formats::allowedTokens<domain::EdgeSide>());
        v.bandFrom = side;
    }
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return v;
}

} // namespace

Json writeContour(const furniture::ContourSpec& c) {
    Json j = Json::object();
    j["kind"] = furniture::toString(c.kind);
    j["radii"] = Json(std::vector<std::string>(c.radii.begin(), c.radii.end()));
    j["arcEdge"] = std::string(formats::enumToToken(c.arcEdge));
    j["arcRise"] = c.arcRise;
    j["arcDirection"] = furniture::toString(c.arcDirection);
    Json vertices = Json::array();
    for (const auto& v : c.vertices) vertices.push_back(vertexJson(v));
    j["vertices"] = std::move(vertices);
    Json cutouts = Json::array();
    for (const auto& k : c.cutouts) cutouts.push_back(cutoutJson(k));
    j["cutouts"] = std::move(cutouts);
    return j;
}

core::Result<furniture::ContourSpec> readContour(const Json& json, std::string_view path) {
    using namespace furniture;
    Reader r(json, path, "a contour");
    ContourSpec c;
    r.token("kind", c.kind, furnitureEnum<ContourKind>(),
            furnitureTokens({ContourKind::Rectangle, ContourKind::RoundedCorners, ContourKind::ArcEdge, ContourKind::Custom}),
            true);
    if (const Json* radii = r.find("radii"); radii != nullptr) {
        if (!radii->is_array() || radii->size() != 4)
            r.failAt("radii", "expected an array of 4 expression strings, got " + describe(*radii));
        else
            for (std::size_t k = 0; k < 4 && r.ok(); ++k) {
                if (!(*radii)[k].is_string())
                    r.fail(schemaError(joinPath(r.at("radii"), k), "expected an expression string, got " + describe((*radii)[k])));
                else c.radii[k] = (*radii)[k].get<std::string>();
            }
    }
    r.token("arcEdge", c.arcEdge, formatsEnum<domain::EdgeSide>(), formats::allowedTokens<domain::EdgeSide>());
    r.text("arcRise", c.arcRise);
    r.token("arcDirection", c.arcDirection, furnitureEnum<ArcDirection>(),
            furnitureTokens({ArcDirection::Convex, ArcDirection::Concave}));
    r.limitArray("vertices", limits::kMaxContourVertices, "contour vertices");
    r.array("vertices", c.vertices, vertexFromJson);
    r.limitArray("cutouts", limits::kMaxContourCutouts, "cut-outs");
    r.array("cutouts", c.cutouts, cutoutFromJson);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    if (auto check = checkContourSpec(c, "the contour"); !check) return schemaError(path, check.error().message);
    return c;
}

} // namespace detail

Json toJson(const furniture::ContourSpec& contour) { return writeContour(contour); }
core::Result<furniture::ContourSpec> contourFromJson(const Json& json, std::string_view path) {
    return readContour(json, path);
}

// ------------------------------------------------------------------ sections

namespace {

Json sizeJson(const furniture::SectionSize& s) {
    Json j = Json::object();
    j["kind"] = furniture::toString(s.kind);
    j["value"] = s.value;
    j["expression"] = s.expression;
    return j;
}

core::Result<furniture::SectionSize> sizeFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "a section size");
    furniture::SectionSize s;
    r.token("kind", s.kind, furnitureEnum<furniture::SizeKind>(),
            furnitureTokens({furniture::SizeKind::Fixed, furniture::SizeKind::Weight, furniture::SizeKind::Expression}),
            true);
    r.number("value", s.value, Num::Any);
    r.text("expression", s.expression);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    if (s.kind != furniture::SizeKind::Expression && !(s.value > 0))
        return schemaError(joinPath(path, "value"), "expected a positive number for a Fixed or Weight size, got " +
                                                        describe(json["value"]));
    if (s.kind == furniture::SizeKind::Expression && s.expression.empty())
        return schemaError(joinPath(path, "expression"), "an Expression size needs a non-empty expression");
    return s;
}

/// "joint" is written only when it is not the default Dowel, so files written before box
/// joints existed stay byte-identical.
Json drawerBoxJson(const furniture::DrawerBoxSpec& b) {
    Json box = Json::object();
    box["slide"] = b.slide.str();
    box["bottom"] = furniture::toString(b.bottom);
    if (b.joint != furniture::JointFastener::Dowel) box["joint"] = furniture::toString(b.joint);
    return box;
}

Json frontJson(const furniture::FrontSpec& f) {
    Json j = Json::object();
    j["kind"] = furniture::toString(f.kind);
    j["overlay"] = furniture::toString(f.overlay);
    j["hingeSide"] = furniture::toString(f.hingeSide);
    j["hinges"] = f.hinges;
    j["handle"] = furniture::toString(f.handle);
    Json heights = Json::array();
    for (const auto& h : f.drawerHeights) heights.push_back(sizeJson(h));
    j["drawerHeights"] = std::move(heights);
    if (f.drawerBox) // absent = no drawer boxes (files written before drawer boxes stay byte-identical)
        j["drawerBox"] = drawerBoxJson(*f.drawerBox);
    if (!f.drawerBoxes.empty()) { // absent = every drawer uses drawerBox; null entry = no box
        Json boxes = Json::array();
        for (const auto& b : f.drawerBoxes) boxes.push_back(b ? drawerBoxJson(*b) : Json());
        j["drawerBoxes"] = std::move(boxes);
    }
    return j;
}

core::Result<furniture::DrawerBoxSpec> drawerBoxFromJson(const Json& json, std::string_view path) {
    using namespace furniture;
    Reader r(json, path, "a drawer box");
    DrawerBoxSpec b;
    std::string slide;
    r.text("slide", slide, true, true);
    r.token("bottom", b.bottom, furnitureEnum<DrawerBottomMount>(),
            furnitureTokens({DrawerBottomMount::Grooved, DrawerBottomMount::NailedUnder}));
    r.token("joint", b.joint, furnitureEnum<JointFastener>(),
            furnitureTokens({JointFastener::None, JointFastener::Confirmat, JointFastener::Dowel}));
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    b.slide = domain::HardwareId(slide);
    return b;
}

/// Entry of "drawerBoxes": null = no box behind that drawer, else a drawer box object.
core::Result<std::optional<furniture::DrawerBoxSpec>> drawerBoxEntryFromJson(const Json& json, std::string_view path) {
    if (json.is_null()) return std::optional<furniture::DrawerBoxSpec>{};
    auto b = drawerBoxFromJson(json, path);
    if (!b) return b.error();
    return std::optional<furniture::DrawerBoxSpec>{std::move(b.value())};
}

core::Result<furniture::FrontSpec> frontFromJson(const Json& json, std::string_view path) {
    using namespace furniture;
    Reader r(json, path, "a front");
    FrontSpec f;
    r.token("kind", f.kind, furnitureEnum<FrontKind>(),
            furnitureTokens({FrontKind::Door, FrontKind::DoorPair, FrontKind::DrawerStack}), true);
    r.token("overlay", f.overlay, furnitureEnum<OverlayKind>(),
            furnitureTokens({OverlayKind::Full, OverlayKind::Half, OverlayKind::Inset}));
    r.token("hingeSide", f.hingeSide, furnitureEnum<HingeSide>(), furnitureTokens({HingeSide::Left, HingeSide::Right}));
    r.boolean("hinges", f.hinges);
    r.token("handle", f.handle, furnitureEnum<HandlePosition>(),
            furnitureTokens({HandlePosition::None, HandlePosition::Center, HandlePosition::Top, HandlePosition::Bottom}));
    r.limitArray("drawerHeights", furniture::limits::kMaxDrawerHeights, "drawer heights");
    r.array("drawerHeights", f.drawerHeights, sizeFromJson);
    r.nested("drawerBox", f.drawerBox, drawerBoxFromJson);
    r.limitArray("drawerBoxes", furniture::limits::kMaxDrawerHeights, "per-drawer boxes");
    r.array("drawerBoxes", f.drawerBoxes, drawerBoxEntryFromJson);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return f;
}

} // namespace

Json toJson(const furniture::Section& s) {
    Json j = Json::object();
    j["size"] = sizeJson(s.size);
    j["split"] = furniture::toString(s.split);
    j["shelfCount"] = s.shelfCount;
    if (s.front) j["front"] = frontJson(*s.front);
    Json children = Json::array();
    for (const auto& c : s.children) children.push_back(toJson(c));
    j["children"] = std::move(children);
    return j;
}

core::Result<furniture::Section> sectionFromJson(const Json& json, std::string_view path) {
    using namespace furniture;
    Reader r(json, path, "a section");
    Section s;
    r.nested("size", s.size, sizeFromJson);
    r.token("split", s.split, furnitureEnum<SplitDirection>(),
            furnitureTokens({SplitDirection::None, SplitDirection::Vertical, SplitDirection::Horizontal}));
    r.integerInRange("shelfCount", s.shelfCount, 0, furniture::limits::kMaxShelvesPerSection);
    if (const Json* f = r.find("front"); f != nullptr && !f->is_null()) r.nested("front", s.front, frontFromJson);
    r.limitArray("children", furniture::limits::kMaxSectionChildren, "child sections");
    r.array("children", s.children, sectionFromJson);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return s;
}

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
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return h;
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
