// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/cam/postprocessor.hpp"

/// Reference postprocessor "Generic ISO G-code (RS-274 style), UNVERIFIED", id "generic-iso".
///
/// Emitted subset: comments in parentheses; G21 G90 G17 G94 preamble; "Tn M6"; "Sn M3"; "M5";
/// "G0 X Y Z" / "G0 Z" (Z-only retract); "G1 X Y Z F"; "G2|G3 X Y Z I J F" (G2 = clockwise,
/// G3 = counter-clockwise seen from +Z, XY plane G17; I/J = arc centre INCREMENTAL from the arc
/// start point, computed as rounded centre - rounded start so the printed centre is exact to the
/// 3 decimals; Z changes linearly = helix); "G4 P<seconds>"; final "M30".
/// Coordinates with exactly 3 decimals, F with 1 decimal, S as integer. No canned cycles,
/// no R-format arcs, no cutter compensation, no work-offset selection, no tool-length offset words.
/// The output starts with a comment block stating that the dialect is not verified and
/// describing the setup (origin, face up, stock, tools).
namespace fos::cam {

class GenericIsoPostprocessor final : public Postprocessor {
public:
    std::string id() const override { return "generic-iso"; }
    std::string name() const override { return "Generic ISO G-code (RS-274 style), UNVERIFIED"; }

protected:
    core::Result<PostOutput> emit(const PostInput& input, const SimulationReport& report) const override;
};

} // namespace fos::cam
