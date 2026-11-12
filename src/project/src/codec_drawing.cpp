// SPDX-License-Identifier: AGPL-3.0-only
// Codec of drawings::DrawingEdits.
#include "codec_common.hpp"
#include "fos/project/codec.hpp"

namespace fos::project {
using namespace detail;
using formats::Json;

namespace {

Json optText(const std::optional<std::string>& s) { return s ? Json(*s) : Json(nullptr); }

const char* orientationToken(drawings::DimensionOrientation o) {
    return o == drawings::DimensionOrientation::Horizontal ? "horizontal" : "vertical";
}
std::optional<drawings::DimensionOrientation> orientationFromToken(std::string_view s) {
    if (s == "horizontal") return drawings::DimensionOrientation::Horizontal;
    if (s == "vertical") return drawings::DimensionOrientation::Vertical;
    return std::nullopt;
}

core::Result<drawings::ElementEdit> elementFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "an element edit");
    drawings::ElementEdit e;
    r.optionalNumber("offset", e.offset, Num::Any);
    r.vec2("textShift", e.textShift);
    r.boolean("hidden", e.hidden);
    r.optionalText("textOverride", e.textOverride, false);
    r.optionalNumber("measuredAtEdit", e.measuredAtEdit, Num::Any);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return e;
}

void ids(Reader& r, std::string& id, std::string& viewId) {
    r.text("id", id, true, true);
    r.text("viewId", viewId, true, true);
}

Json anchorJson(const std::optional<drawings::ModelAnchor>& a) {
    if (!a) return Json(nullptr);
    Json j = Json::object();
    j["partId"] = a->partId;
    j["corner"] = a->corner ? Json(*a->corner) : Json(nullptr);
    j["localPoint"] = Json::array({a->localPoint.x, a->localPoint.y, a->localPoint.z});
    return j;
}

core::Result<drawings::ModelAnchor> anchorFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "a model anchor");
    drawings::ModelAnchor a;
    r.text("partId", a.partId, true, true);
    if (const Json* c = r.find("corner"); c != nullptr && !c->is_null()) {
        int corner = 0;
        r.integer("corner", corner, -1000000000);
        a.corner = corner;
    }
    if (const Json* lp = r.find("localPoint"); lp != nullptr) {
        const Json& v = *lp;
        if (!v.is_array() || v.size() != 3) {
            r.failAt("localPoint", "expected an array [x, y, z] of three numbers, got " + describe(v));
        } else {
            double c[3] = {0, 0, 0};
            for (std::size_t i = 0; i < 3 && r.ok(); ++i) {
                auto n = readNumber(v[i], joinPath(r.at("localPoint"), i), Num::Any);
                if (!n) r.fail(n.error());
                else c[i] = n.value();
            }
            a.localPoint = {c[0], c[1], c[2]};
        }
    }
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return a;
}

core::Result<drawings::UserText> textFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "a user text");
    drawings::UserText t;
    ids(r, t.id, t.viewId);
    r.vec2("position", t.position, true);
    r.text("text", t.text, true);
    r.number("height", t.height, Num::Positive);
    r.number("rotationDeg", t.rotationDeg, Num::Any);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return t;
}

core::Result<drawings::UserLeader> leaderFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "a user leader");
    drawings::UserLeader l;
    ids(r, l.id, l.viewId);
    r.vec2("target", l.target, true);
    r.vec2("textOffset", l.textOffset);
    r.text("text", l.text, true);
    r.number("height", l.height, Num::Positive);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return l;
}

core::Result<drawings::UserDimension> dimensionFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "a user dimension");
    drawings::UserDimension d;
    ids(r, d.id, d.viewId);
    r.token("orientation", d.orientation, orientationFromToken, "\"horizontal\", \"vertical\"", true);
    r.vec2("p1", d.p1, true);
    r.vec2("p2", d.p2, true);
    r.number("offset", d.offset, Num::Any);
    r.optionalText("textOverride", d.textOverride, false);
    if (const Json* a = r.find("anchor1"); a != nullptr && !a->is_null()) r.nested("anchor1", d.anchor1, anchorFromJson);
    if (const Json* a = r.find("anchor2"); a != nullptr && !a->is_null()) r.nested("anchor2", d.anchor2, anchorFromJson);
    r.optionalNumber("measuredAtEdit", d.measuredAtEdit, Num::Any);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return d;
}

core::Result<drawings::UserDetailMarker> markerFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "a detail marker");
    drawings::UserDetailMarker m;
    ids(r, m.id, m.viewId);
    r.vec2("center", m.center, true);
    r.number("radius", m.radius, Num::Positive);
    r.text("label", m.label);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return m;
}

} // namespace

Json toJson(const drawings::DrawingEdits& edits) {
    Json j = Json::object();
    Json elements = Json::object();
    for (const auto& [id, e] : edits.elements) {
        Json ej = Json::object();
        ej["offset"] = e.offset ? Json(*e.offset) : Json(nullptr);
        ej["textShift"] = writeVec2(e.textShift);
        ej["hidden"] = e.hidden;
        ej["textOverride"] = optText(e.textOverride);
        ej["measuredAtEdit"] = e.measuredAtEdit ? Json(*e.measuredAtEdit) : Json(nullptr);
        elements[id] = std::move(ej);
    }
    j["elements"] = std::move(elements);
    Json texts = Json::array();
    for (const auto& t : edits.texts)
        texts.push_back({{"id", t.id}, {"viewId", t.viewId}, {"position", writeVec2(t.position)}, {"text", t.text},
                         {"height", t.height}, {"rotationDeg", t.rotationDeg}});
    j["texts"] = std::move(texts);
    Json leaders = Json::array();
    for (const auto& l : edits.leaders)
        leaders.push_back({{"id", l.id}, {"viewId", l.viewId}, {"target", writeVec2(l.target)},
                           {"textOffset", writeVec2(l.textOffset)}, {"text", l.text}, {"height", l.height}});
    j["leaders"] = std::move(leaders);
    Json dims = Json::array();
    for (const auto& d : edits.dimensions)
        dims.push_back({{"id", d.id}, {"viewId", d.viewId}, {"orientation", orientationToken(d.orientation)},
                        {"p1", writeVec2(d.p1)}, {"p2", writeVec2(d.p2)}, {"offset", d.offset},
                        {"textOverride", optText(d.textOverride)}, {"anchor1", anchorJson(d.anchor1)},
                        {"anchor2", anchorJson(d.anchor2)},
                        {"measuredAtEdit", d.measuredAtEdit ? Json(*d.measuredAtEdit) : Json(nullptr)}});
    j["dimensions"] = std::move(dims);
    Json markers = Json::array();
    for (const auto& m : edits.detailMarkers)
        markers.push_back({{"id", m.id}, {"viewId", m.viewId}, {"center", writeVec2(m.center)}, {"radius", m.radius},
                           {"label", m.label}});
    j["detailMarkers"] = std::move(markers);
    return j;
}

core::Result<drawings::DrawingEdits> drawingEditsFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "drawing edits");
    drawings::DrawingEdits e;
    if (const Json* el = r.find("elements"); el != nullptr) {
        if (!el->is_object()) r.failAt("elements", "expected an object keyed by element id, got " + describe(*el));
        else
            for (auto it = el->begin(); it != el->end() && r.ok(); ++it) {
                auto one = elementFromJson(it.value(), joinPath(r.at("elements"), it.key()));
                if (!one) r.fail(one.error());
                else e.elements[it.key()] = std::move(one.value());
            }
    }
    r.array("texts", e.texts, textFromJson);
    r.array("leaders", e.leaders, leaderFromJson);
    r.array("dimensions", e.dimensions, dimensionFromJson);
    r.array("detailMarkers", e.detailMarkers, markerFromJson);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return e;
}

} // namespace fos::project
