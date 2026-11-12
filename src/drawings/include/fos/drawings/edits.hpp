// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"
#include "fos/domain/model.hpp"
#include "fos/drawings/annotation.hpp"
#include "fos/drawings/geom2d.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

/// USER edits to a generated drawing. Drawings are always regenerated from the
/// `domain::Model`; only this structure is stored (by the `project` module) and it is
/// re-applied by id after every regeneration. Nothing here can modify the model.
/// One DrawingEdits instance belongs to ONE drawing (one generate*Drawing call).
namespace fos::drawings {

/// Edit of an automatically generated element, keyed by its stable element id
/// (e.g. "cab1/view.front/dim.overall.width").
struct ElementEdit {
    std::optional<double> offset;            ///< Dimensions only: replaces Dimension::offset (paper mm, signed).
    Vec2 textShift{};                        ///< Paper mm, added to the element's text position.
    bool hidden = false;                     ///< Element is not drawn.
    std::optional<std::string> textOverride; ///< Dimensions and notes: replaces the displayed text.
    /// Dimensions only: measuredValue() (model mm) of the dimension when `textOverride` was
    /// made (setTextOverride() fills it). On every regeneration the current measured value is
    /// compared with it; a difference > kStaleOverrideTolerance yields the Warning
    /// "drawings.edits.stale_override" and the override is drawn flagged (Dimension::staleOverride,
    /// see dimensionText()). An override WITHOUT this value (legacy data) cannot be checked.
    std::optional<double> measuredAtEdit{};
};

/// Largest difference (model mm) between the measured value at edit time and now that still
/// counts as "unchanged" for a text override.
inline constexpr double kStaleOverrideTolerance = core::kLengthEps;

/// Sets `edit.textOverride = text` and records the value it was made against
/// (`edit.measuredAtEdit = measuredValue(current)`). `current` is the dimension as currently
/// generated (e.g. from DrawingResult::views[i].annotations).
void setTextOverride(ElementEdit& edit, const Dimension& current, std::string text);

/// A point of the MODEL that a user dimension is attached to; it is resolved into view
/// coordinates at every generation, so the dimension follows the model.
///
/// The point is given in the PART frame of part `partId` (see fos/domain/part.hpp: x along
/// length, y along width, z along thickness) either as a corner of the part box
/// [0, L] x [0, W] x [0, T] or as an explicit point. Corner numbering (same bit order as
/// geometry::Aabb::transformed): bit 0 set -> x = L, bit 1 set -> y = W, bit 2 set -> z = T:
///   0 (0,0,0)  1 (L,0,0)  2 (0,W,0)  3 (L,W,0)  4 (0,0,T)  5 (L,0,T)  6 (0,W,T)  7 (L,W,T).
/// Assembly views: part frame -> assembly frame (Part::placement) -> projectPoint(view.basis).
/// Part views: only the drawn part can be referenced (its view basis is in the part frame).
struct ModelAnchor {
    std::string partId;
    std::optional<int> corner; ///< 0..7; another value is reported ("drawings.edits.bad_anchor").
    Vec3 localPoint{};         ///< Used when `corner` is empty: point in the part frame, mm.
};

/// Model geometry used to resolve ModelAnchor points of the view being edited.
struct EditModelContext {
    const domain::Assembly* assembly = nullptr; ///< Assembly views: the drawn assembly.
    const domain::Part* part = nullptr;         ///< Part views: the drawn part.
};

/// Result of resolving one anchor.
struct AnchorResolution {
    std::optional<Vec2> point; ///< View coordinates; empty when unresolved.
    std::string problem;       ///< Why it is unresolved (English sentence fragment), else empty.
    bool missingPart = false;  ///< The referenced part is not in the drawn assembly / not the drawn part.
};

/// Resolves `anchor` in `view` (see ModelAnchor). Never fails; problems are described.
AnchorResolution resolveAnchor(const ModelAnchor& anchor, const View& view, const EditModelContext& model);

/// User-authored annotations. `id` is chosen by the caller (UUID); `viewId` names the
/// view the annotation lives in; points are view coordinates (model mm).
struct UserText {
    std::string id, viewId;
    Vec2 position;
    std::string text;
    double height = 3.5;
    double rotationDeg = 0;
};
struct UserLeader {
    std::string id, viewId;
    Vec2 target;
    Vec2 textOffset{10, 10}; ///< Paper mm.
    std::string text;
    double height = 2.5;
};
/// Extra dimension between two picked points.
///
/// Anchors: `p1`/`p2` are FIXED view coordinates (model mm). They do NOT follow the model:
/// when the model changes, a fixed-point dimension keeps measuring between the same view
/// points. Prefer `anchor1`/`anchor2` (ModelAnchor): when set, they replace p1/p2 and are
/// resolved from the model at every generation, so e.g. a dimension between two part corners
/// measures the new size after the part changes. A model anchor whose part no longer exists
/// makes the dimension an orphan (not drawn, listed in EditReport::orphanIds with a
/// "drawings.edits.orphan_annotation" Warning).
struct UserDimension {
    std::string id, viewId;
    DimensionOrientation orientation = DimensionOrientation::Horizontal;
    Vec2 p1, p2;
    double offset = 10.0;
    std::optional<std::string> textOverride;
    std::optional<ModelAnchor> anchor1{}; ///< Replaces p1 when set.
    std::optional<ModelAnchor> anchor2{}; ///< Replaces p2 when set.
    /// Measured value (model mm) when `textOverride` was made; checked like
    /// ElementEdit::measuredAtEdit ("drawings.edits.stale_override").
    std::optional<double> measuredAtEdit{};
};

/// The dimension `u` would produce in `view` (anchors resolved), or empty with `problem`
/// set when an anchor cannot be resolved. Useful to fill UserDimension::measuredAtEdit:
///   u.measuredAtEdit = measuredValue(*resolveUserDimension(u, view, model, problem)).
std::optional<Dimension> resolveUserDimension(const UserDimension& u, const View& view,
                                              const EditModelContext& model, std::string& problem);
struct UserDetailMarker {
    std::string id, viewId;
    Vec2 center;
    double radius = 50.0; ///< Model mm.
    std::string label;
};

struct DrawingEdits {
    std::map<std::string, ElementEdit> elements;
    std::vector<UserText> texts;
    std::vector<UserLeader> leaders;
    std::vector<UserDimension> dimensions;
    std::vector<UserDetailMarker> detailMarkers;

    bool empty() const noexcept {
        return elements.empty() && texts.empty() && leaders.empty() && dimensions.empty() && detailMarkers.empty();
    }
};

/// Bookkeeping while edits are applied to the views of one drawing.
struct EditTracker {
    std::set<std::string> usedElementIds;
    std::set<std::string> usedAnnotationIds;
    std::set<std::string> rejectedAnnotationIds; ///< Skipped (duplicate/empty id); already reported.
    /// User dimensions not drawn because a model anchor references a missing part: id -> reason.
    /// finishEdits() reports them as orphans.
    std::map<std::string, std::string> unresolvedAnnotations;
    core::Diagnostics diagnostics;
};

struct EditReport {
    std::vector<std::string> appliedIds; ///< Element edits and user annotations that took effect (sorted).
    std::vector<std::string> orphanIds;  ///< Ids that matched nothing in the regenerated drawing (sorted).
    /// One Warning per orphan ("drawings.edits.orphan", "drawings.edits.orphan_annotation")
    /// plus "drawings.edits.not_applicable" / "drawings.edits.duplicate_id" /
    /// "drawings.edits.stale_override" / "drawings.edits.bad_anchor" findings.
    core::Diagnostics diagnostics;
};

/// Applies `edits` to the automatically generated annotations of `view` and appends the
/// user annotations that belong to this view, resolving ModelAnchor points with `model`.
/// Never fails: problems are recorded in `tracker`. Text overrides are checked against
/// their measuredAtEdit ("drawings.edits.stale_override", the dimension gets staleOverride).
void applyEditsToView(const View& view, const EditModelContext& model, ViewAnnotations& annotations,
                      const DrawingEdits& edits, EditTracker& tracker);

/// Legacy form without model geometry: as above, but user dimensions with a ModelAnchor
/// cannot be resolved and become orphans.
void applyEditsToView(const std::string& viewId, ViewAnnotations& annotations, const DrawingEdits& edits,
                      EditTracker& tracker);

/// Call once after all views: reports every edit that matched nothing.
EditReport finishEdits(const DrawingEdits& edits, const EditTracker& tracker);

} // namespace fos::drawings
