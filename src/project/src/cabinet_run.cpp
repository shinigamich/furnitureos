// SPDX-License-Identifier: AGPL-3.0-only
#include "cabinet_run.hpp"

#include "fos/project/resolve.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>

namespace fos::project::detail {

std::string runNum(double v) {
    if (std::fabs(v) < 1e-9) v = 0;
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed);
    return r.ec == std::errc() ? std::string(buf, r.ptr) : std::to_string(v);
}

std::string runMm(double v) { return runNum(std::round(v * 10.0) / 10.0); }

namespace {

core::Error invalid(std::string message, std::string context = {}) {
    return core::makeError(core::ErrorCode::InvalidArgument, std::move(message), std::move(context));
}

bool sameRotation(const geometry::Transform& relative) {
    const auto near = [](geometry::Vec3 a, geometry::Vec3 b) {
        return std::fabs(a.x - b.x) < 1e-9 && std::fabs(a.y - b.y) < 1e-9 && std::fabs(a.z - b.z) < 1e-9;
    };
    return near(relative.xAxis, {1, 0, 0}) && near(relative.yAxis, {0, 1, 0}) && near(relative.zAxis, {0, 0, 1});
}

} // namespace

core::Result<CabinetRun> analyzeRun(const Project& project, const std::vector<std::string>& itemIds,
                                    std::string_view nounView) {
    const std::string noun(nounView);
    if (itemIds.empty()) return invalid("A " + noun + " needs at least one cabinet item id.");
    CabinetRun run;
    std::set<std::string> seen;
    for (const std::string& id : itemIds) {
        if (!seen.insert(id).second) return invalid("Cabinet \"" + id + "\" is listed twice for the " + noun + ".", id);
        const Item* item = project.findItem(id);
        if (item == nullptr)
            return core::makeError(core::ErrorCode::NotFound, "The project has no furniture item \"" + id + "\".", id);
        if (!item->isCabinet())
            return invalid("Item \"" + id + "\" is a free panel, not a cabinet; a " + noun + " spans cabinet items only.",
                           id);
        if (run.cabinets.empty()) run.reference = item->placement();
        const geometry::Transform relative = run.reference.inverse() * item->placement();
        if (!sameRotation(relative))
            return invalid("Cabinet \"" + id + "\" is rotated differently from \"" + itemIds.front() + "\"; a " + noun +
                               " spans a straight run (corner runs need two " + noun + "s).",
                           id);
        auto assembly = generateItem(project, id);
        if (!assembly) return invalid("Cabinet \"" + id + "\" cannot be generated: " + assembly.error().message, id);
        const geometry::Aabb local = assembly.value().bounds();
        if (local.empty()) return invalid("Cabinet \"" + id + "\" has no generated parts.", id);
        run.cabinets.push_back({id, item, relative, std::move(assembly.value()), local.transformed(relative)});
    }
    std::stable_sort(run.cabinets.begin(), run.cabinets.end(),
                     [](const RunCabinet& a, const RunCabinet& b) { return a.box.min.x < b.box.min.x; });
    for (std::size_t i = 0; i < run.cabinets.size(); ++i) {
        const RunCabinet& c = run.cabinets[i];
        run.box.min.x = std::min(run.box.min.x, c.box.min.x);
        run.box.min.y = std::min(run.box.min.y, c.box.min.y);
        run.box.min.z = std::min(run.box.min.z, c.box.min.z);
        run.box.max.x = std::max(run.box.max.x, c.box.max.x);
        run.box.max.y = std::max(run.box.max.y, c.box.max.y);
        run.box.max.z = std::max(run.box.max.z, c.box.max.z);
        if (i == 0) continue;
        const RunCabinet& prev = run.cabinets[i - 1];
        const double gap = c.box.min.x - prev.box.max.x;
        if (gap < -kRunFlushTolerance)
            return invalid("Cabinets \"" + prev.id + "\" and \"" + c.id + "\" overlap by " + runMm(-gap) +
                               " mm along the run; a " + noun + " needs a straight run of cabinets side by side.",
                           c.id);
        if (gap > kRunFlushTolerance) run.gaps.push_back({prev.id, c.id, gap});
    }
    return run;
}

} // namespace fos::project::detail
