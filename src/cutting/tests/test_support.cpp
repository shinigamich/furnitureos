// SPDX-License-Identifier: AGPL-3.0-only
#include "test_support.hpp"

namespace fos::cutting::test {

Job makeJob(double sheetLength, double sheetWidth, double kerf) {
    Job job;
    job.id = "job";
    job.name = "Test board";
    job.kerf = kerf;
    StockSheet s;
    s.id = "board";
    s.length = sheetLength;
    s.width = sheetWidth;
    job.stock.push_back(s);
    return job;
}

Job& addPiece(Job& job, const std::string& id, double length, double width, int quantity, bool canRotate) {
    Piece p;
    p.id = id;
    p.label = id;
    p.length = length;
    p.width = width;
    p.quantity = quantity;
    p.canRotate = canRotate;
    job.pieces.push_back(p);
    return job;
}

CuttingResult solve(const Job& job, const Options& options) {
    auto r = optimize(job, options);
    if (!r.ok()) {
        ADD_FAILURE() << "optimize failed: " << r.error().message;
        return {};
    }
    return std::move(r).value();
}

void expectValid(const Job& job, const CuttingResult& result) {
    const core::Diagnostics ds = validateLayout(job, result);
    for (const auto& d : ds) ADD_FAILURE() << "validator: [" << d.code << "] " << d.message;
    EXPECT_FALSE(result.isProvenOptimal);
}

bool hasCode(const core::Diagnostics& diagnostics, const std::string& code) {
    for (const auto& d : diagnostics)
        if (d.code == code) return true;
    return false;
}

int placedCount(const CuttingResult& result, const std::string& pieceId) {
    int n = 0;
    for (const auto& s : result.sheets)
        for (const auto& p : s.placements)
            if (p.pieceId == pieceId) ++n;
    return n;
}

} // namespace fos::cutting::test
