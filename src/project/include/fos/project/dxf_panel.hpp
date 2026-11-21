// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// DXF outline -> free panel item: turns a formats::DxfImport into a project::PanelItem whose
/// outline is a Custom furniture::ContourSpec (numbers, editable later) with circle / rectangle
/// cut-outs. Shared by `fos import-dxf` and the desktop app (one rule set, one set of messages).
/// No geometry is computed here beyond splitting the arcs of a two-vertex outline (a circle) in
/// halves, which does not change the shape. The caller adds the item (commands::addItem).
/// UNITS: mm. FRAME: the outline is in the part frame (bounding box from (0,0), see
/// formats::importPanelShape); `placement` maps the item frame to the project frame.
#include "fos/core/diagnostic.hpp"
#include "fos/core/result.hpp"
#include "fos/formats/dxf_import.hpp"
#include "fos/geometry/transform.hpp"
#include "fos/project/project.hpp"

#include <cstddef>
#include <optional>
#include <string>

namespace fos::project {

/// What the caller collects besides the DXF reading options.
struct DxfPanelRequest {
    std::string name;                    ///< Item / panel name (required).
    std::string materialId;              ///< Library material; empty = pick by `thickness`.
    std::optional<double> thickness;     ///< mm; alone: first library material (by id) of that
                                         ///< thickness; with a material: must agree with it.
    bool skipUnsupportedCutouts = false; ///< Leave out cut-outs that are neither circles nor rectangles.
    /// How the front end names the "skip unsupported cut-outs" switch in messages, e.g.
    /// "--skip-unsupported-cutouts" (CLI) or "\"Skip unsupported cut-outs\"" (app).
    std::string skipOptionLabel = "--skip-unsupported-cutouts";
};

/// The material for the request, or InvalidArgument naming the problem (unknown id, thickness
/// mismatch, no material of the thickness, neither given).
core::Result<domain::MaterialId> pickDxfPanelMaterial(const furniture::Libraries& libraries,
                                                     const DxfPanelRequest& request);

struct DxfPanelImport {
    Item item;                     ///< Panel item (not yet in the project).
    domain::MaterialId material;   ///< Material chosen for the panel.
    core::Diagnostics diagnostics; ///< DXF reading + panel shape + "project.import_dxf.*" findings.
    std::size_t outlineSegments = 0, arcs = 0, circles = 0, rectangles = 0, skipped = 0;
    double length = 0, width = 0;  ///< Outline bounding box, mm.
};

/// Builds the panel item `itemId` (part `<itemId>/panel`) at `placement` from `dxf`. The
/// largest loop is the outline, loops inside it cut-outs (formats::importPanelShape). Refused
/// (InvalidArgument carrying the message; `diagnosticsOut`, when given, receives what was found
/// so far): no name, `itemId` already used, no material, no closed outline, an unsupported
/// cut-out without `skipUnsupportedCutouts` (each one is an Error diagnostic
/// "project.import_dxf.cutout_unsupported"; with the switch a Warning), or a panel the
/// furniture module refuses. On success `diagnosticsOut` also receives the diagnostics.
core::Result<DxfPanelImport> buildDxfPanelItem(const Project& project, const formats::DxfImport& dxf,
                                               const DxfPanelRequest& request, const std::string& itemId,
                                               const geometry::Transform& placement = geometry::Transform::identity(),
                                               core::Diagnostics* diagnosticsOut = nullptr);

/// Item id for a panel named `name`: a lower-case slug ("Table top" -> "table-top"), made unique
/// in `project` with "-2", "-3", ... ("panel" when nothing is left).
std::string dxfPanelItemId(const Project& project, const std::string& name);

} // namespace fos::project
