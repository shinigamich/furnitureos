// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Internal helpers of the hardware module (not installed).
#include "fos/core/diagnostic.hpp"
#include "fos/core/result.hpp"
#include "fos/hardware/placement.hpp"
#include "fos/machining/face.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace fos::hardware::detail {

inline constexpr double kAngleEps = 1e-6; ///< Tolerance on dot products of unit vectors.

inline std::string num(double v) { return core::formatLength(v, 2); }
inline std::string quoted(const domain::Part& p) { return "'" + p.name + "'"; }

/// Outward surface normal of a face in the assembly frame.
inline geometry::Vec3 outwardNormal(const domain::Part& p, domain::Face f) noexcept {
    return -machining::faceFrameInAssembly(p, f).toolDir;
}
inline geometry::Vec3 centreInAssembly(const domain::Part& p) noexcept {
    return p.placement.applyPoint({p.length / 2, p.width / 2, p.thickness / 2});
}
/// Extent of the part's box along `dir` (assembly frame): min/max of dot(corner, dir).
inline machining::Range extentAlong(const domain::Part& p, geometry::Vec3 dir) noexcept {
    machining::Range r{1e300, -1e300};
    for (int i = 0; i < 8; ++i) {
        const double d = dot(p.placement.applyPoint({(i & 1) ? p.length : 0.0, (i & 2) ? p.width : 0.0,
                                                     (i & 4) ? p.thickness : 0.0}),
                             dir);
        r.min = std::min(r.min, d);
        r.max = std::max(r.max, d);
    }
    return r;
}
inline bool isParallel(geometry::Vec3 a, geometry::Vec3 b) noexcept { return std::fabs(dot(a, b)) > 1 - kAngleEps; }
inline bool isPerpendicular(geometry::Vec3 a, geometry::Vec3 b) noexcept { return std::fabs(dot(a, b)) < kAngleEps; }

/// A hole of radius r centred at (u, v) lies completely on the face, keeping `margin`.
inline bool circleOnFace(const domain::Part& p, domain::Face f, double u, double v, double r,
                         double margin = 0) noexcept {
    const auto e = machining::faceExtents(p, f);
    const double eps = core::kLengthEps;
    return u - r - margin >= e.u.min - eps && u + r + margin <= e.u.max + eps && v - r - margin >= e.v.min - eps &&
           v + r + margin <= e.v.max + eps;
}

/// Sanity of the part itself (positive size, rigid placement).
inline core::Result<void> checkPart(const domain::Part& p) {
    if (!(p.length > 0) || !(p.width > 0) || !(p.thickness > 0) || !p.placement.isRigid(1e-6))
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Part " + quoted(p) + " (" + p.id.str() + ") has a non-positive size (" +
                                   num(p.length) + " x " + num(p.width) + " x " + num(p.thickness) +
                                   " mm) or a placement that is not a rigid transform.",
                               p.id.str());
    return {};
}

/// Collects operations per part and numbers them `<instance>/op.N`.
class OpWriter {
public:
    explicit OpWriter(const domain::HardwareInstanceId& instance) : instance_(instance.str()) {}
    void add(const domain::Part& part, const domain::Hole& hole, std::string note) {
        domain::Operation op;
        op.id = domain::OperationId(instance_ + "/op." + std::to_string(++n_));
        op.shape = hole;
        op.source = instance_;
        op.note = std::move(note);
        for (auto& po : out_)
            if (po.part == part.id) {
                po.operations.push_back(std::move(op));
                return;
            }
        out_.push_back({part.id, {std::move(op)}});
    }
    std::vector<PartOperations> take() { return std::move(out_); }

private:
    std::string instance_;
    int n_ = 0;
    std::vector<PartOperations> out_;
};

inline core::Result<void> checkInstanceId(const domain::HardwareInstanceId& id) {
    if (id.empty())
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "The placement configuration has an empty instanceId; a hardware instance id is "
                               "required to derive deterministic operation ids.");
    return {};
}

/// First error-severity diagnostic as a ConstraintViolation error, if any.
inline core::Result<void> firstError(const core::Diagnostics& ds) {
    for (const auto& d : ds)
        if (d.severity == core::Severity::Error)
            return core::makeError(core::ErrorCode::ConstraintViolation, d.message, d.entityId);
    return {};
}

} // namespace fos::hardware::detail
