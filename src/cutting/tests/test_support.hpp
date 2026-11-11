// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/cutting/optimizer.hpp"
#include "fos/cutting/types.hpp"
#include "fos/cutting/validate.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace fos::cutting::test {

/// Job with one stock type "board" (unlimited), no trim, the given kerf.
Job makeJob(double sheetLength, double sheetWidth, double kerf);

/// Adds a piece; returns the job for chaining.
Job& addPiece(Job& job, const std::string& id, double length, double width, int quantity = 1, bool canRotate = true);

/// Runs optimize() and fails the test if it returns an error.
CuttingResult solve(const Job& job, const Options& options = {});

/// Runs the independent validator and fails the test with every finding it reports.
void expectValid(const Job& job, const CuttingResult& result);

/// True if `diagnostics` contains a finding with the given code.
bool hasCode(const core::Diagnostics& diagnostics, const std::string& code);

/// Number of placed copies of `pieceId` over all sheets.
int placedCount(const CuttingResult& result, const std::string& pieceId);

/// Own linear congruential generator so that test data is identical on every platform.
class TestLcg {
public:
    explicit TestLcg(std::uint32_t seed) : x_(seed) {}
    /// Uniform integer in [lo, hi].
    int range(int lo, int hi) {
        x_ = x_ * 1664525u + 1013904223u;
        return lo + static_cast<int>((x_ >> 8) % static_cast<std::uint32_t>(hi - lo + 1));
    }

private:
    std::uint32_t x_;
};

} // namespace fos::cutting::test
