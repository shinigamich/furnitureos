// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// File > Import DXF outline: turns a formats::DxfImport into a free panel item whose outline
/// is a Custom furniture::ContourSpec (numbers, editable later) with circle / rectangle
/// cut-outs, exactly as `fos import-dxf` does (same rules and messages). No geometry is
/// computed here beyond splitting the arcs of a two-vertex outline (a circle) in halves, which
/// does not change the shape. The caller adds the item with commands::addItem (undoable).
#include "fos/formats/dxf_import.hpp"
#include "fos/project/project.hpp"

#include <optional>
#include <string>

namespace fos::app {

/// What the import dialog collects (besides the DXF reading options).
struct DxfPanelRequest {
    std::string name;                 ///< Item / panel name (required).
    std::string materialId;           ///< Library material; empty = pick by `thickness`.
    std::optional<double> thickness;  ///< mm; alone: first library material (by id) of that thickness;
                                      ///< with a material: must agree with it.
    bool skipUnsupportedCutouts = false; ///< Leave out cut-outs that are neither circles nor rectangles.
};

/// The material for the request, or InvalidArgument naming the problem.
core::Result<domain::MaterialId> pickDxfPanelMaterial(const furniture::Libraries& libraries,
                                                     const DxfPanelRequest& request);

struct DxfPanelImport {
    project::Item item;            ///< Panel item (not yet in the project).
    core::Diagnostics diagnostics; ///< DXF reading + panel shape + "app.import_dxf.*" findings.
    std::size_t outlineSegments = 0, arcs = 0, circles = 0, rectangles = 0, skipped = 0;
    double length = 0, width = 0;  ///< Outline bounding box, mm.
};

/// Builds the panel item `itemId` (part `<itemId>/panel`) at `placement` from `dxf`. The
/// largest loop is the outline, loops inside it cut-outs (formats::importPanelShape). Refused
/// (Error carrying the message; `diagnosticsOut` gets what was found so far): no name, no
/// material, no closed outline, an unsupported cut-out without `skipUnsupportedCutouts`, or a
/// panel the furniture module refuses.
core::Result<DxfPanelImport> buildDxfPanelItem(const project::Project& project, const formats::DxfImport& dxf,
                                               const DxfPanelRequest& request, const std::string& itemId,
                                               const geometry::Transform& placement,
                                               core::Diagnostics* diagnosticsOut = nullptr);

/// Item id for a panel named `name`: a lower-case slug ("Table top" -> "table-top"), made unique
/// with "-2", "-3", ... ("panel" when nothing is left).
std::string dxfPanelItemId(const project::Project& project, const std::string& name);

} // namespace fos::app
