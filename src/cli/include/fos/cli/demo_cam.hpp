// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// FICTIONAL demonstration data for `fos cam`. The machine profile and the tool library below
/// describe NO real machine and NO real tools: the numbers are plausible round values chosen
/// only so that the generated cabinet parts can be run through the CAM pipeline (toolpath
/// generation, independent simulation, unverified generic ISO postprocessor). The profile is
/// never verified (`verified == false`), so every program carries the UNVERIFIED header and
/// `PostOutput::verifiedForMachine` is false. Do not run such output on a machine.
#include "fos/cam/machine.hpp"
#include "fos/cam/tool.hpp"
#include "fos/geometry/vec.hpp"

namespace fos::cli {

/// Name of the demo profile; contains the words "demo profile — not a real machine".
inline constexpr const char* kDemoProfileName = "FurnitureOS demo profile — not a real machine";

/// Fictional 3-axis router: envelope X 0..3200, Y 0..1600, Z -5..150 mm (machine frame,
/// Z = 0 spoilboard top), safe Z 60, clearance 5, max feed 12000 mm/min, max 24000 rpm,
/// rapid 25000 mm/min, tool change 8 s, spoilboard penetration limit 1 mm. verified = false.
cam::MachineProfile demoMachineProfile();

/// Fictional tool library (tool number: kind diameter / flute length):
///   T1 drill 3/30, T2 drill 4/30, T3 drill 5/30, T4 drill 7/30, T5 drill 8/35, T6 drill 10/35,
///   T7 drill 12/35, T8 drill 15/20, T9 drill 20/20, T10 drill 35/15,
///   T11 end mill 4/22 (DOC 3), T12 end mill 6/22 (DOC 3), T13 end mill 8/25 (DOC 4).
/// Drills: plunge 1500 mm/min, 6000 rpm (35 mm: plunge 800, 3000 rpm). End mills: feed 3000,
/// plunge 1000 mm/min, 18000 rpm, centre cutting.
cam::ToolLibrary demoToolLibrary();

/// Stock origin used by `fos cam`: the panel's minimum corner at machine (20, 20, 0).
inline geometry::Vec3 demoStockOrigin() { return {20.0, 20.0, 0.0}; }

} // namespace fos::cli
