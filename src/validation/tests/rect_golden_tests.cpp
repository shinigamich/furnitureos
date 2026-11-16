// SPDX-License-Identifier: AGPL-3.0-only
/// Regression proof for ADR-0008 (phase 12): validation of rectangular models must stay
/// byte-for-byte identical to the output recorded BEFORE contoured-part support was added
/// (tests/golden/rect_validation.txt, generated with the phase-11 code).
/// Regenerate only deliberately: FOS_UPDATE_GOLDEN=1 ./fos_validation_tests
#include "fos/machining/validation.hpp"
#include "test_support.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace fos;
using namespace fos::validation;
using namespace fos::validation::test;

namespace {

std::string dump(const std::string& title, const domain::Model& m) {
    const ValidationReport r = validateModel(m);
    std::string s = "# " + title + "\n" + toText(r) + toMarkdown(r);
    const ValidationStats& st = r.stats;
    s += "stats " + std::to_string(st.checksRun) + " " + std::to_string(st.possiblePartPairs) + " " +
         std::to_string(st.broadPhaseChecks) + " " + std::to_string(st.partPairsTested) + " " +
         std::to_string(st.grooveEngagementsAllowed) + " " + std::to_string(st.possibleCrossAssemblyPartPairs) + " " +
         std::to_string(st.crossAssemblyBroadPhaseChecks) + " " + std::to_string(st.crossAssemblyPartPairsTested) +
         " " + std::to_string(st.matingHolePairs) + "\n";
    for (const auto& a : m.assemblies)
        for (const auto& p : a.parts)
            for (const auto& d : machining::validateOperations(p))
                s += "machining " + d.code + " | " + d.message + "\n";
    return s;
}

std::string dumpAll() {
    std::string s;
    domain::Model basic = domain::fixtures::basicCabinet();
    s += dump("basicCabinet", basic);

    // Shelf pushed 30 mm to the left into the left side, top raised into nothing, a rotated board.
    domain::Model bad = basic;
    part(bad, "cab1/shelf.1").placement = geometry::Transform::translation({-12, 20, 351});
    part(bad, "cab1/top").placement = geometry::Transform::translation({18, 0, 690});
    s += dump("interfering cabinet", bad);

    // Holes near / beyond the edges and out of bounds, a deep groove, a pocket.
    domain::Part p = board("p", 600, 400);
    domain::Operation h1, h2, h3, g, pk;
    h1.id = domain::OperationId("h1");
    h1.shape = domain::Hole{domain::Face::A, 4.0, 200.0, 8.0, 10.0, false};
    h2.id = domain::OperationId("h2");
    h2.shape = domain::Hole{domain::Face::A, 700.0, 200.0, 8.0, 10.0, false};
    h3.id = domain::OperationId("h3");
    h3.shape = domain::Hole{domain::Face::L1, 300.0, 9.0, 8.0, 30.0, false};
    g.id = domain::OperationId("g");
    g.shape = domain::Groove{domain::Face::B, 0.0, 380.0, 600.0, 380.0, 8.0, 20.0};
    domain::Pocket pocket;
    pocket.face = domain::Face::A;
    pocket.contour = {{100, 100}, {200, 100}, {200, 150}, {100, 150}};
    pocket.depth = 5;
    pk.id = domain::OperationId("pk");
    pk.shape = pocket;
    p.operations = {h1, h2, h3, g, pk};
    domain::Part q = board("q", 300, 300, geometry::Transform::translation({500, 100, 10}));
    domain::Part r = board("r", 300, 300,
                           geometry::Transform::fromAxes({0.6, 0.8, 0}, {-0.8, 0.6, 0}, {100, 100, -5}));
    s += dump("boards", libraryModel({p, q, r}));
    return s;
}

} // namespace

TEST(RectangularGolden, ValidationUnchangedByContourSupport) {
    const std::string path = std::string(FOS_VALIDATION_GOLDEN_DIR) + "/rect_validation.txt";
    const std::string now = dumpAll();
    if (const char* u = std::getenv("FOS_UPDATE_GOLDEN"); u && std::string(u) == "1") {
        std::ofstream(path, std::ios::binary) << now;
        GTEST_SKIP() << "golden file rewritten: " << path;
    }
    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(in.good()) << "missing golden file " << path;
    std::stringstream buf;
    buf << in.rdbuf();
    EXPECT_EQ(buf.str(), now);
}
