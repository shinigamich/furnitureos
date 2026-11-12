// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Internal: deterministic bottom-left-fill decoder (one complete layout for one part order).
#include "shapes.hpp"

#include <array>
#include <map>
#include <vector>

namespace fos::nesting::detail {

struct Instance {
    int part = 0;
    int instance = 0;
};

struct PlacedItem {
    int part = 0;
    int orient = 0;
    Vec2 pos;          ///< Bounding-box min of the placed outline (sheet frame).
    Polygon2 outline;  ///< transformOutline(original, rot, mirrored, pos - offset).
    Rect2 box;
    Instance inst;
};

struct OpenSheet {
    int stock = 0;
    double freeArea = 0;
    std::vector<PlacedItem> items;
    /// failed[part]: an instance of this part did not fit. Sheets only fill up, so it never will.
    std::vector<bool> failed;
};

struct Decoded {
    std::vector<OpenSheet> sheets;
    std::vector<Instance> unplaced;
    bool aborted = false;
};

/// Lexicographic quality of a layout: (unplaced instances, sheets used, used length of the
/// last sheet). Smaller is better.
struct Score {
    int unplaced = 0;
    int sheets = 0;
    double lastLength = 0;
    bool betterThan(const Score& o) const noexcept;
};
Score scoreOf(const Decoded& d);

class Placer {
public:
    Placer(const Job& job, const std::vector<Shape>& shapes, const NestOptions& options);
    /// Places the instances in the given order. Stops (aborted = true) when isCancelled fires;
    /// cancellation is sticky for the lifetime of the Placer.
    Decoded decode(const std::vector<Instance>& order);
    /// fitsEmpty[part][stock]: some orientation's bounding box fits the empty placement area.
    const std::vector<std::vector<bool>>& fitsEmpty() const noexcept { return fitsEmpty_; }

private:
    struct Nfp {
        Polygon2 raw;   ///< Q (+) (-P): positions of P's bbox-min where the pieces overlap.
        Polygon2 grown; ///< raw miter-inflated by the part spacing (== raw for spacing 0).
        Rect2 box;      ///< bounds(grown).
    };
    const std::vector<Nfp>& nfps(int partQ, int orientQ, int partP, int orientP);
    bool tryPlace(OpenSheet& sheet, int part, PlacedItem& out);
    bool cancelled();
    bool positionValid(const Polygon2& outline, const Rect2& box, const OpenSheet& sheet) const;

    const Job& job_;
    const std::vector<Shape>& shapes_;
    const NestOptions& options_;
    std::vector<Rect2> areas_;
    std::vector<std::vector<bool>> fitsEmpty_;
    std::map<std::array<int, 4>, std::vector<Nfp>> cache_;
    bool cancelled_ = false; ///< Sticky once isCancelled returned true.
};

} // namespace fos::nesting::detail
