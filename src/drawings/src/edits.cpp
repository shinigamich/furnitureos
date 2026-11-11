// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/drawings/edits.hpp"

namespace fos::drawings {
namespace {

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
    }
};

bool idTaken(const ViewAnnotations& a, const std::string& id) {
    for (const auto& e : a.dimensions) if (e.id == id) return true;
    for (const auto& e : a.notes) if (e.id == id) return true;
    for (const auto& e : a.leaders) if (e.id == id) return true;
    for (const auto& e : a.detailMarkers) if (e.id == id) return true;
    for (const auto& e : a.edgeBandMarks) if (e.id == id) return true;
    for (const auto& e : a.grainArrows) if (e.id == id) return true;
    return false;
}

} // namespace

void applyEditsToView(const std::string& viewId, ViewAnnotations& ann, const DrawingEdits& edits,
                      EditTracker& tracker) {
    Applier ap{edits, tracker};
    for (Dimension& d : ann.dimensions) {
        const ElementEdit* ed = ap.find(d.id);
        if (!ed) continue;
        d.hidden = ed->hidden;
        d.textShift = d.textShift + ed->textShift;
        if (ed->offset) d.offset = *ed->offset;
        if (ed->textOverride) d.textOverride = ed->textOverride;
    }
    for (Note& n : ann.notes) {
        const ElementEdit* ed = ap.find(n.id);
        if (!ed) continue;
        n.hidden = ed->hidden;
        n.textShift = n.textShift + ed->textShift;
        if (ed->textOverride) n.text = *ed->textOverride;
        if (ed->offset) ap.notApplicable(n.id, "offset override", "a note");
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
        if (!accept(u.id, u.viewId)) continue;
        Dimension d;
        d.id = u.id;
        d.orientation = u.orientation;
        d.p1 = u.p1;
        d.p2 = u.p2;
        d.offset = u.offset;
        d.textOverride = u.textOverride;
        d.userAuthored = true;
        ann.dimensions.push_back(std::move(d));
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
