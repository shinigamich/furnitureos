// SPDX-License-Identifier: AGPL-3.0-only
#include "benchmark_jobs.hpp"
#include "test_support.hpp"

#include <cstdio>

using namespace fos::nesting;
using namespace fos::nesting::test;

TEST(NestBenchmark, Baseline) {
    for (const auto& b : benchmarkJobs()) {
        NestOptions opt;
        opt.iterations = 5;
        const NestResult r = nestValid(b.job, opt);
        std::printf("BASE %s sheets=%zu util=%.4f unplaced=%zu last=%.1f\n", b.name.c_str(), r.sheets.size(),
                    r.utilisation, r.unplaced.size(), r.sheets.empty() ? 0.0 : r.sheets.back().usedLength);
    }
}
