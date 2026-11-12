// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"

#include <string>
#include <string_view>
#include <vector>

/// Cutting tools and the tool library used by CAM. All lengths in mm, feeds in mm/min,
/// spindle speeds in revolutions per minute. Tool data is user data: nothing here is
/// verified against a real tool or manufacturer recommendation.
namespace fos::cam {

/// Drill: axial drilling only (flat-bottomed cylinder model; tip geometry is ignored).
/// EndMill: axial plunge (if centre cutting) and side milling.
enum class ToolKind { Drill, EndMill };
const char* toString(ToolKind k) noexcept;

struct Tool {
    std::string id;          ///< Unique, e.g. "drill.5".
    int number = 0;          ///< Tool number in the changer / tool table (Tn), >= 1, unique.
    ToolKind kind = ToolKind::Drill;
    std::string description;
    double diameter = 0;     ///< mm
    double fluteLength = 0;  ///< Usable cutting length, mm. Must be >= every cutting depth.
    double feedRate = 0;     ///< Horizontal cutting feed, mm/min (end mills).
    double plungeRate = 0;   ///< Axial feed, mm/min (drills and end-mill plunges).
    double rpm = 0;          ///< Spindle speed, 1/min.
    double maxDepthOfCut = 0;///< EndMill: maximum axial step-down per pass, mm. Unused for drills.
    bool centerCutting = true; ///< EndMill: may plunge axially. Required for grooves.
};

/// Set of tools, kept sorted by tool number (deterministic).
class ToolLibrary {
public:
    /// Adds a tool after checking it: non-empty unique id, unique number >= 1, positive
    /// diameter / flute length / feeds / rpm, positive maxDepthOfCut for end mills.
    core::Result<void> add(Tool tool);
    const std::vector<Tool>& tools() const noexcept { return tools_; }
    const Tool* findByNumber(int number) const noexcept;
    const Tool* findById(std::string_view id) const noexcept;

    /// Drill whose diameter equals `diameter` (within 1e-6 mm) and whose flute length is
    /// >= `requiredDepth`; lowest tool number wins. Errors (NotFound), e.g.
    /// "No drill of diameter 7 mm in the tool library (available: 5, 8, 35)." or
    /// "Drill 'drill.5' (T1, 5 mm) has flute length 10 mm but 13 mm of cutting depth is needed."
    core::Result<const Tool*> selectDrill(double diameter, double requiredDepth) const;
    /// Centre-cutting end mill with diameter <= `grooveWidth` and flute length >=
    /// `requiredDepth`; the largest diameter wins, ties: lowest tool number.
    core::Result<const Tool*> selectGrooveCutter(double grooveWidth, double requiredDepth) const;

private:
    std::vector<Tool> tools_;
};

} // namespace fos::cam
