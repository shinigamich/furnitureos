// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fos/parametric/parameter_graph.hpp"

#define FOS_ASSERT_OK(expr)                                         \
    do {                                                            \
        const auto& fosResult_ = (expr);                            \
        ASSERT_TRUE(fosResult_.ok()) << fosResult_.error().message; \
    } while (false)

namespace fos::parametric::test {

/// Value of a parameter; fails the test if it does not exist.
inline double valueOf(const ParameterGraph& g, const std::string& name) {
    const auto v = g.value(name);
    EXPECT_TRUE(v.ok()) << name;
    return v.valueOr(-12345.0);
}

/// The canonical furniture example: 800 mm cabinet with two 18 mm sides -> 764 mm inner width.
inline ParameterGraph makeCabinet() {
    ParameterGraph g;
    EXPECT_TRUE(g.defineConstant("outer_width", 800, ParameterKind::Length).ok());
    EXPECT_TRUE(g.defineConstant("left_thickness", 18, ParameterKind::Length).ok());
    EXPECT_TRUE(g.defineConstant("right_thickness", 18, ParameterKind::Length).ok());
    EXPECT_TRUE(g.defineExpression("inner_width", "outer_width - left_thickness - right_thickness",
                                   ParameterKind::Length)
                    .ok());
    return g;
}

} // namespace fos::parametric::test
