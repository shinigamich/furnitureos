// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Fixed benchmark jobs for nesting quality (sheets used / utilisation). Shared by the benchmark tests.
#include "fos/nesting/types.hpp"

#include <string>
#include <vector>

namespace fos::nesting::test {

struct BenchmarkJob {
    std::string name;
    Job job;
};

/// The fixed benchmark set (stable order). See benchmark_jobs.cpp for the shapes and their areas.
std::vector<BenchmarkJob> benchmarkJobs();

} // namespace fos::nesting::test
