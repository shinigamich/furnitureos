// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Internal state shared by the generator stages (carcass, back, interior, fronts, hardware).
#include "fos/furniture/generate.hpp"
#include "fos/furniture/parameters.hpp"
#include "fos/hardware/handle.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace fos::furniture::detail {

using geometry::Transform;
using geometry::Vec3;

inline constexpr Vec3 kRight{1, 0, 0}, kBack{0, 1, 0}, kUp{0, 0, 1};

/// Which edge band an edge of a generated part receives.
enum class EdgeUse { Raw, Visible, Hidden, Front };
/// Indexed by domain::EdgeSide (L1, L2, W1, W2).
using Edges = std::array<EdgeUse, 4>;

enum class JobKind { Corner, ShelfPins, Hinges, Handle };
/// A deferred hardware placement; run after all parts exist (gen_hardware.cpp).
struct Job {
    JobKind kind = JobKind::Corner;
    std::string instance;   ///< Instance id suffix after "<cabinet>/".
    std::size_t a = 0;      ///< Corner: face part. ShelfPins: shelf. Hinges: door. Handle: front.
    std::size_t b = 0;      ///< Corner: edge part. ShelfPins: left support. Hinges: carrying panel.
    std::size_t c = 0;      ///< ShelfPins: right support.
    JointFastener fastener = JointFastener::None; ///< Corner.
    bool sharedPanel = false; ///< A carrying panel is an interior partition (may be drilled from both faces).
    hardware::HandleConfig handle;
};

/// The panels bounding a section (indices into Gen::assembly.parts).
struct Bounds {
    std::size_t left = 0, right = 0;
    std::vector<std::size_t> bottom, top; ///< Two entries for stretcher rails.
    bool leftOuter = true, rightOuter = true, bottomOuter = true, topOuter = true; ///< Carcass panel (not a divider).
};
/// Clear opening of a section in the assembly frame (x across, z up).
struct Box {
    double x0 = 0, x1 = 0, z0 = 0, z1 = 0;
};

struct Gen {
    const CabinetDefinition& def; ///< Resolved definition.
    const Libraries& libs;
    std::map<std::string, double> values;
    domain::Assembly assembly;
    core::Diagnostics diagnostics;
    std::vector<Job> jobs;

    /// Value of a parameter of the resolved graph (all standard names exist).
    double v(const char* name) const { return values.at(name); }
    std::string partId(const std::string& slot) const { return def.id.str() + "/" + slot; }
    /// Index of the part with this slot; parts.size() when absent.
    std::size_t index(const std::string& slot) const;
    void warn(std::string code, std::string message, std::string entity);
    void error(std::string code, std::string message, std::string entity);

    /// Appends a part; material, grain and edge bands come from the role assignment and the
    /// per-slot overrides. `thickness` is the thickness parameter of the role.
    std::size_t addPart(const std::string& slot, std::string name, domain::PartRole role, MaterialRole material,
                        double length, double width, double thickness, const Transform& placement, const Edges& edges);
};

/// "a.b" joining of slot segments; empty segments are skipped ("door", "", "left" -> "door.left").
std::string joinSlot(std::initializer_list<std::string_view> segments);
/// Display form of a section path: "root" for "".
std::string sectionLabel(const std::string& path);
/// Number for messages (formatLength with 3 decimals).
std::string num(double value);

/// Splits `available` mm among `sizes` (Fixed / Expression take their millimetres, Weight
/// children share the rest). `what` names the thing being split for error messages.
core::Result<std::vector<double>> distribute(const Gen& g, double available, const std::vector<SectionSize>& sizes,
                                             const std::string& what);

core::Result<Bounds> buildCarcass(Gen& g);
core::Result<void> buildBack(Gen& g);
core::Result<void> buildInterior(Gen& g, const Bounds& rootBounds);
core::Result<void> buildFront(Gen& g, const FrontSpec& spec, const std::string& path, const Box& box, const Bounds& bounds);
void runHardware(Gen& g);

} // namespace fos::furniture::detail
