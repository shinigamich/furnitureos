// SPDX-License-Identifier: AGPL-3.0-only
// Codecs of the model-level types: Operation, Part, Assembly, Model.
#include "codec_reader.hpp"
#include "fos/formats/codec.hpp"

namespace fos::formats {
using namespace detail;

// ---------------------------------------------------------------- Operation

Json toJson(const domain::Operation& op) {
    Json j = Json::object();
    j["id"] = op.id.str();
    struct Writer {
        Json& j;
        void operator()(const domain::Hole& h) const {
            j["type"] = "hole";
            j["face"] = enumToToken(h.face);
            j["u"] = h.u;
            j["v"] = h.v;
            j["diameter"] = h.diameter;
            j["depth"] = h.depth;
            j["through"] = h.through;
        }
        void operator()(const domain::Groove& g) const {
            j["type"] = "groove";
            j["face"] = enumToToken(g.face);
            j["u1"] = g.u1;
            j["v1"] = g.v1;
            j["u2"] = g.u2;
            j["v2"] = g.v2;
            j["width"] = g.width;
            j["depth"] = g.depth;
        }
        void operator()(const domain::Pocket& p) const {
            j["type"] = "pocket";
            j["face"] = enumToToken(p.face);
            Json contour = Json::array();
            for (const auto& point : p.contour) contour.push_back(writeVec2(point));
            j["contour"] = std::move(contour);
            j["depth"] = p.depth;
            j["through"] = p.through;
        }
    };
    std::visit(Writer{j}, op.shape);
    j["source"] = op.source;
    j["note"] = op.note;
    return j;
}

core::Result<domain::Operation> operationFromJson(const Json& json, std::string_view path) {
    ObjectReader r(json, path, "an operation");
    domain::Operation op;
    r.text("id", op.id.value, true, true);
    std::string type;
    r.text("type", type, true);
    if (!r.ok()) return r.error();

    if (type == "hole") {
        domain::Hole h;
        r.enumeration("face", h.face, true);
        r.number("u", h.u, NumberRule::Any, true);
        r.number("v", h.v, NumberRule::Any, true);
        r.number("diameter", h.diameter, NumberRule::Positive, true);
        h.depth = 0.0;
        r.number("depth", h.depth, NumberRule::NonNegative);
        r.boolean("through", h.through);
        op.shape = h;
    } else if (type == "groove") {
        domain::Groove g;
        r.enumeration("face", g.face, true);
        r.number("u1", g.u1, NumberRule::Any, true);
        r.number("v1", g.v1, NumberRule::Any, true);
        r.number("u2", g.u2, NumberRule::Any, true);
        r.number("v2", g.v2, NumberRule::Any, true);
        r.number("width", g.width, NumberRule::Positive, true);
        r.number("depth", g.depth, NumberRule::Positive, true);
        op.shape = g;
    } else if (type == "pocket") {
        domain::Pocket p;
        r.enumeration("face", p.face, true);
        r.array("contour", p.contour, readVec2);
        if (r.ok() && p.contour.size() < 3)
            r.failAt("contour", "expected at least 3 points [u, v], got " + std::to_string(p.contour.size()));
        r.number("depth", p.depth, NumberRule::NonNegative);
        r.boolean("through", p.through);
        op.shape = std::move(p);
    } else {
        return schemaError(r.childPath("type"),
                           "expected one of \"hole\", \"groove\", \"pocket\", got " + describe(json["type"]));
    }
    r.text("source", op.source);
    r.text("note", op.note);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return op;
}

// ---------------------------------------------------------------- Part

Json toJson(const domain::Part& p) {
    Json j = Json::object();
    j["id"] = p.id.str();
    j["name"] = p.name;
    j["role"] = enumToToken(p.role);
    j["length"] = p.length;
    j["width"] = p.width;
    j["thickness"] = p.thickness;
    j["material"] = p.material.str();
    Json edges = Json::object();
    for (const domain::EdgeSide side : domain::kAllEdges)
        if (const auto& band = p.edge(side)) edges[std::string(enumToToken(side))] = band->str();
    j["edges"] = std::move(edges);
    j["grain"] = enumToToken(p.grain);
    j["placement"] = toJson(p.placement);
    Json operations = Json::array();
    for (const auto& op : p.operations) operations.push_back(toJson(op));
    j["operations"] = std::move(operations);
    writeCustom(j, p.custom);
    return j;
}

core::Result<domain::Part> partFromJson(const Json& json, std::string_view path) {
    ObjectReader r(json, path, "a part");
    domain::Part p;
    r.text("id", p.id.value, true, true);
    r.text("name", p.name, true);
    r.enumeration("role", p.role);
    r.number("length", p.length, NumberRule::Positive, true);
    r.number("width", p.width, NumberRule::Positive, true);
    r.number("thickness", p.thickness, NumberRule::Positive, true);
    r.text("material", p.material.value);
    if (const Json* edges = r.find("edges")) {
        if (!edges->is_object()) {
            r.failAt("edges", "expected an object with the keys " + allowedTokens<domain::EdgeSide>() + ", got " +
                                  describe(*edges));
        } else {
            const std::string base = r.childPath("edges");
            for (const auto& [key, value] : edges->items()) {
                const auto side = enumFromToken<domain::EdgeSide>(key);
                if (!side) {
                    r.fail(schemaError(joinPath(base, key),
                                       "unknown edge (allowed keys here: " + allowedTokens<domain::EdgeSide>() + ")"));
                    break;
                }
                if (value.is_null()) continue; // explicit "raw edge"
                auto band = readString(value, joinPath(base, key), true);
                if (!band) {
                    r.fail(band.error());
                    break;
                }
                p.edge(*side) = domain::EdgeBandId(band.value());
            }
        }
    }
    r.enumeration("grain", p.grain);
    r.nested("placement", p.placement, transformFromJson);
    r.array("operations", p.operations, operationFromJson);
    if (r.ok())
        if (auto duplicate = findDuplicateId(p.operations, r.childPath("operations"))) r.fail(*duplicate);
    r.customAndUnknown(p.custom);
    if (!r.ok()) return r.error();
    return p;
}

// ---------------------------------------------------------------- Assembly

Json toJson(const domain::Assembly& a) {
    Json j = Json::object();
    j["id"] = a.id.str();
    j["name"] = a.name;
    j["placement"] = toJson(a.placement);
    Json parts = Json::array();
    for (const auto& part : a.parts) parts.push_back(toJson(part));
    j["parts"] = std::move(parts);
    Json hardware = Json::array();
    for (const auto& instance : a.hardware) hardware.push_back(toJson(instance));
    j["hardware"] = std::move(hardware);
    return j;
}

core::Result<domain::Assembly> assemblyFromJson(const Json& json, std::string_view path) {
    ObjectReader r(json, path, "an assembly");
    domain::Assembly a;
    r.text("id", a.id.value, true, true);
    r.text("name", a.name, true);
    r.nested("placement", a.placement, transformFromJson);
    r.array("parts", a.parts, partFromJson);
    r.array("hardware", a.hardware, hardwareInstanceFromJson);
    r.rejectUnknown();
    if (r.ok())
        if (auto duplicate = findDuplicateId(a.parts, r.childPath("parts"))) r.fail(*duplicate);
    if (r.ok())
        if (auto duplicate = findDuplicateId(a.hardware, r.childPath("hardware"))) r.fail(*duplicate);
    if (!r.ok()) return r.error();
    return a;
}

// ---------------------------------------------------------------- Model

Json toJson(const domain::Model& m) {
    Json j = Json::object();
    Json materials = Json::array();
    for (const auto& material : m.materials) materials.push_back(toJson(material));
    j["materials"] = std::move(materials);
    Json edgeBands = Json::array();
    for (const auto& band : m.edgeBands) edgeBands.push_back(toJson(band));
    j["edgeBands"] = std::move(edgeBands);
    Json hardware = Json::array();
    for (const auto& definition : m.hardwareCatalog) hardware.push_back(toJson(definition));
    j["hardwareCatalog"] = std::move(hardware);
    Json assemblies = Json::array();
    for (const auto& assembly : m.assemblies) assemblies.push_back(toJson(assembly));
    j["assemblies"] = std::move(assemblies);
    return j;
}

core::Result<domain::Model> modelFromJson(const Json& json, std::string_view path) {
    ObjectReader r(json, path, "a model");
    domain::Model m;
    r.array("materials", m.materials, materialFromJson);
    r.array("edgeBands", m.edgeBands, edgeBandFromJson);
    r.array("hardwareCatalog", m.hardwareCatalog, hardwareDefinitionFromJson);
    r.array("assemblies", m.assemblies, assemblyFromJson);
    r.rejectUnknown();
    if (r.ok())
        if (auto duplicate = findDuplicateId(m.materials, r.childPath("materials"))) r.fail(*duplicate);
    if (r.ok())
        if (auto duplicate = findDuplicateId(m.edgeBands, r.childPath("edgeBands"))) r.fail(*duplicate);
    if (r.ok())
        if (auto duplicate = findDuplicateId(m.hardwareCatalog, r.childPath("hardwareCatalog"))) r.fail(*duplicate);
    if (r.ok())
        if (auto duplicate = findDuplicateId(m.assemblies, r.childPath("assemblies"))) r.fail(*duplicate);
    if (!r.ok()) return r.error();
    return m;
}

core::Result<Json> modelToJsonChecked(const domain::Model& model) {
    Json json = toJson(model);
    if (auto decoded = modelFromJson(json); !decoded) {
        core::Error error = decoded.error();
        error.code = core::ErrorCode::InvalidArgument;
        error.message = "The model cannot be saved because it would not load again: " + error.message;
        return error;
    }
    return json;
}

} // namespace fos::formats
