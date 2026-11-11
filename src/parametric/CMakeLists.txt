# SPDX-License-Identifier: AGPL-3.0-only
fos_add_module(parametric
    SOURCES src/expression.cpp src/expression_parser.cpp src/graph_state.cpp src/graph_commit.cpp
            src/parameter_graph.cpp src/scope.cpp
    DEPS fos::core)
fos_add_tests(parametric
    SOURCES tests/expression_tests.cpp tests/parameter_graph_tests.cpp tests/transaction_constraint_tests.cpp
            tests/snapshot_load_scope_tests.cpp tests/scale_tests.cpp)
