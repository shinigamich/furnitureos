// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/drawings/drawing.hpp"

#include <algorithm>
#include <functional>
#include <optional>

namespace fos::drawings {
namespace {

using Annotator = std::function<ViewAnnotations(const View&, double scale)>;

/// Role of a view in the arrangement of one sheet.
enum class Slot { Main, Vertical /* above or below Main */, Side /* right of Main */ };

struct ViewJob {
    View view;
    Annotator annotate;
    Slot slot = Slot::Main;
};

struct SheetJob {
    std::string sheetId;
    std::string subject; ///< For messages, e.g. "the views of 'Base cabinet 800'".
    std::vector<ViewJob> views; ///< views[0] is Slot::Main.
    bool verticalBelow = true;
    double gap = 12.0;
    EditModelContext model; ///< Resolves model-anchored user dimensions.
};

struct Arrangement {
    std::vector<ViewAnnotations> annotations;
    std::vector<std::vector<Primitive>> primitives; ///< Rendered with origin (0, 0).
    std::vector<Rect2> boxes;                       ///< Geometry + annotation boxes, origin (0, 0).
    std::vector<Vec2> origins;                      ///< Relative to an arbitrary common origin.
    Rect2 total;
    EditTracker tracker;
};

Arrangement arrange(const SheetJob& job, double scale, const DrawingStyle& style, const DrawingEdits& edits) {
    Arrangement a;
    const ViewPlacement atOrigin{{0, 0}, scale};
    for (const ViewJob& vj : job.views) {
        ViewAnnotations ann = vj.annotate(vj.view, scale);
        applyEditsToView(vj.view, job.model, ann, edits, a.tracker);
        std::vector<Primitive> prims = renderView(vj.view, ann, atOrigin, style);
        Rect2 box = boundsOf(prims);
        box.expand(Rect2{vj.view.bounds.min * scale, vj.view.bounds.max * scale});
        a.annotations.push_back(std::move(ann));
        a.primitives.push_back(std::move(prims));
        a.boxes.push_back(box);
    }
    // The main view sits at (0, 0). A vertical neighbour shares its x origin (aligned
    // projection) and a side neighbour shares its y origin.
    const Rect2& main = a.boxes.front();
    for (std::size_t i = 0; i < job.views.size(); ++i) {
        const Rect2& b = a.boxes[i];
        Vec2 o{0, 0};
        if (job.views[i].slot == Slot::Vertical)
            o.y = job.verticalBelow ? main.min.y - job.gap - b.max.y : main.max.y + job.gap - b.min.y;
        else if (job.views[i].slot == Slot::Side)
            o.x = main.max.x + job.gap - b.min.x;
        a.origins.push_back(o);
        a.total.expand(b.translated(o));
    }
    return a;
}

struct SheetOutcome {
    Sheet sheet;
    std::vector<ViewAnnotations> annotations;
    EditTracker tracker;
};

core::Result<SheetOutcome> buildSheet(const SheetJob& job, const SheetOptions& sheetOptions, const DrawingStyle& style,
                                      const DrawingEdits& edits) {
    const ScaleRequest& req = sheetOptions.scale;
    std::vector<Scale> candidates;
    if (req.mode == ScaleRequest::Mode::Explicit) {
        if (!(req.scale.numerator > 0 && req.scale.denominator > 0))
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Drawing scale " + req.scale.text() + " is not valid; both numbers must be positive.");
        candidates.push_back(req.scale);
    } else {
        candidates = standardScales();
    }
    std::optional<core::Error> lastError;
    for (const Scale& scale : candidates) {
        auto sheetResult = makeSheet(sheetOptions, scale, job.sheetId);
        if (!sheetResult) return sheetResult.error();
        Sheet sheet = std::move(sheetResult).value();
        Arrangement a = arrange(job, scale.factor(), style, edits);
        const Rect2& area = sheet.drawingArea;
        const double tol = 1e-6;
        if (a.total.width() > area.width() + tol || a.total.height() > area.height() + tol) {
            const std::string paper = core::formatLength(sheet.width) + " x " + core::formatLength(sheet.height);
            lastError = core::makeError(
                core::ErrorCode::ConstraintViolation,
                (req.mode == ScaleRequest::Mode::Explicit
                     ? "At scale " + scale.text() + " "
                     : "Even at the smallest standard scale " + scale.text() + " ") +
                    job.subject + " with dimensions need " + core::formatLength(a.total.width()) + " x " +
                    core::formatLength(a.total.height()) + " mm, but the drawing area of the " + paper +
                    " mm sheet is only " + core::formatLength(area.width()) + " x " +
                    core::formatLength(area.height()) + " mm. Choose a smaller scale, a larger sheet or fit-to-page.",
                job.sheetId);
            continue;
        }
        // Centre the arrangement in the drawing area.
        const Vec2 shift = area.center() - a.total.center();
        for (std::size_t i = 0; i < job.views.size(); ++i) {
            const Vec2 origin = a.origins[i] + shift;
            for (Primitive& p : a.primitives[i]) {
                translate(p, origin);
                sheet.primitives.push_back(std::move(p));
            }
            sheet.views.push_back({job.views[i].view.id, {origin, scale.factor()}, a.boxes[i].translated(origin)});
        }
        return SheetOutcome{std::move(sheet), std::move(a.annotations), std::move(a.tracker)};
    }
    return *lastError;
}

void mergeTracker(EditTracker& into, const EditTracker& from) {
    into.usedElementIds.insert(from.usedElementIds.begin(), from.usedElementIds.end());
    into.usedAnnotationIds.insert(from.usedAnnotationIds.begin(), from.usedAnnotationIds.end());
    into.rejectedAnnotationIds.insert(from.rejectedAnnotationIds.begin(), from.rejectedAnnotationIds.end());
    into.unresolvedAnnotations.insert(from.unresolvedAnnotations.begin(), from.unresolvedAnnotations.end());
    into.diagnostics.insert(into.diagnostics.end(), from.diagnostics.begin(), from.diagnostics.end());
}

core::Result<DrawingResult> run(std::vector<SheetJob> jobs, SheetOptions sheetOptions, const DrawingStyle& style,
                                const DrawingEdits& edits) {
    DrawingResult result;
    EditTracker tracker;
    const int first = sheetOptions.title.sheetNumber;
    const int count = std::max(sheetOptions.title.sheetCount, first + static_cast<int>(jobs.size()) - 1);
    for (std::size_t s = 0; s < jobs.size(); ++s) {
        sheetOptions.title.sheetNumber = first + static_cast<int>(s);
        sheetOptions.title.sheetCount = count;
        auto outcome = buildSheet(jobs[s], sheetOptions, style, edits);
        if (!outcome) return outcome.error();
        SheetOutcome o = std::move(outcome).value();
        mergeTracker(tracker, o.tracker);
        for (std::size_t i = 0; i < jobs[s].views.size(); ++i) {
            const View& v = jobs[s].views[i].view;
            result.diagnostics.insert(result.diagnostics.end(), v.diagnostics.begin(), v.diagnostics.end());
            result.views.push_back({v, std::move(o.annotations[i]), s});
        }
        result.sheets.push_back(std::move(o.sheet));
    }
    result.edits = finishEdits(edits, tracker);
    result.diagnostics.insert(result.diagnostics.end(), result.edits.diagnostics.begin(),
                              result.edits.diagnostics.end());
    return result;
}

} // namespace

core::Result<DrawingResult> generateAssemblyDrawing(const domain::Model& model, const domain::AssemblyId& assemblyId,
                                                    const AssemblyDrawingOptions& options, const DrawingEdits& edits) {
    const domain::Assembly* assembly = model.findAssembly(assemblyId);
    if (!assembly)
        return core::makeError(core::ErrorCode::NotFound,
                               "Assembly '" + assemblyId.str() + "' does not exist in the model.", assemblyId.str());
    const DimensionStyle dimStyle = options.style.dimension;
    const AutoDimensionOptions dimOptions = options.dimensions;
    const bool autoDims = options.autoDimensions;
    const Annotator annotate = [dimStyle, dimOptions, autoDims](const View& v, double scale) {
        ViewAnnotations ann;
        if (autoDims) ann.dimensions = autoDimensionView(v, scale, dimStyle, dimOptions);
        return ann;
    };
    ViewOptions vo;
    vo.hiddenLines = options.hiddenLines;
    const bool firstAngle = options.projection == ProjectionMethod::FirstAngle;
    const std::string name = "'" + (assembly->name.empty() ? assemblyId.str() : assembly->name) + "'";

    std::vector<SheetJob> jobs;
    SheetJob mainJob;
    mainJob.sheetId = assemblyId.str() + "/sheet.views";
    mainJob.subject = "the views of " + name;
    mainJob.verticalBelow = firstAngle;
    mainJob.gap = options.viewGap;
    mainJob.model.assembly = assembly;
    mainJob.views.push_back({generateAssemblyView(*assembly, ViewKind::Front, vo), annotate, Slot::Main});
    if (mainJob.views.front().view.bounds.empty())
        return core::makeError(core::ErrorCode::NotFound,
                               "Assembly " + name + " has no part with a positive size, so there is nothing to draw.",
                               assemblyId.str());
    if (options.topView)
        mainJob.views.push_back({generateAssemblyView(*assembly, ViewKind::Top, vo), annotate, Slot::Vertical});
    if (options.sideView)
        mainJob.views.push_back({generateAssemblyView(*assembly, firstAngle ? ViewKind::Left : ViewKind::Right, vo),
                                 annotate, Slot::Side});
    jobs.push_back(std::move(mainJob));

    for (std::size_t i = 0; i < options.sections.size(); ++i) {
        const SectionRequest& sr = options.sections[i];
        ViewOptions so = vo;
        so.sectionPosition = sr.position;
        so.id = sr.id;
        SheetJob job;
        job.model.assembly = assembly;
        job.subject = "the section of " + name + " at " + core::formatLength(sr.position) + " mm";
        job.views.push_back({generateAssemblyView(*assembly, sr.direction, so), annotate, Slot::Main});
        job.sheetId = assemblyId.str() + "/sheet.section." + std::to_string(i + 1);
        if (job.views.front().view.bounds.empty())
            return core::makeError(core::ErrorCode::ConstraintViolation,
                                   "The section plane at " + core::formatLength(sr.position) + " mm (" +
                                       toString(sr.direction) + " view) leaves no part of " + name +
                                       " behind it, so the section would be empty.",
                                   job.views.front().view.id);
        jobs.push_back(std::move(job));
    }
    SheetOptions sheetOptions = options.sheet;
    if (sheetOptions.title.title.empty()) sheetOptions.title.title = assembly->name;
    return run(std::move(jobs), std::move(sheetOptions), options.style, edits);
}

core::Result<DrawingResult> generatePartDrawing(const domain::Model& model, const domain::PartId& partId,
                                                const PartDrawingOptions& options, const DrawingEdits& edits) {
    const domain::Part* part = nullptr;
    for (const domain::Assembly& a : model.assemblies)
        if ((part = a.findPart(partId))) break;
    if (!part)
        return core::makeError(core::ErrorCode::NotFound, "Part '" + partId.str() + "' does not exist in the model.",
                               partId.str());
    View view = generatePartView(*part, options.view);
    if (view.bounds.empty())
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               view.diagnostics.empty() ? "Part '" + part->name + "' cannot be drawn."
                                                        : view.diagnostics.front().message,
                               partId.str());
    const DimensionStyle dimStyle = options.style.dimension;
    const PartViewOptions viewOptions = options.view;
    SheetJob job;
    job.sheetId = partId.str() + "/sheet";
    job.subject = "the drawing of part '" + part->name + "'";
    job.model.part = part;
    job.views.push_back({std::move(view),
                         [&model, part, dimStyle, viewOptions](const View& v, double scale) {
                             return annotatePartView(model, *part, v, scale, dimStyle, viewOptions);
                         },
                         Slot::Main});
    SheetOptions sheetOptions = options.sheet;
    if (sheetOptions.title.title.empty()) sheetOptions.title.title = part->name;
    std::vector<SheetJob> jobs;
    jobs.push_back(std::move(job));
    return run(std::move(jobs), std::move(sheetOptions), options.style, edits);
}

} // namespace fos::drawings
