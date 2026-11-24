// SPDX-License-Identifier: AGPL-3.0-only
// Codecs of the section tree and fronts (sizes, drawer boxes, sliding doors, appliance niches).
#include "codec_common.hpp"
#include "fos/furniture/limits.hpp"
#include "fos/project/codec.hpp"

namespace fos::project {
using namespace detail;
using formats::Json;

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
    // Sliding doors: written for that kind (or a non-default spec) only, so older files stay
    // byte-identical; "countExpression" only when set.
    if (f.kind == furniture::FrontKind::SlidingDoors || f.sliding != furniture::SlidingDoorSpec{}) {
        Json s = Json::object();
        s["count"] = f.sliding.count;
        if (!f.sliding.countExpression.empty()) s["countExpression"] = f.sliding.countExpression;
        j["sliding"] = std::move(s);
    }
    // Appliance niche: written for that kind (or a non-default spec) only (older files unchanged).
    if (f.kind == furniture::FrontKind::ApplianceNiche || f.niche != furniture::ApplianceNicheSpec{}) {
        Json n = Json::object();
        n["vent"] = furniture::toString(f.niche.vent);
        n["ventSize"] = f.niche.ventSize;
        n["ventEdgeDistance"] = f.niche.ventEdgeDistance;
        n["ventEndMargin"] = f.niche.ventEndMargin;
        j["niche"] = std::move(n);
    }
    // Front style: written only for FramePanel, so slab fronts (and older files) stay
    // byte-identical; "panelMaterial" only when set.
    if (f.style == furniture::FrontStyle::FramePanel) {
        j["style"] = furniture::toString(f.style);
        Json fr = Json::object();
        fr["stileWidth"] = f.frame.stileWidth;
        fr["railWidth"] = f.frame.railWidth;
        fr["panelThickness"] = f.frame.panelThickness;
        if (f.frame.panelMaterial) fr["panelMaterial"] = f.frame.panelMaterial->str();
        fr["grooveDepth"] = f.frame.grooveDepth;
        fr["panelClearance"] = f.frame.panelClearance;
        fr["joint"] = furniture::toString(f.frame.joint);
        j["frame"] = std::move(fr);
    }
    return j;
}

core::Result<furniture::FramePanelSpec> frameFromJson(const Json& json, std::string_view path) {
    using namespace furniture;
    Reader r(json, path, "a frame-and-panel front");
    FramePanelSpec f;
    std::optional<std::string> material;
    r.number("stileWidth", f.stileWidth, Num::Positive);
    r.number("railWidth", f.railWidth, Num::Positive);
    r.number("panelThickness", f.panelThickness, Num::Positive);
    r.optionalText("panelMaterial", material, true);
    r.number("grooveDepth", f.grooveDepth, Num::Positive);
    r.number("panelClearance", f.panelClearance, Num::NonNegative);
    r.token("joint", f.joint, furnitureEnum<FrameJoint>(), furnitureTokens({FrameJoint::StubTenon, FrameJoint::Dowel}));
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    if (material) f.panelMaterial = domain::MaterialId(*material);
    return f;
}

core::Result<furniture::ApplianceNicheSpec> nicheFromJson(const Json& json, std::string_view path) {
    using namespace furniture;
    Reader r(json, path, "an appliance niche");
    ApplianceNicheSpec n;
    r.token("vent", n.vent, furnitureEnum<NicheVent>(),
            furnitureTokens({NicheVent::None, NicheVent::ShelfSlot, NicheVent::BackCutout}));
    r.number("ventSize", n.ventSize, Num::Positive);
    r.number("ventEdgeDistance", n.ventEdgeDistance, Num::Positive);
    r.number("ventEndMargin", n.ventEndMargin, Num::Positive);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return n;
}

core::Result<furniture::SlidingDoorSpec> slidingFromJson(const Json& json, std::string_view path) {
    Reader r(json, path, "sliding doors");
    furniture::SlidingDoorSpec s;
    r.integerInRange("count", s.count, furniture::kMinSlidingDoors, furniture::kMaxSlidingDoors);
    r.text("countExpression", s.countExpression);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return s;
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
            furnitureTokens({FrontKind::Door, FrontKind::DoorPair, FrontKind::DrawerStack, FrontKind::Flap,
                             FrontKind::SlidingDoors, FrontKind::ApplianceNiche, FrontKind::ApplianceFront}),
            true);
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
    r.nested("sliding", f.sliding, slidingFromJson);
    r.nested("niche", f.niche, nicheFromJson);
    r.token("style", f.style, furnitureEnum<FrontStyle>(), furnitureTokens({FrontStyle::Slab, FrontStyle::FramePanel}));
    r.nested("frame", f.frame, frameFromJson);
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
} // namespace fos::project

