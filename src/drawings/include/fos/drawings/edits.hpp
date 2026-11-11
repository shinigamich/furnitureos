// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"
#include "fos/drawings/annotation.hpp"

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
};

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
/// Extra dimension between two picked model points (projected into the view).
struct UserDimension {
    std::string id, viewId;
    DimensionOrientation orientation = DimensionOrientation::Horizontal;
    Vec2 p1, p2;
    double offset = 10.0;
    std::optional<std::string> textOverride;
};
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
    core::Diagnostics diagnostics;
};

struct EditReport {
    std::vector<std::string> appliedIds; ///< Element edits and user annotations that took effect (sorted).
    std::vector<std::string> orphanIds;  ///< Ids that matched nothing in the regenerated drawing (sorted).
    /// One Warning per orphan ("drawings.edits.orphan", "drawings.edits.orphan_annotation")
    /// plus "drawings.edits.not_applicable" / "drawings.edits.duplicate_id" findings.
    core::Diagnostics diagnostics;
};

/// Applies `edits` to the automatically generated annotations of view `viewId` and
/// appends the user annotations that belong to this view. Never fails: problems are
/// recorded in `tracker`.
void applyEditsToView(const std::string& viewId, ViewAnnotations& annotations, const DrawingEdits& edits,
                      EditTracker& tracker);

/// Call once after all views: reports every edit that matched nothing.
EditReport finishEdits(const DrawingEdits& edits, const EditTracker& tracker);

} // namespace fos::drawings
