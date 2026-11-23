// SPDX-License-Identifier: AGPL-3.0-only
// Codecs of part / panel contours (ContourSpec, cut-outs, vertices).
#include "codec_common.hpp"
#include "fos/furniture/contour.hpp"
#include "fos/project/codec.hpp"

namespace fos::project {
using namespace detail;
using formats::Json;

namespace detail {

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

} // namespace fos::project
