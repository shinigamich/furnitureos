# SPDX-License-Identifier: AGPL-3.0-only
# 1D cutting-stock optimisation (bars, profiles, edge-band rolls). Depends only on fos::core.
fos_add_module(linear
    SOURCES src/check.cpp src/optimize.cpp src/validate.cpp src/svg.cpp
    DEPS fos::core)
fos_add_tests(linear
    SOURCES tests/linear_tests.cpp)
