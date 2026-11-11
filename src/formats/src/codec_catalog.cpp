// SPDX-License-Identifier: AGPL-3.0-only
// Codecs of the library-level types: Color, Transform, Material, EdgeBand,
// HardwareDefinition, HardwareInstance.
#include "codec_reader.hpp"
#include "fos/formats/codec.hpp"

#include <climits>

namespace fos::formats {
using namespace detail;

// ---------------------------------------------------------------- Color

Json toJson(const domain::Color& color) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string text = "#";
    for (const std::uint8_t channel : {color.r, color.g, color.b}) {
        text.push_back(kHex[channel >> 4]);
        text.push_back(kHex[channel & 0x0f]);
    }
    return text;
}

core::Result<domain::Color> colorFromJson(const Json& json, std::string_view path) {
    const auto bad = [&] {
        return schemaError(path, "expected a colour string \"#RRGGBB\" (six hexadecimal digits), got " + describe(json));
    };
    if (!json.is_string()) return bad();
    const std::string text = json.get<std::string>();
    if (text.size() != 7 || text[0] != '#') return bad();
    std::uint8_t channels[3] = {0, 0, 0};
    for (std::size_t i = 0; i < 6; ++i) {
        const char c = text[i + 1];
        int digit = 0;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return bad();
        channels[i / 2] = static_cast<std::uint8_t>(channels[i / 2] * 16 + digit);
    }
    return domain::Color{channels[0], channels[1], channels[2]};
}

// ---------------------------------------------------------------- Transform

Json toJson(const geometry::Transform& placement) {
    Json j = Json::object();
    j["origin"] = writeVec3(placement.origin);
    j["xAxis"] = writeVec3(placement.xAxis);
    j["yAxis"] = writeVec3(placement.yAxis);
    j["zAxis"] = writeVec3(placement.zAxis);
    return j;
}

core::Result<geometry::Transform> transformFromJson(const Json& json, std::string_view path) {
    ObjectReader r(json, path, "a placement");
    geometry::Transform t;
    r.nested("origin", t.origin, readVec3, true);
    r.nested("xAxis", t.xAxis, readVec3, true);
    r.nested("yAxis", t.yAxis, readVec3, true);
    r.nested("zAxis", t.zAxis, readVec3, true);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    if (!t.isRigid())
        return schemaError(path, "the axes xAxis " + toJson(t).at("xAxis").dump() + ", yAxis " +
                                     toJson(t).at("yAxis").dump() + ", zAxis " + toJson(t).at("zAxis").dump() +
                                     " are not a proper rotation (they must be unit length, mutually "
                                     "perpendicular and right-handed: zAxis = xAxis x yAxis; mirroring is not allowed)");
    return t;
}

// ---------------------------------------------------------------- Material

Json toJson(const domain::Material& m) {
    Json j = Json::object();
    j["id"] = m.id.str();
    j["name"] = m.name;
    j["manufacturer"] = m.manufacturer;
    j["code"] = m.code;
    j["supplier"] = m.supplier;
    j["kind"] = enumToToken(m.kind);
    j["thickness"] = m.thickness;
    j["sheetLength"] = m.sheetLength;
    j["sheetWidth"] = m.sheetWidth;
    j["hasGrain"] = m.hasGrain;
    j["color"] = toJson(m.color);
    j["texture"] = m.texture;
    j["costPerSheet"] = m.costPerSheet;
    j["available"] = m.available;
    writeCustom(j, m.custom);
    return j;
}

core::Result<domain::Material> materialFromJson(const Json& json, std::string_view path) {
    ObjectReader r(json, path, "a material");
    domain::Material m;
    r.text("id", m.id.value, true, true);
    r.text("name", m.name, true);
    r.text("manufacturer", m.manufacturer);
    r.text("code", m.code);
    r.text("supplier", m.supplier);
    r.enumeration("kind", m.kind);
    r.number("thickness", m.thickness, NumberRule::Positive, true);
    r.number("sheetLength", m.sheetLength, NumberRule::Positive);
    r.number("sheetWidth", m.sheetWidth, NumberRule::Positive);
    r.boolean("hasGrain", m.hasGrain);
    r.nested("color", m.color, colorFromJson);
    r.text("texture", m.texture);
    r.number("costPerSheet", m.costPerSheet, NumberRule::NonNegative);
    r.boolean("available", m.available);
    r.customAndUnknown(m.custom);
    if (!r.ok()) return r.error();
    return m;
}

// ---------------------------------------------------------------- EdgeBand

Json toJson(const domain::EdgeBand& e) {
    Json j = Json::object();
    j["id"] = e.id.str();
    j["name"] = e.name;
    j["manufacturer"] = e.manufacturer;
    j["code"] = e.code;
    j["kind"] = enumToToken(e.kind);
    j["thickness"] = e.thickness;
    j["width"] = e.width;
    j["color"] = toJson(e.color);
    j["costPerMeter"] = e.costPerMeter;
    writeCustom(j, e.custom);
    return j;
}

core::Result<domain::EdgeBand> edgeBandFromJson(const Json& json, std::string_view path) {
    ObjectReader r(json, path, "an edge band");
    domain::EdgeBand e;
    r.text("id", e.id.value, true, true);
    r.text("name", e.name, true);
    r.text("manufacturer", e.manufacturer);
    r.text("code", e.code);
    r.enumeration("kind", e.kind);
    r.number("thickness", e.thickness, NumberRule::Positive, true);
    r.number("width", e.width, NumberRule::Positive, true);
    r.nested("color", e.color, colorFromJson);
    r.number("costPerMeter", e.costPerMeter, NumberRule::NonNegative);
    r.customAndUnknown(e.custom);
    if (!r.ok()) return r.error();
    return e;
}

// ---------------------------------------------------------------- HardwareDefinition

Json toJson(const domain::HardwareDefinition& d) {
    Json j = Json::object();
    j["id"] = d.id.str();
    j["kind"] = enumToToken(d.kind);
    j["name"] = d.name;
    j["manufacturer"] = d.manufacturer;
    j["article"] = d.article;
    j["dimensionsVerified"] = d.dimensionsVerified;
    j["unitCost"] = d.unitCost;
    Json params = Json::object();
    for (const auto& [key, value] : d.params) params[key] = value;
    j["params"] = std::move(params);
    writeCustom(j, d.custom);
    return j;
}

core::Result<domain::HardwareDefinition> hardwareDefinitionFromJson(const Json& json, std::string_view path) {
    ObjectReader r(json, path, "a hardware definition");
    domain::HardwareDefinition d;
    r.text("id", d.id.value, true, true);
    r.enumeration("kind", d.kind, true);
    r.text("name", d.name, true);
    r.text("manufacturer", d.manufacturer);
    r.text("article", d.article);
    r.boolean("dimensionsVerified", d.dimensionsVerified);
    r.number("unitCost", d.unitCost, NumberRule::NonNegative);
    if (const Json* params = r.find("params")) {
        if (!params->is_object()) {
            r.failAt("params", "expected an object of numbers, got " + describe(*params));
        } else {
            const std::string base = r.childPath("params");
            for (const auto& [key, value] : params->items()) {
                auto number = readNumber(value, joinPath(base, key), NumberRule::Any);
                if (!number) {
                    r.fail(number.error());
                    break;
                }
                d.params[key] = number.value();
            }
        }
    }
    r.customAndUnknown(d.custom);
    if (!r.ok()) return r.error();
    return d;
}

// ---------------------------------------------------------------- HardwareInstance

Json toJson(const domain::HardwareInstance& h) {
    Json j = Json::object();
    j["id"] = h.id.str();
    j["definition"] = h.definition.str();
    j["quantity"] = h.quantity;
    Json parts = Json::array();
    for (const auto& part : h.parts) parts.push_back(part.str());
    j["parts"] = std::move(parts);
    j["position"] = writeVec3(h.position);
    j["note"] = h.note;
    return j;
}

core::Result<domain::HardwareInstance> hardwareInstanceFromJson(const Json& json, std::string_view path) {
    ObjectReader r(json, path, "a hardware instance");
    domain::HardwareInstance h;
    r.text("id", h.id.value, true, true);
    r.text("definition", h.definition.value, true, true);
    if (const Json* quantity = r.find("quantity")) {
        const bool valid = quantity->is_number_integer() &&
                           (quantity->is_number_unsigned() ? quantity->get<std::uint64_t>() <= INT_MAX
                                                           : quantity->get<std::int64_t>() <= INT_MAX) &&
                           quantity->get<double>() >= 1.0;
        if (valid) h.quantity = quantity->get<int>();
        else r.failAt("quantity", "expected a whole number >= 1, got " + describe(*quantity));
    }
    r.array("parts", h.parts, [](const Json& value, std::string_view p) -> core::Result<domain::PartId> {
        auto text = readString(value, p, true);
        if (!text) return text.error();
        return domain::PartId(text.value());
    });
    r.nested("position", h.position, readVec3);
    r.text("note", h.note);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return h;
}

} // namespace fos::formats
