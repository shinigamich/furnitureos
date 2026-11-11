// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/drawings/view.hpp"

#include "fos/drawings/hlr.hpp"

#include <cmath>

namespace fos::drawings {

ViewBasis viewBasis(ViewKind k) noexcept {
    switch (k) {
    case ViewKind::Front: return {{1, 0, 0}, {0, 0, 1}, {0, -1, 0}};
    case ViewKind::Back: return {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}};
    case ViewKind::Left: return {{0, -1, 0}, {0, 0, 1}, {-1, 0, 0}};
    case ViewKind::Right: return {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}};
    case ViewKind::Top: return {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    case ViewKind::Bottom: return {{1, 0, 0}, {0, -1, 0}, {0, 0, -1}};
    }
    return {{1, 0, 0}, {0, 0, 1}, {0, -1, 0}};
}

const char* toString(ViewKind k) noexcept {
    switch (k) {
    case ViewKind::Front: return "front";
    case ViewKind::Back: return "back";
    case ViewKind::Left: return "left";
    case ViewKind::Right: return "right";
    case ViewKind::Top: return "top";
    case ViewKind::Bottom: return "bottom";
    }
    return "?";
}

bool isAxisAligned(const geometry::Transform& placement) noexcept {
    const auto axisOk = [](Vec3 a) {
        const double c[3] = {std::fabs(a.x), std::fabs(a.y), std::fabs(a.z)};
        int ones = 0, zeros = 0;
        for (const double v : c) {
            if (std::fabs(v - 1.0) <= 1e-9) ++ones;
            if (v <= 1e-9) ++zeros;
        }
        return ones == 1 && zeros == 2;
    };
    return axisOk(placement.xAxis) && axisOk(placement.yAxis) && axisOk(placement.zAxis);
}

View generateAssemblyView(const domain::Assembly& assembly, ViewKind kind, const ViewOptions& options) {
    View view;
    view.kind = kind;
    view.subject = ViewSubject::Assembly;
    view.basis = viewBasis(kind);
    view.sectionPosition = options.sectionPosition;
    view.id = !options.id.empty()
                  ? options.id
                  : assembly.id.str() + (options.sectionPosition ? "/section." : "/view.") + toString(kind);

    const ViewBasis& b = view.basis;
    // toViewer is +/- one assembly axis, so the plane "axis = position" has this depth:
    const double axisSign = b.toViewer.x + b.toViewer.y + b.toViewer.z;
    const bool section = options.sectionPosition.has_value();
    const double planeDepth = section ? *options.sectionPosition * axisSign : 0.0;

    std::vector<HlrBox> boxes;
    std::vector<HlrLooseEdge> loose;
    std::size_t cutIndex = 0;

    for (const domain::Part& part : assembly.parts) {
        const std::string& pid = part.id.str();
        if (!(part.length > 0 && part.width > 0 && part.thickness > 0)) {
            view.diagnostics.push_back(
                {core::Severity::Warning, "drawings.view.degenerate_part",
                 "Part '" + part.name + "' has a non-positive size (" + core::formatLength(part.length) + " x " +
                     core::formatLength(part.width) + " x " + core::formatLength(part.thickness) +
                     " mm) and was left out of the view.",
                 pid});
            continue;
        }
        if (!isAxisAligned(part.placement)) {
            const geometry::Aabb lb = part.localBounds();
            Vec2 p2[8];
            double dp[8];
            PartFootprint fp;
            fp.partId = pid;
            fp.role = part.role;
            fp.aspect = PanelAspect::Oblique;
            fp.depthNear = -std::numeric_limits<double>::infinity();
            fp.depthFar = std::numeric_limits<double>::infinity();
            for (int i = 0; i < 8; ++i) {
                const Vec3 p = part.placement.applyPoint(
                    {(i & 1) ? lb.max.x : lb.min.x, (i & 2) ? lb.max.y : lb.min.y, (i & 4) ? lb.max.z : lb.min.z});
                p2[i] = projectPoint(b, p);
                dp[i] = depthOf(b, p);
                fp.rect.expand(p2[i]);
                fp.depthNear = std::max(fp.depthNear, dp[i]);
                fp.depthFar = std::min(fp.depthFar, dp[i]);
            }
            for (int i = 0; i < 8; ++i)
                for (const int bit : {1, 2, 4})
                    if (!(i & bit)) loose.push_back({p2[i], p2[i | bit], std::max(dp[i], dp[i | bit]), pid});
            view.footprints.push_back(fp);
            view.bounds.expand(fp.rect);
            view.diagnostics.push_back(
                {core::Severity::Warning, "drawings.hlr.not_axis_aligned",
                 "Part '" + part.name + "' is rotated off the assembly axes; hidden-line removal" +
                     (section ? " and section cutting are" : " is") +
                     " only implemented for axis-aligned panels, so all of its edges are drawn as visible and it "
                     "does not hide other parts.",
                 pid});
            continue;
        }

        const geometry::Aabb box = part.bounds();
        const Vec2 q0 = projectPoint(b, box.min), q1 = projectPoint(b, box.max);
        const double d0 = depthOf(b, box.min), d1 = depthOf(b, box.max);
        PartFootprint fp;
        fp.partId = pid;
        fp.role = part.role;
        fp.rect = Rect2::fromCorners(q0, q1);
        fp.depthNear = std::max(d0, d1);
        fp.depthFar = std::min(d0, d1);
        const Vec3 t = part.placement.zAxis;
        if (std::fabs(dot(t, b.right)) > 0.5)
            fp.aspect = PanelAspect::EdgeOnVertical;
        else if (std::fabs(dot(t, b.up)) > 0.5)
            fp.aspect = PanelAspect::EdgeOnHorizontal;
        else
            fp.aspect = PanelAspect::FaceOn;

        if (section) {
            if (fp.depthFar >= planeDepth - core::kLengthEps) continue; // entirely on the viewer's side
            if (fp.depthNear > planeDepth + core::kLengthEps) {
                fp.depthNear = planeDepth;
                fp.cut = true;
                view.cuts.push_back({fp.rect, (cutIndex++ % 2 == 0) ? 45.0 : 135.0, pid});
            }
        }
        boxes.push_back({fp.rect, fp.depthNear, fp.depthFar, pid});
        view.footprints.push_back(fp);
        view.bounds.expand(fp.rect);
    }

    view.segments = removeHiddenLines(boxes, loose, options.hiddenLines);
    return view;
}

} // namespace fos::drawings
