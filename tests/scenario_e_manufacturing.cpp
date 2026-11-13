// SPDX-License-Identifier: AGPL-3.0-only
// Scenario E - manufacturing: a generated part with drilling operations -> machining
// validation clean -> CAM programs for face A and face B -> independent simulation clean ->
// postprocessor output carries the UNVERIFIED header and verifiedForMachine == false; edge
// holes are reported as unsupported, never silently dropped. Uses the FICTIONAL demo machine
// and tools of the CLI (fos/cli/demo_cam.hpp: "demo profile — not a real machine").
//
// Part: left side of a base cabinet 800 x 720 x 560 (720 x 560 x 18). Its holes (production
// machining report): face A - 9 confirmat through holes 7 mm (joints of bottom 3, front rail 2,
// rear rail 2, plinth 2); face B - 6 holes 5 mm (2 hinge plates x 2 screws + 2 shelf pins).
// Expected machine positions come from the setup convention (cam/setup.hpp), written here
// independently: stock min corner o = (20, 20, 0), W = 560, T = 18,
//   face A up: (u, v) -> (o.x + u, o.y + v);   face B up: (u, v) -> (o.x + u, o.y + W - v)
//   floor Z: through hole -> stock bottom - 0.5 breakthrough = -0.5; blind -> T - depth.
#include "scenario_support.hpp"

#include "fos/cam/generate.hpp"
#include "fos/cam/generic_iso.hpp"
#include "fos/cam/simulate.hpp"
#include "fos/cli/demo_cam.hpp"
#include "fos/machining/validation.hpp"

#include <cstdio>

namespace fos::scenario {
namespace {

project::Resolved baseCabinet800x720() {
    project::Document doc(project::newProject(meta("Scenario E")));
    EXPECT_TRUE(doc.execute(project::commands::addItem(templateItem(&furniture::templates::baseCabinet, "cab1"))).ok());
    EXPECT_TRUE(doc.execute(project::commands::setParameter("cab1", "width", 800)).ok());
    EXPECT_TRUE(doc.execute(project::commands::setParameter("cab1", "height", 720)).ok());
    return resolved(doc.project());
}

std::string g1Line(double x, double y, double z) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "\nG1 X%.3f Y%.3f Z%.3f F", x, y, z);
    return buf;
}

struct FaceRun {
    cam::CamResult generated;
    cam::PostOutput post;
};

FaceRun runFace(const domain::Part& part, domain::Face face) {
    const auto machine = cli::demoMachineProfile();
    const auto tools = cli::demoToolLibrary();
    const cam::Setup setup{face, cli::demoStockOrigin()};
    auto g = cam::generateProgram(part, setup, machine, tools);
    EXPECT_TRUE(g.ok()) << (g.ok() ? "" : g.error().message);
    if (!g.ok()) return {};
    const auto sim = cam::simulate(g.value().program, part, setup, machine, tools);
    EXPECT_FALSE(sim.hasErrors());
    for (const auto& d : sim.diagnostics) ADD_FAILURE() << "simulation: " << d.code << ": " << d.message;
    const cam::GenericIsoPostprocessor post;
    auto out = post.generate(cam::PostInput{g.value().program, part, setup, machine, tools});
    EXPECT_TRUE(out.ok()) << (out.ok() ? "" : out.error().message);
    if (!out.ok()) return {g.value(), {}};
    return {g.value(), out.value()};
}

void expectHolesAt(const domain::Part& part, const FaceRun& run, domain::Face face) {
    const double ox = 20, oy = 20, w = part.width, t = part.thickness;
    int checked = 0;
    for (const auto& planned : run.generated.plan.machinableNow) {
        const auto& op = part.operations[planned.index];
        const auto* hole = std::get_if<domain::Hole>(&op.shape);
        ASSERT_NE(hole, nullptr) << op.id.str();
        ASSERT_EQ(hole->face, face);
        const double x = ox + hole->u;
        const double y = face == domain::Face::A ? oy + hole->v : oy + w - hole->v;
        const double z = hole->through ? -0.5 : t - hole->depth;
        EXPECT_TRUE(contains(run.post.text, g1Line(x, y, z))) << op.id.str() << " expected" << g1Line(x, y, z);
        ++checked;
    }
    EXPECT_GT(checked, 0);
}

TEST(ScenarioE, SidePanelFaceAAndFaceBPrograms) {
    const auto r = baseCabinet800x720();
    const domain::Part* side = findPart(r, "cab1/side.left");
    ASSERT_NE(side, nullptr);
    ASSERT_EQ(side->operations.size(), 15u); // 9 on A + 6 on B
    EXPECT_FALSE(core::hasErrors(machining::validateOperations(*side)));

    const FaceRun a = runFace(*side, domain::Face::A);
    EXPECT_EQ(a.generated.plan.machinableNow.size(), 9u);
    EXPECT_EQ(a.generated.plan.needsOtherSetup.size(), 6u);
    EXPECT_TRUE(a.generated.plan.unsupported.empty());
    const FaceRun b = runFace(*side, domain::Face::B);
    EXPECT_EQ(b.generated.plan.machinableNow.size(), 6u);
    EXPECT_EQ(b.generated.plan.needsOtherSetup.size(), 9u);

    for (const FaceRun* run : {&a, &b}) {
        const std::string& text = run->post.text;
        EXPECT_FALSE(run->post.verifiedForMachine);
        EXPECT_FALSE(run->post.reviewWarnings.empty());
        EXPECT_EQ(text.rfind("(=====", 0), 0u); // header first
        for (const char* s : {"UNVERIFIED", "NOT VERIFIED AGAINST ANY CONTROLLER", "PROFILE NOT VERIFIED",
                              "demo profile — not a real machine"})
            EXPECT_TRUE(contains(text, s)) << s;
        EXPECT_LT(text.find("QUALIFIED OPERATOR"), text.find("G21"));
        EXPECT_EQ(text.substr(text.size() - 4), "M30\n");
    }
    EXPECT_TRUE(contains(a.post.text, "SETUP: FACE A UP"));
    EXPECT_TRUE(contains(b.post.text, "SETUP: FACE B UP"));
    expectHolesAt(*side, a, domain::Face::A);
    expectHolesAt(*side, b, domain::Face::B);
    // Face A holes are 7 mm confirmat through holes -> demo drill T4 (7 mm); face B 5 mm -> T3.
    EXPECT_TRUE(contains(a.post.text, "\nT4 M6\n"));
    EXPECT_TRUE(contains(b.post.text, "\nT3 M6\n"));
}

TEST(ScenarioE, EdgeHolesAreReportedUnsupported) {
    const auto r = baseCabinet800x720();
    // The bottom has only confirmat holes in its end edges W1 / W2 (3 each).
    const domain::Part* bottom = findPart(r, "cab1/bottom");
    ASSERT_NE(bottom, nullptr);
    auto plan = cam::planSetup(*bottom, {domain::Face::A, cli::demoStockOrigin()});
    ASSERT_TRUE(plan.ok());
    EXPECT_TRUE(plan.value().machinableNow.empty());
    ASSERT_EQ(plan.value().unsupported.size(), 6u);
    for (const auto& u : plan.value().unsupported) EXPECT_TRUE(contains(u.reason, "edge face")) << u.reason;

    // An extra edge hole on the side: still listed (unsupported + warning), absent from the
    // program, while the 9 face-A holes are machined.
    domain::Part side = *findPart(r, "cab1/side.left");
    domain::Operation edge;
    edge.id = domain::OperationId("test/edge.hole");
    edge.shape = domain::Hole{domain::Face::L1, 100, 9, 8, 30, false};
    side.operations.push_back(edge);
    EXPECT_FALSE(core::hasErrors(machining::validateOperations(side)));
    const FaceRun a = runFace(side, domain::Face::A);
    ASSERT_EQ(a.generated.plan.unsupported.size(), 1u);
    EXPECT_EQ(a.generated.plan.unsupported[0].id.str(), "test/edge.hole");
    EXPECT_EQ(a.generated.plan.machinableNow.size(), 9u);
    bool warned = false;
    for (const auto& d : a.generated.diagnostics)
        warned = warned || (d.code == "cam.setup.unsupported" && contains(d.message, "test/edge.hole"));
    EXPECT_TRUE(warned);
    EXPECT_FALSE(contains(a.post.text, "test/edge.hole"));
}

TEST(ScenarioE, PostprocessorRefusesASimulationError) {
    const auto r = baseCabinet800x720();
    const domain::Part* side = findPart(r, "cab1/side.left");
    ASSERT_NE(side, nullptr);
    auto machine = cli::demoMachineProfile();
    machine.envelopeX = {0, 300}; // the 720 mm side does not fit -> cam.sim.envelope errors
    const auto tools = cli::demoToolLibrary();
    const cam::Setup setup{domain::Face::A, cli::demoStockOrigin()};
    auto g = cam::generateProgram(*side, setup, machine, tools);
    ASSERT_TRUE(g.ok()) << g.error().message;
    const auto sim = cam::simulate(g.value().program, *side, setup, machine, tools);
    EXPECT_TRUE(sim.hasErrors());
    const cam::GenericIsoPostprocessor post;
    auto out = post.generate(cam::PostInput{g.value().program, *side, setup, machine, tools});
    ASSERT_FALSE(out.ok());
    EXPECT_TRUE(contains(out.error().message, "cam.sim.envelope"));
}

} // namespace
} // namespace fos::scenario
