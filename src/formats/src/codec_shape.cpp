// SPDX-License-Identifier: AGPL-3.0-only
// JSON codec of domain::PartShape (structure only; geometry checks live in shape_check.cpp).
#include "codec_reader.hpp"
#include "fos/formats/codec.hpp"
#include "shape_codec.hpp"

namespace fos::formats::detail {
namespace {

Json contourToJson(const geometry::Contour& contour) {
    Json out = Json::array();
    for (const geometry::ContourVertex& v : contour.vertices) {
        Json vertex = Json::object();
        vertex["x"] = v.point.x;
        vertex["y"] = v.point.y;
        if (v.bulge != 0.0) vertex["bulge"] = v.bulge; // omitted when 0 (also for -0.0)
        out.push_back(std::move(vertex));
    }
    return out;
}

core::Result<geometry::ContourVertex> vertexFromJson(const Json& json, std::string_view path) {
    ObjectReader r(json, path, "a contour vertex {\"x\", \"y\"[, \"bulge\"]}");
    geometry::ContourVertex v;
    r.number("x", v.point.x, NumberRule::Any, true);
    r.number("y", v.point.y, NumberRule::Any, true);
    r.number("bulge", v.bulge, NumberRule::Any); // optional, default 0
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return v;
}

/// `what` names the contour in messages ("the outer contour", "cut-out 3").
core::Result<geometry::Contour> contourFromJson(const Json& json, std::string_view path, const std::string& what) {
    if (!json.is_array())
        return schemaError(path, "expected " + what + " as an array of vertices, got " + describe(json));
    if (json.size() > kMaxContourVertices)
        return schemaError(path, what + " has " + std::to_string(json.size()) + " vertices; at most " +
                                     std::to_string(kMaxContourVertices) + " are allowed per contour");
    geometry::Contour contour;
    contour.vertices.reserve(json.size());
    for (std::size_t i = 0; i < json.size(); ++i) {
        auto vertex = vertexFromJson(json[i], joinPath(path, i));
        if (!vertex) return vertex.error();
        contour.vertices.push_back(vertex.value());
    }
    return contour;
}

} // namespace

std::string totalVerticesMessage(std::size_t count) {
    return "the shape has at least " + std::to_string(count) + " vertices in its outer contour and cut-outs; at most " +
           std::to_string(kMaxShapeVertices) + " are allowed per part";
}

Json toJson(const domain::PartShape& shape) {
    Json j = Json::object();
    j["outer"] = contourToJson(shape.outer);
    Json cutouts = Json::array();
    for (const auto& cutout : shape.cutouts) cutouts.push_back(contourToJson(cutout));
    j["cutouts"] = std::move(cutouts);
    Json edges = Json::array();
    for (const auto& band : shape.outerEdges) edges.push_back(band ? Json(band->str()) : Json(nullptr));
    j["outerEdges"] = std::move(edges);
    return j;
}

core::Result<domain::PartShape> shapeFromJson(const Json& json, std::string_view path) {
    ObjectReader r(json, path, "a part shape");
    domain::PartShape shape;
    if (const Json* outer = r.find("outer", true)) {
        auto contour = contourFromJson(*outer, r.childPath("outer"), "the outer contour");
        if (!contour) r.fail(contour.error());
        else shape.outer = std::move(contour.value());
    }
    if (const Json* cutouts = r.find("cutouts")) {
        const std::string base = r.childPath("cutouts");
        if (!cutouts->is_array()) {
            r.failAt("cutouts", "expected an array of cut-out contours, got " + describe(*cutouts));
        } else if (cutouts->size() > kMaxShapeCutouts) {
            r.failAt("cutouts", "the part has " + std::to_string(cutouts->size()) + " cut-outs; at most " +
                                    std::to_string(kMaxShapeCutouts) + " are allowed");
        } else {
            // Total vertex limit over all cut-outs, checked before any cut-out is decoded; the
            // error names the first cut-out that exceeds it.
            std::size_t total = shape.outer.size();
            for (std::size_t k = 0; k < cutouts->size() && r.ok(); ++k) {
                const Json& item = (*cutouts)[k];
                total += item.is_array() ? item.size() : 0;
                if (total > kMaxShapeVertices) r.fail(schemaError(joinPath(base, k), totalVerticesMessage(total)));
            }
            for (std::size_t k = 0; k < cutouts->size() && r.ok(); ++k) {
                auto contour = contourFromJson((*cutouts)[k], joinPath(base, k), "cut-out " + std::to_string(k));
                if (!contour) r.fail(contour.error());
                else shape.cutouts.push_back(std::move(contour.value()));
            }
        }
    }
    if (const Json* edges = r.find("outerEdges")) {
        const std::string base = r.childPath("outerEdges");
        if (!edges->is_array()) {
            r.failAt("outerEdges", "expected an array with one edge band id or null per outer segment, got " +
                                       describe(*edges));
        } else if (edges->size() > kMaxContourVertices) {
            r.failAt("outerEdges", "has " + std::to_string(edges->size()) + " entries; at most " +
                                       std::to_string(kMaxContourVertices) + " (one per outer segment) are allowed");
        } else {
            for (std::size_t i = 0; i < edges->size() && r.ok(); ++i) {
                const Json& value = (*edges)[i];
                if (value.is_null()) {
                    shape.outerEdges.emplace_back(std::nullopt); // raw segment
                    continue;
                }
                auto band = readString(value, joinPath(base, i), true);
                if (!band) r.fail(band.error());
                else shape.outerEdges.emplace_back(domain::EdgeBandId(band.value()));
            }
        }
    }
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return shape;
}

} // namespace fos::formats::detail
