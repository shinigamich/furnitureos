// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/cam/toolpath.hpp"
#include "fos/core/result.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/// Independent reader for the subset emitted by GenericIsoPostprocessor, used to check that
/// the text reproduces the IR. Not a general G-code interpreter.
namespace fos::cam {

/// Parses the text back into motions (sources empty). ParseError naming the line for any
/// word outside the subset, motion before the G21/G90 preamble, missing F on G1, missing
/// M30, or anything after M30. Modal G0/G1 and missing axes (previous value) are handled.
core::Result<std::vector<Motion>> parseGenericIso(std::string_view text);

struct RoundTripReport {
    bool matches = false;
    std::size_t expectedMotions = 0;
    std::size_t parsedMotions = 0;
    std::vector<std::string> differences; ///< English, "motion #3: ..."; empty when matches
};

/// Compares kinds and values: coordinates/seconds within `tolerance` mm/s, feed within
/// 0.05, rpm within 0.5 (emission rounding). Z-only rapids compare Z only.
RoundTripReport compareMotions(const std::vector<Motion>& expected, const std::vector<Motion>& parsed,
                               double tolerance = 0.0005 + 1e-9);

core::Result<RoundTripReport> checkRoundTrip(const Program& program, std::string_view text);

} // namespace fos::cam
