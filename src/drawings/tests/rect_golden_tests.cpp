// SPDX-License-Identifier: AGPL-3.0-only
// Golden regression for RECTANGULAR parts (phase 12, ADR-0008: "rectangular parts must behave
// exactly as before"). The expected hashes were recorded from the drawings module BEFORE contoured
// parts were supported (build of the unmodified phase-11 sources on fixtures::basicCabinet()).
// Any change in the generated SVG/DXF text of the fixture - one coordinate digit, one primitive
// more or less, a reordering - changes the FNV-1a hash. If the FIXTURE itself is changed on purpose,
// re-record the values (the test prints the actual numbers).
#include "fos/drawings/dxf.hpp"
#include "fos/drawings/svg.hpp"
#include "test_util.hpp"

#include <cstdint>

using namespace fos;
using namespace fos::drawings;

namespace {

std::uint64_t fnv1a(const std::string& s) {
    std::uint64_t h = 1469598103934665603ull;
    for (const unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

struct Golden {
    std::uint64_t svg = 0;   ///< Hash over the SVG of every sheet, concatenated.
    std::uint64_t dxf = 0;   ///< Hash over the model-space DXF of every view, concatenated.
    std::size_t primitives = 0; ///< Total primitive count over all sheets.
};

Golden digest(const DrawingResult& r) {
    std::string svg, dxf;
    Golden g;
    for (const Sheet& s : r.sheets) {
        svg += toSvg(s);
        g.primitives += s.primitives.size();
    }
    for (const GeneratedView& v : r.views) dxf += viewToDxf(v.view, v.annotations);
    g.svg = fnv1a(svg);
    g.dxf = fnv1a(dxf);
    return g;
}

} // namespace

TEST(RectangularGolden, AssemblyDrawingUnchanged) {
    const domain::Model m = domain::fixtures::basicCabinet();
    AssemblyDrawingOptions o;
    o.sheet.title.date = "2026-01-01";
    o.sections.push_back({ViewKind::Left, 400.0, {}});
    o.sections.push_back({ViewKind::Top, 360.0, {}});
    const auto r = generateAssemblyDrawing(m, m.assemblies[0].id, o);
    ASSERT_TRUE(r.ok());
    const Golden g = digest(r.value());
    std::printf("assembly svg=%llu dxf=%llu prims=%zu\n", static_cast<unsigned long long>(g.svg),
                static_cast<unsigned long long>(g.dxf), g.primitives);
    EXPECT_EQ(g.svg, 15490637936474628240ull);
    EXPECT_EQ(g.dxf, 865092947202512816ull);
    EXPECT_EQ(g.primitives, 831u);
}

TEST(RectangularGolden, PartDrawingsUnchanged) {
    const domain::Model m = domain::fixtures::basicCabinet();
    std::string all;
    std::size_t prims = 0;
    std::string dxf;
    for (const domain::Part& p : m.assemblies[0].parts) {
        for (const PartViewFace face : {PartViewFace::A, PartViewFace::B}) {
            PartDrawingOptions o;
            o.sheet.title.date = "2026-01-01";
            o.view.face = face;
            const auto r = generatePartDrawing(m, p.id, o);
            ASSERT_TRUE(r.ok()) << p.id.str();
            const Golden g = digest(r.value());
            all += std::to_string(g.svg) + "/" + std::to_string(g.dxf) + ";";
            prims += g.primitives;
        }
    }
    const std::uint64_t h = fnv1a(all);
    std::printf("parts hash=%llu prims=%zu\n", static_cast<unsigned long long>(h), prims);
    EXPECT_EQ(h, 8972437443029511878ull);
    EXPECT_EQ(prims, 792u);
}
