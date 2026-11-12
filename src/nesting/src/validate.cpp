// SPDX-License-Identifier: AGPL-3.0-only
// Independent result check: uses only the job, the result and fos::geometry, never the optimiser.
#include "fos/nesting/validate.hpp"

#include "fos/core/units.hpp"
#include "fos/nesting/nest.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>

namespace fos::nesting {

namespace {

using geometry::Polygon2;
using geometry::Rect2;

constexpr double kEps = core::kLengthEps;

std::string fmt(double v) { return core::formatLength(v, 6); }

void err(core::Diagnostics& out, const std::string& code, std::string message, std::string entity) {
    out.push_back({core::Severity::Error, "nesting.result." + code, std::move(message), std::move(entity)});
}

bool closeRel(double a, double b) { return std::fabs(a - b) <= 1e-6 * std::max({1.0, std::fabs(a), std::fabs(b)}); }

struct Placed {
    const Placement* p;
    Polygon2 outline;
    Rect2 box;
};

} // namespace

core::Diagnostics validateNest(const Job& job, const NestResult& result, const NestOptions& options) {
    core::Diagnostics out;
    if (!result.isHeuristic) err(out, "not_heuristic", "The result must be flagged as heuristic.", "");

    std::map<std::string, std::size_t> partIx, sheetIx;
    for (std::size_t i = 0; i < job.parts.size(); ++i) partIx[job.parts[i].id] = i;
    for (std::size_t i = 0; i < job.sheets.size(); ++i) sheetIx[job.sheets[i].id] = i;

    std::vector<int> placedCount(job.parts.size(), 0), unplacedCount(job.parts.size(), 0);
    std::set<std::pair<std::size_t, int>> seen;
    std::map<std::size_t, std::set<int>> copies;
    double totalPart = 0, totalNet = 0, totalSheet = 0;
    int placedTotal = 0;
    const double s = job.partSpacing;

    for (std::size_t si = 0; si < result.sheets.size(); ++si) {
        const SheetLayout& L = result.sheets[si];
        const std::string sname = "Sheet " + std::to_string(si) + " ('" + L.sheetId + "')";
        auto sit = sheetIx.find(L.sheetId);
        if (sit == sheetIx.end()) {
            err(out, "unknown_sheet", sname + " refers to a stock sheet that is not in the job.", L.sheetId);
            continue;
        }
        const Sheet& sheet = job.sheets[sit->second];
        if (!copies[sit->second].insert(L.copy).second)
            err(out, "stock", sname + " copy " + std::to_string(L.copy) + " is used twice.", L.sheetId);
        const Rect2 area = placementArea(job, sheet);
        std::vector<Placed> placed;
        double partArea = 0, used = 0;
        for (const Placement& p : L.placements) {
            auto pit = partIx.find(p.partId);
            if (pit == partIx.end()) {
                err(out, "unknown_part", sname + " holds unknown part '" + p.partId + "'.", p.partId);
                continue;
            }
            const Part& part = job.parts[pit->second];
            const std::string pname = "Part '" + part.label + "' instance " + std::to_string(p.instance);
            if (p.instance < 0 || p.instance >= part.quantity)
                err(out, "bad_instance", pname + " is outside 0.." + std::to_string(part.quantity - 1) + ".", part.id);
            if (!seen.insert({pit->second, p.instance}).second)
                err(out, "duplicate_instance", pname + " is placed more than once.", part.id);
            ++placedCount[pit->second];
            const auto rots = allowedRotations(part, options);
            if (std::none_of(rots.begin(), rots.end(), [&](double r) { return std::fabs(r - p.rotationDeg) <= 1e-9; }))
                err(out, "rotation_not_allowed", pname + " is rotated by " + fmt(p.rotationDeg) +
                                                     " degrees, which its rotation rule does not allow.", part.id);
            if (p.mirrored && !part.mirrorAllowed)
                err(out, "mirror_not_allowed", pname + " is mirrored but mirroring is not allowed.", part.id);

            // Re-derive from the ORIGINAL outline: mirror (x -> -x), rotate about origin, translate.
            Polygon2 o = p.mirrored ? geometry::mirroredX(part.outline, 0.0) : part.outline;
            o = geometry::translated(geometry::rotated(o, p.rotationDeg), p.translation);
            bool same = o.size() == p.outline.size();
            for (std::size_t k = 0; same && k < o.size(); ++k) same = geometry::approxEqual(o[k], p.outline[k], kEps);
            if (!same) err(out, "outline_mismatch", pname + ": the stored outline differs from the re-derived one.", part.id);
            for (auto v : o.vertices)
                if (!area.contains(v, kEps)) {
                    err(out, "outside_area",
                        pname + " has vertex (" + fmt(v.x) + ", " + fmt(v.y) + ") outside the placement area [" +
                            fmt(area.min.x) + ", " + fmt(area.max.x) + "] x [" + fmt(area.min.y) + ", " +
                            fmt(area.max.y) + "] of " + sname + ".",
                        part.id);
                    break;
                }
            const double a = geometry::area(part.outline);
            double net = a;
            for (const auto& h : part.holes) net -= geometry::area(h);
            partArea += a;
            totalNet += net;
            const Rect2 box = geometry::bounds(o);
            used = std::max(used, box.max.x);
            placed.push_back({&p, std::move(o), box});
        }
        for (std::size_t i = 0; i < placed.size(); ++i)
            for (std::size_t j = i + 1; j < placed.size(); ++j) {
                const Rect2 &a = placed[i].box, &b = placed[j].box;
                const double dx = std::max({0.0, a.min.x - b.max.x, b.min.x - a.max.x});
                const double dy = std::max({0.0, a.min.y - b.max.y, b.min.y - a.max.y});
                if (std::hypot(dx, dy) >= s + 1e-9) continue;
                const std::string pair = "Parts '" + placed[i].p->partId + "'#" + std::to_string(placed[i].p->instance) +
                                         " and '" + placed[j].p->partId + "'#" + std::to_string(placed[j].p->instance);
                if (s > kEps) {
                    const double d = geometry::polygonDistance(placed[i].outline, placed[j].outline);
                    if (d < s - kEps)
                        err(out, "spacing", pair + " on " + sname + " are " + fmt(d) + " mm apart; the spacing is " +
                                                fmt(s) + " mm.", placed[i].p->partId);
                } else if (geometry::polygonsOverlap(placed[i].outline, placed[j].outline)) {
                    err(out, "spacing", pair + " on " + sname + " overlap.", placed[i].p->partId);
                }
            }
        const double sheetArea = sheet.length * sheet.width;
        if (!closeRel(partArea, L.partArea) || !closeRel(sheetArea, L.sheetArea) ||
            !closeRel(L.utilisation, sheetArea > 0 ? partArea / sheetArea : 0) || !closeRel(used, L.usedLength))
            err(out, "area", sname + ": reported areas/utilisation/used length differ from recomputed values (part area " +
                                 fmt(partArea) + " mm2, used length " + fmt(used) + " mm).", L.sheetId);
        const double x0 = used + s;
        const bool expectRemnant = sheet.length - x0 > kEps && sheet.length - x0 >= job.minRemnantLength;
        if (expectRemnant != L.hasRemnant ||
            (L.hasRemnant && !(closeRel(L.remnant.min.x, x0) && closeRel(L.remnant.max.x, sheet.length) &&
                               L.remnant.min.y == 0 && closeRel(L.remnant.max.y, sheet.width))))
            err(out, "area", sname + ": the remnant should " + (expectRemnant ? "" : "not ") +
                                 "exist and start at x = " + fmt(x0) + " mm.", L.sheetId);
        totalPart += partArea;
        totalSheet += sheetArea;
        placedTotal += static_cast<int>(L.placements.size());
    }
    for (const auto& [ix, set] : copies) {
        const Sheet& sh = job.sheets[ix];
        if (sh.quantity != kUnlimited && static_cast<int>(set.size()) > sh.quantity)
            err(out, "stock", std::to_string(set.size()) + " sheets of '" + sh.id + "' are used but only " +
                                  std::to_string(sh.quantity) + " are available.", sh.id);
    }
    for (const Unplaced& u : result.unplaced) {
        auto pit = partIx.find(u.partId);
        if (pit == partIx.end()) {
            err(out, "unknown_part", "Unplaced entry refers to unknown part '" + u.partId + "'.", u.partId);
            continue;
        }
        if (!seen.insert({pit->second, u.instance}).second || u.instance < 0 ||
            u.instance >= job.parts[pit->second].quantity)
            err(out, "duplicate_instance", "Unplaced instance " + std::to_string(u.instance) + " of '" + u.partId +
                                               "' is invalid or also listed elsewhere.", u.partId);
        ++unplacedCount[pit->second];
    }
    for (std::size_t i = 0; i < job.parts.size(); ++i)
        if (placedCount[i] + unplacedCount[i] != job.parts[i].quantity)
            err(out, "quantity", "Part '" + job.parts[i].id + "': " + std::to_string(placedCount[i]) + " placed + " +
                                     std::to_string(unplacedCount[i]) + " unplaced != quantity " +
                                     std::to_string(job.parts[i].quantity) + ".", job.parts[i].id);
    if (placedTotal != result.placedCount || !closeRel(totalPart, result.totalPartArea) ||
        !closeRel(totalNet, result.totalNetPartArea) || !closeRel(totalSheet, result.totalSheetArea) ||
        !closeRel(result.utilisation, totalSheet > 0 ? totalPart / totalSheet : 0))
        err(out, "area", "Result totals (count, areas, utilisation) differ from the recomputed values.", "");
    return out;
}

} // namespace fos::nesting
