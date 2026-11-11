// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/machining/patterns.hpp"

#include <cmath>

namespace fos::machining {
namespace {
using domain::Operation;
using geometry::Vec2;

void emit(std::vector<Operation>& out, const HoleSpec& s, Vec2 p, const std::string& prefix,
          const std::string& source) {
    Operation op;
    op.id = domain::OperationId(prefix + "." + std::to_string(out.size() + 1));
    op.shape = domain::Hole{s.face, p.x, p.y, s.diameter, s.through ? 0.0 : s.depth, s.through};
    op.source = source;
    out.push_back(std::move(op));
}
} // namespace

std::vector<Operation> lineOfHoles(const HoleSpec& spec, Vec2 start, Vec2 pitch, int count,
                                   const std::string& idPrefix, const std::string& source) {
    std::vector<Operation> out;
    for (int i = 0; i < count; ++i) emit(out, spec, start + pitch * static_cast<double>(i), idPrefix, source);
    return out;
}

std::vector<Operation> system32Row(const System32Row& row, const std::string& idPrefix, const std::string& source) {
    std::vector<Operation> out;
    const double eps = core::kLengthEps;
    if (!(row.pitch > 0) || row.rangeMax < row.rangeMin) return out;
    // Smallest k >= 0 with firstHoleOffset + k*pitch >= rangeMin.
    double kStart = std::ceil((row.rangeMin - row.firstHoleOffset - eps) / row.pitch);
    if (kStart < 0) kStart = 0;
    for (double k = kStart;; k += 1.0) {
        const double along = row.firstHoleOffset + k * row.pitch;
        if (along > row.rangeMax + eps) break;
        const Vec2 p = row.direction == RowDirection::AlongU ? Vec2{along, row.rowOffset} : Vec2{row.rowOffset, along};
        emit(out, row.hole, p, idPrefix, source);
    }
    return out;
}

std::vector<Operation> rectangularGrid(const HoleSpec& spec, Vec2 start, double pitchU, double pitchV, int countU,
                                       int countV, const std::string& idPrefix, const std::string& source) {
    std::vector<Operation> out;
    if (countU <= 0 || countV <= 0) return out;
    if ((countU > 1 && !(pitchU > 0)) || (countV > 1 && !(pitchV > 0))) return out;
    for (int j = 0; j < countV; ++j)
        for (int i = 0; i < countU; ++i)
            emit(out, spec, {start.x + pitchU * i, start.y + pitchV * j}, idPrefix, source);
    return out;
}

std::vector<Operation> mirroredPairs(const HoleSpec& spec, const std::vector<Vec2>& positions, MirrorLine line,
                                     double position, const std::string& idPrefix, const std::string& source) {
    std::vector<Operation> out;
    for (const Vec2 p : positions) {
        const Vec2 m = line == MirrorLine::ConstantU ? Vec2{2 * position - p.x, p.y} : Vec2{p.x, 2 * position - p.y};
        emit(out, spec, p, idPrefix, source);
        if (!approxEqual(p, m)) emit(out, spec, m, idPrefix, source);
    }
    return out;
}

} // namespace fos::machining
