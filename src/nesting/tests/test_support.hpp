// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/nesting/nest.hpp"
#include "fos/nesting/validate.hpp"

#include <gtest/gtest.h>

#include <initializer_list>
#include <string>

namespace fos::nesting::test {

geometry::Polygon2 poly(std::initializer_list<geometry::Vec2> pts);
geometry::Polygon2 rectPoly(double w, double h);
Part makePart(std::string id, geometry::Polygon2 outline, int quantity, RotationRule rule = RotationRule::None);
Sheet makeSheet(std::string id, double length, double width, int quantity = kUnlimited);

/// Runs nest(), asserts success and that validateNest() reports nothing; returns the result.
NestResult nestValid(const Job& job, const NestOptions& options = {});
/// Minimum outline distance over all pairs of placements on one sheet (+inf for < 2 parts).
double minPairDistance(const SheetLayout& sheet);
/// Sum of the diagnostics' codes, for readable failure output.
std::string codes(const core::Diagnostics& ds);

} // namespace fos::nesting::test
