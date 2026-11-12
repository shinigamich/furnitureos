// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/drawings/edits.hpp"

#include <cmath>

namespace fos::drawings {
namespace {

using core::formatLength;

struct Applier {
    const DrawingEdits& edits;
    EditTracker& tracker;

    const ElementEdit* find(const std::string& id) {
        const auto it = edits.elements.find(id);
        if (it == edits.elements.end()) return nullptr;
        tracker.usedElementIds.insert(id);
        return &it->second;
    }
    void notApplicable(const std::string& id, const char* what, const char* kind) {
        tracker.diagnostics.push_back({core::Severity::Warning, "drawings.edits.not_applicable",
                                       std::string("The ") + what + " stored for '" + id + "' was ignored because " +
                                           kind + " has no such property.",
                                       id});
    }
    /// Common part for elements without offset/text override.
    template <class T> void simple(T& e, const char* kind) {
        const ElementEdit* ed = find(e.id);
        if (!ed) return;
        e.hidden = ed->hidden;
        e.textShift = e.textShift + ed->textShift;
        if (ed->offset) notApplicable(e.id, "offset override", kind);
        if (ed->textOverride) notApplicable(e.id, "text override", kind);
        if (ed->measuredAtEdit) notApplicable(e.id, "measured value of a text override", kind);
    }
};

/// Flags `d` and reports it when its override was made against another measured value.
void checkStale(Dimension& d, const std::optional<double>& measuredAtEdit, EditTracker& tracker) {
    if (!d.textOverride || !measuredAtEdit) return;
    const double now = measuredValue(d);
    if (std::fabs(now - *measuredAtEdit) <= kStaleOverrideTolerance) return;
    d.staleOverride = true;
    tracker.diagnostics.push_back(
        {core::Severity::Warning, "drawings.edits.stale_override",
         "Dimension '" + d.id + "' shows the text override '" + *d.textOverride + "', which was made when it measured " +
             formatLength(*measuredAtEdit) + " mm; the model now measures " + formatLength(now) +
             " mm. The sheet shows '" + *d.textOverride + kStaleOverrideMarker +
             "'; check the override or remove it.",
         d.id});
}

const char* subjectOf(const View& view) {
    return view.subject == ViewSubject::Part ? "the drawn part" : "the drawn assembly";
}

bool idTaken(const ViewAnnotations& a, const std::string& id) {
    for (const auto& e : a.dimensions) if (e.id == id) return true;
    for (const auto& e : a.notes) if (e.id == id) return true;
    for (const auto& e : a.leaders) if (e.id == id) return true;
    for (const auto& e : a.detailMarkers) if (e.id == id) return true;
    for (const auto& e : a.edgeBandMarks) if (e.id == id) return true;
    for (const auto& e : a.grainArrows) if (e.id == id) return true;
    return false;
}

void applyEdits(const std::string& viewId, const View* view, const EditModelContext& model, ViewAnnotations& ann,
                const DrawingEdits& edits, EditTracker& tracker) {
    Applier ap{edits, tracker};
    for (Dimension& d : ann.dimensions) {
        const ElementEdit* ed = ap.find(d.id);
        if (!ed) continue;
        d.hidden = ed->hidden;
        d.textShift = d.textShift + ed->textShift;
        if (ed->offset) d.offset = *ed->offset;
        if (ed->textOverride) d.textOverride = ed->textOverride;
        if (ed->measuredAtEdit && !ed->textOverride)
            ap.notApplicable(d.id, "measured value of a text override", "an edit without a text override");
        checkStale(d, ed->measuredAtEdit, tracker);
    }
    for (Note& n : ann.notes) {
        const ElementEdit* ed = ap.find(n.id);
        if (!ed) continue;
        n.hidden = ed->hidden;
        n.textShift = n.textShift + ed->textShift;
        if (ed->textOverride) n.text = *ed->textOverride;
        if (ed->offset) ap.notApplicable(n.id, "offset override", "a note");
        if (ed->measuredAtEdit) ap.notApplicable(n.id, "measured value of a text override", "a note");
    }
    for (LeaderNote& l : ann.leaders) ap.simple(l, "a leader");
    for (DetailMarker& m : ann.detailMarkers) ap.simple(m, "a detail marker");
    for (EdgeBandMark& m : ann.edgeBandMarks) ap.simple(m, "an edge-band mark");
    for (GrainArrow& g : ann.grainArrows) ap.simple(g, "a grain arrow");

    // User-authored annotations of this view (appended after the generated ones).
    const auto accept = [&](const std::string& id, const std::string& owner) {
        if (owner != viewId) return false;
        if (id.empty() || idTaken(ann, id) || tracker.usedAnnotationIds.count(id) != 0) {
            tracker.diagnostics.push_back({core::Severity::Warning, "drawings.edits.duplicate_id",
                                           "User annotation '" + id + "' in view '" + viewId +
                                               "' was skipped because its id is empty or already used by another "
                                               "element of the drawing.",
                                           id});
            tracker.rejectedAnnotationIds.insert(id); // reported here, not as an orphan
            return false;
        }
        tracker.usedAnnotationIds.insert(id);
        return true;
    };
    for (const UserDimension& u : edits.dimensions) {
        if (u.viewId != viewId) continue;
        std::optional<Dimension> d;
        if (view) {
            std::string problem;
            bool missingPart = false;
            for (const auto* a : {&u.anchor1, &u.anchor2})
                if (*a) missingPart = missingPart || resolveAnchor(**a, *view, model).missingPart;
            d = resolveUserDimension(u, *view, model, problem);
            if (!d) {
                if (missingPart) {
                    tracker.unresolvedAnnotations[u.id] = problem; // reported as an orphan
                } else {
                    tracker.diagnostics.push_back({core::Severity::Warning, "drawings.edits.bad_anchor",
                                                   "User dimension '" + u.id + "' in view '" + viewId +
                                                       "' was not drawn: " + problem + ".",
                                                   u.id});
                    tracker.rejectedAnnotationIds.insert(u.id);
                }
                continue;
            }
        } else if (u.anchor1 || u.anchor2) {
            tracker.unresolvedAnnotations[u.id] =
                "it is attached to model points, but view '" + viewId + "' was edited without model geometry";
            continue;
        } else {
            d = Dimension{};
            d->id = u.id;
            d->orientation = u.orientation;
            d->p1 = u.p1;
            d->p2 = u.p2;
            d->offset = u.offset;
            d->textOverride = u.textOverride;
            d->userAuthored = true;
        }
        if (!accept(u.id, u.viewId)) continue;
        checkStale(*d, u.measuredAtEdit, tracker);
        ann.dimensions.push_back(std::move(*d));
    }
    for (const UserText& u : edits.texts) {
        if (!accept(u.id, u.viewId)) continue;
        Note n;
        n.id = u.id;
        n.anchor = u.position;
        n.text = u.text;
        n.height = u.height;
        n.rotationDeg = u.rotationDeg;
        n.userAuthored = true;
        ann.notes.push_back(std::move(n));
    }
    for (const UserLeader& u : edits.leaders) {
        if (!accept(u.id, u.viewId)) continue;
        LeaderNote l;
        l.id = u.id;
        l.target = u.target;
        l.textOffset = u.textOffset;
        l.text = u.text;
        l.height = u.height;
        l.userAuthored = true;
        ann.leaders.push_back(std::move(l));
    }
    for (const UserDetailMarker& u : edits.detailMarkers) {
        if (!accept(u.id, u.viewId)) continue;
        DetailMarker m;
        m.id = u.id;
        m.center = u.center;
        m.radius = u.radius;
        m.label = u.label;
        m.userAuthored = true;
        ann.detailMarkers.push_back(std::move(m));
    }
}

} // namespace

void setTextOverride(ElementEdit& edit, const Dimension& current, std::string text) {
    edit.textOverride = std::move(text);
    edit.measuredAtEdit = measuredValue(current);
}

AnchorResolution resolveAnchor(const ModelAnchor& anchor, const View& view, const EditModelContext& model) {
    AnchorResolution r;
    const domain::Part* part = nullptr;
    const domain::PartId pid(anchor.partId);
    if (view.subject == ViewSubject::Part) {
        if (model.part && model.part->id == pid) part = model.part;
    } else if (model.assembly) {
        part = model.assembly->findPart(pid);
    }
    if (!part) {
        r.missingPart = true;
        r.problem = "its anchor part '" + anchor.partId + "' is not part of " + subjectOf(view) + " any more";
        return r;
    }
    Vec3 local = anchor.localPoint;
    if (anchor.corner) {
        const int c = *anchor.corner;
        if (c < 0 || c > 7) {
            r.problem = "anchor corner " + std::to_string(c) + " of part '" + anchor.partId +
                        "' does not exist (corners are numbered 0 to 7)";
            return r;
        }
        local = {(c & 1) ? part->length : 0.0, (c & 2) ? part->width : 0.0, (c & 4) ? part->thickness : 0.0};
    }
    if (!std::isfinite(local.x) || !std::isfinite(local.y) || !std::isfinite(local.z)) {
        r.problem = "its anchor point on part '" + anchor.partId + "' is not a finite number";
        return r;
    }
    // Part views use the part frame directly; assembly views go through the placement.
    const Vec3 p = view.subject == ViewSubject::Part ? local : part->placement.applyPoint(local);
    r.point = projectPoint(view.basis, p);
    return r;
}

std::optional<Dimension> resolveUserDimension(const UserDimension& u, const View& view,
                                              const EditModelContext& model, std::string& problem) {
    Dimension d;
    d.id = u.id;
    d.orientation = u.orientation;
    d.p1 = u.p1;
    d.p2 = u.p2;
    d.offset = u.offset;
    d.textOverride = u.textOverride;
    d.userAuthored = true;
    for (int i = 0; i < 2; ++i) {
        const std::optional<ModelAnchor>& a = i == 0 ? u.anchor1 : u.anchor2;
        if (!a) continue;
        const AnchorResolution r = resolveAnchor(*a, view, model);
        if (!r.point) {
            problem = r.problem;
            return std::nullopt;
        }
        (i == 0 ? d.p1 : d.p2) = *r.point;
    }
    return d;
}

void applyEditsToView(const View& view, const EditModelContext& model, ViewAnnotations& annotations,
                      const DrawingEdits& edits, EditTracker& tracker) {
    applyEdits(view.id, &view, model, annotations, edits, tracker);
}

void applyEditsToView(const std::string& viewId, ViewAnnotations& annotations, const DrawingEdits& edits,
                      EditTracker& tracker) {
    applyEdits(viewId, nullptr, EditModelContext{}, annotations, edits, tracker);
}

EditReport finishEdits(const DrawingEdits& edits, const EditTracker& tracker) {
    EditReport report;
    report.diagnostics = tracker.diagnostics;
    std::set<std::string> applied, orphans;
    for (const auto& [id, edit] : edits.elements) {
        if (tracker.usedElementIds.count(id) != 0) {
            applied.insert(id);
            continue;
        }
        orphans.insert(id);
        report.diagnostics.push_back(
            {core::Severity::Warning, "drawings.edits.orphan",
             "The stored drawing edit for '" + id +
                 "' could not be applied because that element no longer exists in the regenerated drawing; the "
                 "edit was kept unchanged.",
             id});
    }
    const auto annotation = [&](const std::string& id, const std::string& viewId, const char* kind) {
        if (tracker.usedAnnotationIds.count(id) != 0) {
            applied.insert(id);
            return;
        }
        if (tracker.rejectedAnnotationIds.count(id) != 0) return;
        orphans.insert(id);
        if (const auto it = tracker.unresolvedAnnotations.find(id); it != tracker.unresolvedAnnotations.end()) {
            report.diagnostics.push_back({core::Severity::Warning, "drawings.edits.orphan_annotation",
                                          std::string("The user ") + kind + " '" + id + "' in view '" + viewId +
                                              "' was not drawn because " + it->second +
                                              "; the edit was kept unchanged.",
                                          id});
            return;
        }
        report.diagnostics.push_back({core::Severity::Warning, "drawings.edits.orphan_annotation",
                                      std::string("The user ") + kind + " '" + id + "' belongs to view '" + viewId +
                                          "', which is not part of the regenerated drawing; it was not drawn.",
                                      id});
    };
    for (const auto& u : edits.dimensions) annotation(u.id, u.viewId, "dimension");
    for (const auto& u : edits.texts) annotation(u.id, u.viewId, "text");
    for (const auto& u : edits.leaders) annotation(u.id, u.viewId, "leader");
    for (const auto& u : edits.detailMarkers) annotation(u.id, u.viewId, "detail marker");
    report.appliedIds.assign(applied.begin(), applied.end());
    report.orphanIds.assign(orphans.begin(), orphans.end());
    return report;
}

} // namespace fos::drawings
