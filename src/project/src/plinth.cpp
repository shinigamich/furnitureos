// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/project/plinth.hpp"

#include "cabinet_run.hpp"
#include "fos/furniture/panel.hpp"
#include "fos/furniture/parameters.hpp"

#include <algorithm>
#include <cmath>

namespace fos::project {

namespace {

using detail::kRunEps;
using detail::kRunFlushTolerance;
constexpr double kLevelTolerance = 1.0; ///< Allowed difference of plinth heights / floor levels (mm).
constexpr std::size_t kMaxPieces = 40;

core::Error invalid(std::string message, std::string context = {}) {
    return core::makeError(core::ErrorCode::InvalidArgument, std::move(message), std::move(context));
}

std::string num(double v) { return detail::runNum(v); }
std::string mm(double v) { return detail::runMm(v); }

bool endsWith(const std::string& s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// x range (run coordinates, from the run's left end) blocked by an appliance end panel.
struct Obstacle {
    double lo, hi;
    std::string cabinet;
};

/// Panel item with the five standard parameters set; error names the item.
core::Result<Item> makePiece(const std::string& id, const std::string& name, const std::string& materialId,
                             furniture::PanelOrientation orientation, double length, double width, geometry::Vec3 at,
                             const geometry::Transform& reference, const furniture::Libraries& libs) {
    furniture::PanelDefinition def =
        furniture::makePanel(domain::PartId(id + "/panel"), name, domain::MaterialId(materialId));
    def.role = domain::PartRole::Plinth;
    def.orientation = orientation;
    parametric::Transaction tx;
    tx.setValue(furniture::panel_param::kLength, length)
        .setValue(furniture::panel_param::kWidth, width)
        .setValue(furniture::panel_param::kX, at.x)
        .setValue(furniture::panel_param::kY, at.y)
        .setValue(furniture::panel_param::kZ, at.z);
    if (auto r = def.parameters.apply(tx); !r) return invalid("Plinth piece \"" + id + "\": " + r.error().message, id);
    if (auto g = furniture::generatePanel(def, libs); !g)
        return invalid("Plinth piece \"" + id + "\" cannot be generated: " + g.error().message, id);
    Item item;
    item.definition = PanelItem{domain::AssemblyId(id), reference, std::move(def)};
    return item;
}

} // namespace

core::Result<PlinthPlan> planPlinth(const Project& project, const PlinthRequest& request) {
    if (request.id.empty()) return invalid("A plinth needs a non-empty item id.");
    if (!std::isfinite(request.setback) || request.setback < 0 || request.setback > 300)
        return invalid("Plinth setback must be between 0 and 300 mm, got " +
                       (std::isfinite(request.setback) ? num(request.setback) : std::string("a non-finite value")) + ".");
    if (request.height && (!std::isfinite(*request.height) || *request.height <= 0))
        return invalid("Plinth height must be a finite number > 0 mm, got " + num(*request.height) + ".");

    auto runR = detail::analyzeRun(project, request.itemIds, "plinth");
    if (!runR) return runR.error();
    const detail::CabinetRun& run = runR.value();
    const std::vector<detail::RunCabinet>& cabinets = run.cabinets;

    PlinthPlan plan;
    plan.left = run.box.min.x;
    plan.length = run.box.max.x - run.box.min.x;
    plan.floor = 0; // z = 0 of the reference cabinet's assembly frame

    // --- cabinet kinds, plinth heights, floor level, carcass fronts ---
    std::optional<double> feetLo, feetHi;
    std::string feetLoId, feetHiId;
    double carcassFront = cabinets.front().relative.origin.y;
    std::vector<Obstacle> obstacles;
    for (const detail::RunCabinet& c : cabinets) {
        plan.runOrder.push_back(c.id);
        const furniture::CabinetDefinition& def = *c.item->cabinet();
        if (std::fabs(c.relative.origin.z) > kLevelTolerance)
            return invalid("Cabinet \"" + c.id + "\" stands " + mm(c.relative.origin.z) + " mm above the floor of \"" +
                               request.itemIds.front() + "\"; a plinth needs every cabinet on the same floor level.",
                           c.id);
        carcassFront = std::min(carcassFront, c.relative.origin.y);
        if (def.applianceGap) {
            for (const domain::Part& part : c.assembly.parts) {
                const std::string pid = part.id.str();
                if (!endsWith(pid, "end_panel.left") && !endsWith(pid, "end_panel.right")) continue;
                const geometry::Aabb b = part.bounds().transformed(c.relative);
                obstacles.push_back({b.min.x - plan.left, b.max.x - plan.left, c.id});
            }
            continue;
        }
        switch (def.construction.plinth) {
        case furniture::PlinthKind::Feet: break;
        case furniture::PlinthKind::SidesToFloor:
            return invalid("Cabinet \"" + c.id +
                               "\" has sides to the floor and its own recessed plinth panel; a run plinth needs "
                               "cabinets on feet (set its plinth construction to Feet).",
                           c.id);
        case furniture::PlinthKind::None:
            return invalid("Cabinet \"" + c.id +
                               "\" has no plinth space (plinth construction None); a run plinth needs cabinets on feet.",
                           c.id);
        }
        auto h = def.parameters.value(furniture::param::kPlinthHeight);
        if (!h) return invalid("Cabinet \"" + c.id + "\": the plinth height cannot be read: " + h.error().message, c.id);
        if (!feetLo || h.value() < *feetLo) feetLo = h.value(), feetLoId = c.id;
        if (!feetHi || h.value() > *feetHi) feetHi = h.value(), feetHiId = c.id;
    }
    if (feetLo && *feetHi - *feetLo > kLevelTolerance)
        return invalid("The cabinets stand on feet of different heights: \"" + feetLoId + "\" has a plinth height of " +
                           mm(*feetLo) + " mm, \"" + feetHiId + "\" " + mm(*feetHi) +
                           " mm (more than 1 mm apart); level the cabinets before adding a plinth.",
                       feetLoId);
    for (const detail::RunGap& g : run.gaps)
        plan.notes.push_back({core::Severity::Warning, "project.plinth.gap",
                              "There is a " + mm(g.width) + " mm gap between cabinets \"" + g.leftId + "\" and \"" +
                                  g.rightId + "\"; the plinth bridges it (add a filler or close the gap).",
                              g.rightId});
    for (const detail::RunCabinet& c : cabinets)
        if (c.relative.origin.y - carcassFront > kRunFlushTolerance)
            plan.notes.push_back({core::Severity::Info, "project.plinth.front_offset",
                                  "The carcass front of cabinet \"" + c.id + "\" is " +
                                      mm(c.relative.origin.y - carcassFront) +
                                      " mm behind the foremost one; its plinth setback is larger by that amount.",
                                  c.id});

    // --- height ---
    if (request.height) {
        plan.height = *request.height;
        if (feetLo && plan.height > *feetLo + kLevelTolerance)
            return invalid("A plinth of " + mm(plan.height) + " mm does not fit under cabinet \"" + feetLoId +
                               "\", whose carcass is raised by " + mm(*feetLo) + " mm on feet.",
                           feetLoId);
        if (!feetLo && plan.height > 300)
            return invalid("Plinth height must be at most 300 mm when no cabinet on feet sets it, got " +
                           num(plan.height) + " mm.");
    } else {
        if (!feetLo)
            return invalid("No cabinet on feet in the run sets the plinth height (only appliance gaps); give the "
                           "plinth height explicitly.");
        plan.height = *feetLo;
    }
    if (!(plan.height > 0))
        return invalid("The plinth height of cabinet \"" + feetLoId + "\" is " + mm(plan.height) +
                           " mm; a plinth needs a height > 0.",
                       feetLoId);

    // --- material and stock ---
    const furniture::Libraries& libs = project.libraries;
    // Default: carcass material of the REFERENCE (first listed, not necessarily leftmost) cabinet.
    plan.materialId = request.materialId.empty()
                          ? project.findItem(request.itemIds.front())->cabinet()->materials.carcass.str()
                          : request.materialId;
    const domain::Material* material = libs.findMaterial(domain::MaterialId(plan.materialId));
    if (material == nullptr)
        return core::makeError(core::ErrorCode::NotFound,
                               "The project library has no material \"" + plan.materialId + "\" for the plinth.",
                               plan.materialId);
    plan.thickness = material->thickness;
    if (!std::isfinite(plan.thickness) || plan.thickness <= 0)
        return invalid("Material \"" + plan.materialId + "\" has no valid thickness.", plan.materialId);
    const CuttingSettings& cut = project.cutting.forMaterial(plan.materialId);
    const double stockLength = material->sheetLength - cut.trim.left - cut.trim.right;
    const double stockWidth = material->sheetWidth - cut.trim.top - cut.trim.bottom;
    if (plan.height > stockWidth + kRunEps)
        return invalid("The plinth is " + mm(plan.height) + " mm high but material \"" + plan.materialId +
                           "\" gives at most " + mm(stockWidth) + " mm of stock width after trims.",
                       plan.materialId);
    plan.usableStockLength = stockLength;
    if (request.maxPieceLength) {
        const double m = *request.maxPieceLength;
        if (!std::isfinite(m) || m <= 0 || m > stockLength + kRunEps)
            return invalid("The maximum plinth piece length must be > 0 and at most the usable stock length " +
                           mm(stockLength) + " mm of \"" + plan.materialId + "\", got " + num(m) + " mm.");
        plan.usableStockLength = m;
    }
    if (!(plan.usableStockLength > 0))
        return invalid("Material \"" + plan.materialId + "\" has no usable stock length after trims.", plan.materialId);
    plan.front = carcassFront + request.setback;

    // --- segments between appliance end panels, split into equal pieces ---
    std::sort(obstacles.begin(), obstacles.end(), [](const Obstacle& a, const Obstacle& b) { return a.lo < b.lo; });
    std::vector<std::pair<double, double>> segments;
    double cursor = 0;
    for (const Obstacle& o : obstacles) {
        if (o.lo - cursor > kRunFlushTolerance) segments.emplace_back(cursor, o.lo);
        cursor = std::max(cursor, o.hi);
    }
    if (plan.length - cursor > kRunFlushTolerance) segments.emplace_back(cursor, plan.length);
    if (segments.empty())
        return invalid("The run consists only of appliance end panels that reach the floor; there is no plinth to add.");
    for (const auto& [s0, s1] : segments) {
        const auto n = static_cast<std::size_t>(std::ceil((s1 - s0) / plan.usableStockLength - 1e-9));
        const std::size_t count = std::max<std::size_t>(n, 1);
        if (plan.pieces.size() + count > kMaxPieces)
            return invalid("The plinth would need more than " + std::to_string(kMaxPieces) + " pieces of at most " +
                           mm(plan.usableStockLength) + " mm.");
        for (std::size_t k = 0; k < count; ++k) {
            const double a = s0 + (s1 - s0) * static_cast<double>(k) / static_cast<double>(count);
            const double b = k + 1 == count ? s1 : s0 + (s1 - s0) * static_cast<double>(k + 1) / static_cast<double>(count);
            plan.pieces.emplace_back(a, b);
        }
    }

    // --- items ---
    const geometry::Transform reference = run.reference;
    const auto checkFree = [&](const std::string& id) -> core::Result<void> {
        if (project.findItem(id) != nullptr)
            return core::makeError(core::ErrorCode::AlreadyExists,
                                   "The project already has a furniture item \"" + id + "\".", id);
        return {};
    };
    const std::size_t n = plan.pieces.size();
    for (std::size_t k = 0; k < n; ++k) {
        const auto [x0, x1] = plan.pieces[k];
        const std::string id = n == 1 ? request.id : request.id + "." + std::to_string(k + 1);
        const std::string name =
            n == 1 ? request.name : request.name + " " + std::to_string(k + 1) + "/" + std::to_string(n);
        if (auto r = checkFree(id); !r) return r.error();
        auto item = makePiece(id, name, plan.materialId, furniture::PanelOrientation::VerticalFront, x1 - x0,
                              plan.height, {plan.left + x0, plan.front, plan.floor}, reference, libs);
        if (!item) return item.error();
        plan.items.push_back(std::move(item.value()));
        if (k + 1 < n && std::fabs(plan.pieces[k + 1].first - x1) < kRunEps)
            plan.notes.push_back({core::Severity::Info, "project.plinth.butt_joint",
                                  "Butt joint between \"" + id + "\" and the next piece at " + mm(x1) +
                                      " mm from the left end of the run (square raw ends; fixing clips are not modelled).",
                                  id});
        for (const detail::RunCabinet& c : cabinets) {
            if (!c.item->cabinet()->applianceGap) continue;
            const double c0 = c.box.min.x - plan.left, c1 = c.box.max.x - plan.left;
            if (std::min(x1, c1) - std::max(x0, c0) <= kRunFlushTolerance) continue;
            plan.notes.push_back({core::Severity::Info, "project.plinth.appliance",
                                  "Plinth piece \"" + id + "\" runs in front of appliance gap \"" + c.id +
                                      "\"; integrated dishwashers often need a lower or notched plinth for the door "
                                      "swing (rule of thumb; check the appliance installation sheet).",
                                  id});
        }
    }
    plan.frontPieces = n;

    // --- end returns ---
    for (const bool leftEnd : {true, false}) {
        if (leftEnd ? !request.returnLeft : !request.returnRight) continue;
        const char* side = leftEnd ? "left" : "right";
        for (const Obstacle& o : obstacles)
            if (leftEnd ? o.lo < kRunFlushTolerance : o.hi > plan.length - kRunFlushTolerance)
                return invalid(std::string("The ") + side + " end of the run is closed by an end panel of appliance gap \"" +
                                   o.cabinet + "\" that reaches the floor; no " + side + " plinth return is needed.",
                               o.cabinet);
        const detail::RunCabinet& end = leftEnd ? cabinets.front() : cabinets.back();
        const double y0 = plan.front + plan.thickness;
        const double depth = end.box.max.y - y0;
        if (!(depth > kRunFlushTolerance))
            return invalid(std::string("The ") + side + " plinth return would be " + mm(depth) + " mm deep: cabinet \"" +
                               end.id + "\" ends at y = " + mm(end.box.max.y) + " mm, the plinth back face is at y = " +
                               mm(y0) + " mm.",
                           end.id);
        const std::string id = request.id + "." + side;
        if (auto r = checkFree(id); !r) return r.error();
        const double x = leftEnd ? plan.left : plan.left + plan.length - plan.thickness;
        auto item = makePiece(id, request.name + " return " + side, plan.materialId,
                              furniture::PanelOrientation::VerticalSide, plan.height, depth, {x, y0, plan.floor},
                              reference, libs);
        if (!item) return item.error();
        plan.items.push_back(std::move(item.value()));
    }
    return plan;
}

core::Result<PlinthPlan> addPlinth(Project& project, const PlinthRequest& request) {
    auto plan = planPlinth(project, request);
    if (!plan) return plan;
    for (const Item& item : plan.value().items) project.items.push_back(item);
    return plan;
}

namespace commands {
Command addPlinth(PlinthRequest request) {
    Command c;
    std::string run;
    for (const auto& id : request.itemIds) run += (run.empty() ? "" : ", ") + id;
    c.label = CommandLabel{"Add plinth {0} under {1}", {request.id, run}};
    c.apply = [request](Project& p) -> core::Result<Affected> {
        auto plan = project::addPlinth(p, request);
        if (!plan) return plan.error();
        Affected a;
        for (const Item& item : plan.value().items) a.items.push_back(item.id());
        return a;
    };
    return c;
}
} // namespace commands

} // namespace fos::project
