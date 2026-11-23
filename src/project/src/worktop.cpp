// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/project/worktop.hpp"

#include "cabinet_run.hpp"

#include "fos/furniture/panel.hpp"

#include <algorithm>
#include <cmath>

namespace fos::project {

const char* toString(WorktopCutoutKind kind) noexcept {
    switch (kind) {
    case WorktopCutoutKind::Sink: return "sink";
    case WorktopCutoutKind::Hob: return "hob";
    }
    return "sink";
}

namespace {

using detail::kRunEps;
constexpr double kEps = kRunEps;
constexpr double kTopTolerance = 1.0; ///< Allowed difference of cabinet top heights (mm).

core::Error invalid(std::string message, std::string context = {}) {
    return core::makeError(core::ErrorCode::InvalidArgument, std::move(message), std::move(context));
}

std::string num(double v) { return detail::runNum(v); }
std::string mm(double v) { return detail::runMm(v); }

core::Result<void> checkRange(const char* what, double v, double lo, double hi) {
    if (!std::isfinite(v) || v < lo || v > hi)
        return invalid(std::string("Worktop ") + what + " must be between " + num(lo) + " and " + num(hi) + " mm, got " +
                       (std::isfinite(v) ? num(v) : std::string("a non-finite value")) + ".");
    return {};
}

std::string cutoutLabel(const WorktopCutout& c, std::size_t index) {
    return std::string(toString(c.kind)) + " cut-out #" + std::to_string(index + 1) + " (centre x = " + mm(c.centerX) +
           " mm)";
}

/// Forbidden x ranges (worktop coordinates) for joints, one per cut-out.
struct Zone {
    double lo, hi;
    std::size_t cutout;
};

bool inZone(double x, const std::vector<Zone>& zones) {
    return std::any_of(zones.begin(), zones.end(), [&](const Zone& z) { return x > z.lo + kEps && x < z.hi - kEps; });
}

/// Joint positions (from the left end) for `n` pieces of at most `maxPiece` over `length`,
/// moved out of the cut-out zones. Error when no position exists for a joint.
core::Result<std::vector<double>> layoutJoints(double length, double maxPiece, std::size_t n,
                                               const std::vector<Zone>& zones, const WorktopRequest& request) {
    std::vector<double> joints;
    double previous = 0;
    for (std::size_t k = 1; k < n; ++k) {
        // Every remaining piece must fit: joint >= length - (n - k) * maxPiece; and this piece too.
        const double lo = std::max(previous + kEps, length - static_cast<double>(n - k) * maxPiece);
        const double hi = previous + maxPiece;
        const double ideal = length * static_cast<double>(k) / static_cast<double>(n);
        std::vector<double> candidates{std::clamp(ideal, lo, hi)};
        for (const Zone& z : zones) {
            candidates.push_back(z.lo);
            candidates.push_back(z.hi);
        }
        std::optional<double> best;
        for (double c : candidates) {
            if (c < lo - kEps || c > hi + kEps || c <= previous + kEps || c >= length - kEps || inZone(c, zones)) continue;
            if (!best || std::fabs(c - ideal) < std::fabs(*best - ideal) - kEps) best = c;
        }
        if (!best) {
            std::string why = "Worktop \"" + request.id + "\" is " + mm(length) + " mm long and must be split into " +
                              std::to_string(n) + " pieces of at most " + mm(maxPiece) + " mm, but joint " +
                              std::to_string(k) + " (allowed between " + mm(lo) + " and " + mm(hi) +
                              " mm from the left end) cannot be placed at least " +
                              mm(request.rules.minJointDistance) + " mm (rule of thumb) away from every cut-out";
            for (const Zone& z : zones)
                if (z.hi > lo && z.lo < hi) {
                    why += "; " + cutoutLabel(request.cutouts[z.cutout], z.cutout) + " blocks " + mm(z.lo) + " .. " +
                           mm(z.hi) + " mm";
                }
            return invalid(why + ". Move the cut-out, or give a shorter maximum piece length.");
        }
        joints.push_back(*best);
        previous = *best;
    }
    return joints;
}

/// Cut-out rectangle in worktop coordinates (x from the left end, y from the front edge).
struct Rect {
    double x0, y0, x1, y1;
};

} // namespace

core::Result<WorktopPlan> planWorktop(const Project& project, const WorktopRequest& request) {
    if (request.id.empty()) return invalid("A worktop needs a non-empty item id.");
    if (auto r = checkRange("front overhang", request.frontOverhang, 0, 300); !r) return r.error();
    if (auto r = checkRange("left overhang", request.leftOverhang, 0, 1000); !r) return r.error();
    if (auto r = checkRange("right overhang", request.rightOverhang, 0, 1000); !r) return r.error();
    if (auto r = checkRange("back gap", request.backGap, 0, 100); !r) return r.error();
    for (double v : {request.rules.minEdgeDistance, request.rules.minEndDistance, request.rules.minJointDistance,
                     request.rules.minCutoutGap})
        if (auto r = checkRange("rule distance", v, 0, 1000); !r) return r.error();

    WorktopPlan plan;
    auto runR = detail::analyzeRun(project, request.itemIds, "worktop");
    if (!runR) return runR.error();
    const detail::CabinetRun& run = runR.value();
    const geometry::Transform reference = run.reference;
    const std::vector<detail::RunCabinet>& cabinets = run.cabinets;

    // --- run extent ---
    const double minX = run.box.min.x, maxX = run.box.max.x;
    const double minY = run.box.min.y, maxY = run.box.max.y;
    double topLo = cabinets.front().box.max.z, topHi = topLo;
    std::string topLoId = cabinets.front().id, topHiId = topLoId;
    for (const detail::RunCabinet& c : cabinets) {
        plan.runOrder.push_back(c.id);
        if (c.box.max.z < topLo) topLo = c.box.max.z, topLoId = c.id;
        if (c.box.max.z > topHi) topHi = c.box.max.z, topHiId = c.id;
    }
    for (const detail::RunGap& g : run.gaps)
        plan.notes.push_back({core::Severity::Warning, "project.worktop.gap",
                              "There is a " + mm(g.width) + " mm gap between cabinets \"" + g.leftId + "\" and \"" +
                                  g.rightId + "\"; the worktop bridges it (add a filler or close the gap).",
                              g.rightId});
    if (topHi - topLo > kTopTolerance)
        return invalid("The cabinet tops are at different heights: \"" + topLoId + "\" ends at z = " + mm(topLo) +
                           " mm, \"" + topHiId + "\" at z = " + mm(topHi) +
                           " mm (more than 1 mm apart); level the cabinets before adding a worktop.",
                       topLoId);
    for (const detail::RunCabinet& c : cabinets)
        if (c.box.min.y - minY > detail::kRunFlushTolerance)
            plan.notes.push_back({core::Severity::Info, "project.worktop.front_offset",
                                  "The front of cabinet \"" + c.id + "\" is " + mm(c.box.min.y - minY) +
                                      " mm behind the foremost front; its overhang is larger by that amount.",
                                  c.id});

    // --- material and band ---
    furniture::Libraries libs = project.libraries;
    const domain::Material* material = nullptr;
    const std::string materialId = request.materialId.empty() ? kGenericWorktopMaterial : request.materialId;
    material = libs.findMaterial(domain::MaterialId(materialId));
    if (material == nullptr) {
        if (!request.materialId.empty())
            return core::makeError(core::ErrorCode::NotFound,
                                   "The project library has no material \"" + materialId + "\" for the worktop.",
                                   materialId);
        domain::Material m;
        m.id = domain::MaterialId(kGenericWorktopMaterial);
        m.name = "Generic worktop 38 mm (placeholder)";
        m.kind = domain::MaterialKind::Particleboard;
        m.thickness = 38.0;
        m.sheetLength = 4100.0;
        m.sheetWidth = 650.0;
        m.hasGrain = true;
        plan.addedMaterial = m;
        libs.materials.push_back(m);
        material = &libs.materials.back();
        plan.notes.push_back({core::Severity::Info, "project.worktop.library_added",
                              "Added the generic placeholder material \"" + materialId +
                                  "\" (38 mm, stock 4100 x 650 mm, cost unknown) to the library.",
                              materialId});
    }
    const double thickness = material->thickness;
    if (!std::isfinite(thickness) || thickness <= 0)
        return invalid("Material \"" + materialId + "\" has no valid thickness.", materialId);

    std::optional<domain::EdgeBandId> band;
    const bool anyBand = request.bandFront || request.bandBack || request.bandLeftEnd || request.bandRightEnd;
    if (anyBand) {
        if (!request.frontBandId.empty()) {
            const domain::EdgeBand* b = libs.findEdgeBand(domain::EdgeBandId(request.frontBandId));
            if (b == nullptr)
                return core::makeError(core::ErrorCode::NotFound,
                                       "The project library has no edge band \"" + request.frontBandId + "\".",
                                       request.frontBandId);
            if (b->width + kEps < thickness)
                return invalid("Edge band \"" + request.frontBandId + "\" is " + mm(b->width) +
                                   " mm wide and does not cover the " + mm(thickness) + " mm worktop edge.",
                               request.frontBandId);
            band = b->id;
        } else {
            std::vector<const domain::EdgeBand*> fit;
            for (const auto& b : libs.edgeBands)
                if (b.width + kEps >= thickness) fit.push_back(&b);
            std::sort(fit.begin(), fit.end(), [](auto* a, auto* b) { return a->id < b->id; });
            if (!fit.empty()) {
                band = fit.front()->id;
            } else if (const auto* g = libs.findEdgeBand(domain::EdgeBandId(kGenericWorktopEdgeBand)); g != nullptr) {
                return invalid("Edge band \"" + std::string(kGenericWorktopEdgeBand) + "\" (" + mm(g->width) +
                               " mm) does not cover the " + mm(thickness) + " mm worktop edge; name a wider band.");
            } else {
                domain::EdgeBand e;
                e.id = domain::EdgeBandId(kGenericWorktopEdgeBand);
                e.name = "Generic ABS 2 x 45 (placeholder)";
                e.thickness = 2.0;
                e.width = 45.0;
                if (e.width + kEps < thickness)
                    return invalid("No library edge band covers the " + mm(thickness) +
                                   " mm worktop edge; add a wide enough band and name it.");
                plan.addedEdgeBand = e;
                libs.edgeBands.push_back(e);
                band = e.id;
                plan.notes.push_back({core::Severity::Info, "project.worktop.library_added",
                                      "Added the generic placeholder edge band \"" + std::string(kGenericWorktopEdgeBand) +
                                          "\" (ABS 2 x 45 mm, cost unknown) to the library.",
                                      kGenericWorktopEdgeBand});
            }
        }
    }

    // --- dimensions ---
    plan.thickness = thickness;
    plan.runStart = minX;
    plan.left = minX - request.leftOverhang;
    plan.front = minY - request.frontOverhang;
    plan.top = topHi;
    plan.length = (maxX + request.rightOverhang) - plan.left;
    plan.depth = (maxY - request.backGap) - plan.front;
    if (!(plan.depth > 0))
        return invalid("The worktop depth would be " + mm(plan.depth) + " mm (back gap " + mm(request.backGap) +
                       " mm is larger than the cabinet depth plus front overhang).");

    const CuttingSettings& cut = project.cutting.forMaterial(materialId);
    const double stockLength = material->sheetLength - cut.trim.left - cut.trim.right;
    const double stockWidth = material->sheetWidth - cut.trim.top - cut.trim.bottom;
    if (plan.depth > stockWidth + kEps)
        return invalid("The worktop is " + mm(plan.depth) + " mm deep but material \"" + materialId + "\" gives at most " +
                           mm(stockWidth) + " mm (stock width " + mm(material->sheetWidth) +
                           " mm minus trims); choose wider stock or a smaller front overhang.",
                       materialId);
    plan.usableStockLength = stockLength;
    if (request.maxPieceLength) {
        const double m = *request.maxPieceLength;
        if (!std::isfinite(m) || m <= 0 || m > stockLength + kEps)
            return invalid("The maximum worktop piece length must be > 0 and at most the usable stock length " +
                           mm(stockLength) + " mm of \"" + materialId + "\", got " + num(m) + " mm.");
        plan.usableStockLength = m;
    }
    if (!(plan.usableStockLength > 0))
        return invalid("Material \"" + materialId + "\" has no usable stock length after trims.", materialId);

    // --- cut-outs (worktop coordinates) and their rule-of-thumb checks ---
    const WorktopRules& rules = request.rules;
    std::vector<Rect> rects;
    std::vector<Zone> zones;
    for (std::size_t i = 0; i < request.cutouts.size(); ++i) {
        const WorktopCutout& c = request.cutouts[i];
        const std::string label = cutoutLabel(c, i);
        if (!std::isfinite(c.centerX) || !std::isfinite(c.length) || !std::isfinite(c.depth) || c.length <= 0 ||
            c.depth <= 0)
            return invalid("The " + label + " needs a finite position and a length and depth > 0.");
        if (!std::isfinite(c.cornerRadius) || c.cornerRadius < 0 ||
            c.cornerRadius > std::min(c.length, c.depth) / 2 + kEps)
            return invalid("The corner radius " + num(c.cornerRadius) + " mm of the " + label +
                           " must be between 0 and half its smaller size (" + mm(std::min(c.length, c.depth) / 2) +
                           " mm).");
        const double cx = c.centerX + (plan.runStart - plan.left);
        Rect r{cx - c.length / 2, 0, cx + c.length / 2, 0};
        if (c.frontSetback) {
            if (!std::isfinite(*c.frontSetback)) return invalid("The front setback of the " + label + " is not finite.");
            r.y0 = *c.frontSetback;
        } else {
            r.y0 = (plan.depth - c.depth) / 2;
        }
        r.y1 = r.y0 + c.depth;
        if (r.y0 < rules.minEdgeDistance - kEps)
            return invalid("The " + label + " is " + mm(r.y0) + " mm from the front edge; keep at least " +
                           mm(rules.minEdgeDistance) + " mm (rule of thumb).");
        if (plan.depth - r.y1 < rules.minEdgeDistance - kEps)
            return invalid("The " + label + " is " + mm(plan.depth - r.y1) + " mm from the back edge of the " +
                           mm(plan.depth) + " mm deep worktop; keep at least " + mm(rules.minEdgeDistance) +
                           " mm (rule of thumb).");
        if (r.x0 < rules.minEndDistance - kEps)
            return invalid("The " + label + " is " + mm(r.x0) + " mm from the left end of the worktop; keep at least " +
                           mm(rules.minEndDistance) + " mm (rule of thumb).");
        if (plan.length - r.x1 < rules.minEndDistance - kEps)
            return invalid("The " + label + " is " + mm(plan.length - r.x1) +
                           " mm from the right end of the worktop; keep at least " + mm(rules.minEndDistance) +
                           " mm (rule of thumb).");
        for (std::size_t j = 0; j < rects.size(); ++j) {
            const Rect& o = rects[j];
            const double dx = std::max({o.x0 - r.x1, r.x0 - o.x1, 0.0});
            const double dy = std::max({o.y0 - r.y1, r.y0 - o.y1, 0.0});
            const double d = std::hypot(dx, dy);
            if (d < rules.minCutoutGap - kEps)
                return invalid("The " + label + " is " + mm(d) + " mm from the " + cutoutLabel(request.cutouts[j], j) +
                               "; keep at least " + mm(rules.minCutoutGap) + " mm between cut-outs (rule of thumb).");
        }
        rects.push_back(r);
        zones.push_back({r.x0 - rules.minJointDistance, r.x1 + rules.minJointDistance, i});
    }

    // --- joints ---
    const auto pieces = static_cast<std::size_t>(std::ceil(plan.length / plan.usableStockLength - 1e-9));
    if (pieces > 20)
        return invalid("The worktop is " + mm(plan.length) + " mm long and would need " + std::to_string(pieces) +
                       " pieces of at most " + mm(plan.usableStockLength) + " mm (more than 20).");
    auto joints = layoutJoints(plan.length, plan.usableStockLength, std::max<std::size_t>(pieces, 1), zones, request);
    if (!joints) return joints.error();
    plan.joints = joints.value();

    // --- pieces ---
    std::vector<double> bounds{0.0};
    bounds.insert(bounds.end(), plan.joints.begin(), plan.joints.end());
    bounds.push_back(plan.length);
    const std::size_t n = bounds.size() - 1;
    for (std::size_t k = 0; k < n; ++k) {
        const double x0 = bounds[k], x1 = bounds[k + 1];
        const std::string id = n == 1 ? request.id : request.id + "." + std::to_string(k + 1);
        const std::string name = n == 1 ? request.name : request.name + " " + std::to_string(k + 1) + "/" + std::to_string(n);
        if (project.findItem(id) != nullptr)
            return core::makeError(core::ErrorCode::AlreadyExists, "The project already has a furniture item \"" + id + "\".",
                                   id);
        furniture::PanelDefinition def = furniture::makePanel(domain::PartId(id + "/panel"), name, domain::MaterialId(materialId));
        def.role = domain::PartRole::Worktop;
        def.orientation = furniture::PanelOrientation::Horizontal;
        parametric::Transaction tx;
        tx.setValue(furniture::panel_param::kLength, x1 - x0)
            .setValue(furniture::panel_param::kWidth, plan.depth)
            .setValue(furniture::panel_param::kX, plan.left + x0)
            .setValue(furniture::panel_param::kY, plan.front)
            .setValue(furniture::panel_param::kZ, plan.top);
        if (auto r = def.parameters.apply(tx); !r)
            return invalid("Worktop piece \"" + id + "\": " + r.error().message, id);
        const auto setEdge = [&](domain::EdgeSide s, bool on) {
            if (on && band) def.edges[static_cast<std::size_t>(s)] = band;
        };
        setEdge(domain::EdgeSide::L1, request.bandFront);
        setEdge(domain::EdgeSide::L2, request.bandBack);
        setEdge(domain::EdgeSide::W1, request.bandLeftEnd && k == 0);
        setEdge(domain::EdgeSide::W2, request.bandRightEnd && k + 1 == n);
        furniture::ContourSpec spec;
        for (const Rect& r : rects) {
            if (r.x1 <= x0 || r.x0 >= x1) continue; // joints never cross a cut-out (zones)
            furniture::CutoutSpec cs;
            cs.kind = furniture::CutoutKind::Rectangle;
            cs.x = num(r.x0 - x0);
            cs.y = num(r.y0);
            cs.length = num(r.x1 - r.x0);
            cs.width = num(r.y1 - r.y0);
            const std::size_t idx = static_cast<std::size_t>(&r - rects.data());
            if (request.cutouts[idx].cornerRadius > 0) cs.radius = num(request.cutouts[idx].cornerRadius);
            spec.cutouts.push_back(cs);
        }
        if (!spec.cutouts.empty()) def.contour = spec;
        auto generated = furniture::generatePanel(def, libs);
        if (!generated) return invalid("Worktop piece \"" + id + "\" cannot be generated: " + generated.error().message, id);
        PanelItem panel{domain::AssemblyId(id), reference, std::move(def)};
        Item item;
        item.definition = std::move(panel);
        plan.items.push_back(std::move(item));
        if (k + 1 < n)
            plan.notes.push_back({core::Severity::Info, "project.worktop.butt_joint",
                                  "Butt joint between \"" + id + "\" and the next piece at " + mm(x1) +
                                      " mm from the left end (square raw ends; connector bolts are not modelled).",
                                  id});
    }
    return plan;
}

core::Result<WorktopPlan> addWorktop(Project& project, const WorktopRequest& request) {
    auto plan = planWorktop(project, request);
    if (!plan) return plan;
    if (plan.value().addedMaterial) project.libraries.materials.push_back(*plan.value().addedMaterial);
    if (plan.value().addedEdgeBand) project.libraries.edgeBands.push_back(*plan.value().addedEdgeBand);
    for (const Item& item : plan.value().items) project.items.push_back(item);
    return plan;
}

namespace commands {
Command addWorktop(WorktopRequest request) {
    Command c;
    std::string run;
    for (const auto& id : request.itemIds) run += (run.empty() ? "" : ", ") + id;
    c.label = CommandLabel{"Add worktop {0} over {1}", {request.id, run}};
    c.apply = [request](Project& p) -> core::Result<Affected> {
        auto plan = project::addWorktop(p, request);
        if (!plan) return plan.error();
        Affected a;
        for (const Item& item : plan.value().items) a.items.push_back(item.id());
        a.libraries = plan.value().addedMaterial.has_value() || plan.value().addedEdgeBand.has_value();
        return a;
    };
    return c;
}
} // namespace commands

} // namespace fos::project
