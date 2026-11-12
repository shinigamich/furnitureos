// SPDX-License-Identifier: AGPL-3.0-only
#include "detail.hpp"

#include <cmath>
#include <cstdio>

namespace fos::validation::detail {

void Context::add(core::Severity severity, std::string code, std::string message, std::string entityId,
                  std::string assemblyId, std::string partId, std::vector<std::string> related) {
    Finding f;
    f.severity = severity;
    f.code = std::move(code);
    f.message = std::move(message);
    f.entityId = std::move(entityId);
    f.assemblyId = std::move(assemblyId);
    f.partId = std::move(partId);
    f.relatedIds = std::move(related);
    report.diagnostics.push_back(std::move(f));
}

std::string num(double mm) {
    if (std::isnan(mm)) return "NaN";
    if (std::isinf(mm)) return mm > 0 ? "inf" : "-inf";
    // Sub-millimetre values (tolerances, small overlaps) would read "0" at 0.1 mm precision.
    return core::formatLength(mm, std::fabs(mm) < 1.0 ? 3 : core::kDisplayDecimals);
}

std::string label(const domain::Part& p) {
    return p.name.empty() ? "part '" + p.id.str() + "'" : "'" + p.name + "'";
}
std::string label(const domain::Assembly& a) {
    return a.name.empty() ? "assembly '" + a.id.str() + "'" : "assembly '" + a.name + "'";
}

bool finite(double v) noexcept { return std::isfinite(v); }
bool finite(const geometry::Vec3& v) noexcept { return finite(v.x) && finite(v.y) && finite(v.z); }
bool finite(const geometry::Transform& t) noexcept {
    return finite(t.xAxis) && finite(t.yAxis) && finite(t.zAxis) && finite(t.origin);
}

bool validSize(const domain::Part& p) noexcept {
    return finite(p.length) && finite(p.width) && finite(p.thickness) && p.length > 0 && p.width > 0 &&
           p.thickness > 0;
}

bool validPlacement(const geometry::Transform& t) noexcept { return finite(t) && t.isRigid(1e-6); }

namespace {
int frameAxis(geometry::Vec3 v) noexcept {
    const double c[3] = {v.x, v.y, v.z};
    for (int i = 0; i < 3; ++i)
        if (std::fabs(std::fabs(c[i]) - 1.0) <= 1e-9 && std::fabs(c[(i + 1) % 3]) <= 1e-9 &&
            std::fabs(c[(i + 2) % 3]) <= 1e-9)
            return i;
    return -1;
}
} // namespace

std::string axisName(geometry::Vec3 axis) {
    const int i = frameAxis(axis);
    if (i >= 0) return std::string(1, static_cast<char>('X' + i));
    char buf[96];
    auto r = [](double x) { return std::fabs(x) < 5e-4 ? 0.0 : x; };
    std::snprintf(buf, sizeof buf, "(%.3f, %.3f, %.3f)", r(axis.x), r(axis.y), r(axis.z));
    return buf;
}

bool axisAligned(const geometry::Transform& t) noexcept {
    return frameAxis(t.xAxis) >= 0 && frameAxis(t.yAxis) >= 0 && frameAxis(t.zAxis) >= 0;
}

} // namespace fos::validation::detail
