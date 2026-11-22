// SPDX-License-Identifier: AGPL-3.0-only
// fos packing / fos assembly
#include "cli_test_support.hpp"

#include "fos/formats/json.hpp"
#include "fos/project/file.hpp"

namespace fos::cli::test {
namespace {

/// Gives every library material a density (kg/m3) and saves.
void setDensities(const std::filesystem::path& file, double density) {
    auto loaded = project::loadProject(file);
    ASSERT_TRUE(loaded) << loaded.error().message;
    project::Project p = loaded.value().project;
    for (auto& m : p.libraries.materials) m.density = density;
    formats::SaveOptions so;
    so.backupCount = 0;
    ASSERT_TRUE(project::saveProject(file, p, so));
}

std::size_t countFiles(const std::filesystem::path& dir, const std::string& prefix) {
    std::size_t n = 0;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.path().filename().string().rfind(prefix, 0) == 0 && e.path().extension() == ".svg") ++n;
    return n;
}

TEST(CliPacking, WithoutDensityWeightsAreUnknownNotInvented) {
    TempDir dir;
    const std::string file = dir.file("k.fosp");
    ASSERT_EQ(newBaseCabinet(file).code, 0);
    EXPECT_FALSE(contains(readFile(dir.path / "k.fosp"), "\"density\"")); // written only when set
    const auto r = fos({"packing", file});
    EXPECT_EQ(r.code, 0) << r.out << r.err;
    EXPECT_TRUE(contains(r.out, "weight unknown")) << r.out;
    EXPECT_TRUE(contains(r.out, "production.packing.density_unknown")) << r.out;
    EXPECT_TRUE(contains(r.out, "Hardware bag")) << r.out;
    EXPECT_FALSE(contains(r.out, " kg\n    #")) << r.out; // no package line ends with a weight
}

TEST(CliPacking, DensityRoundTripJsonCsvAndLabels) {
    TempDir dir;
    const std::string file = dir.file("k.fosp");
    ASSERT_EQ(newBaseCabinet(file).code, 0);
    setDensities(dir.path / "k.fosp", 650);
    EXPECT_TRUE(contains(readFile(dir.path / "k.fosp"), "\"density\": 650"));

    auto r = fos({"packing", file, "--format", "json", "--max-kg", "10"});
    ASSERT_EQ(r.code, 0) << r.err;
    const auto j = formats::Json::parse(r.out);
    EXPECT_EQ(j.at("maxPackageWeightKg"), 10.0);
    const auto& asm0 = j.at("assemblies").at(0);
    double packageSum = 0, partSum = 0;
    std::size_t parts = 0;
    for (const auto& p : asm0.at("packages")) {
        ASSERT_FALSE(p.at("weightKg").is_null());
        const double w = p.at("weightKg").get<double>();
        if (!p.at("overweight").get<bool>()) {
            EXPECT_LE(w, 10.0 + 1e-6);
        }
        packageSum += w;
        for (const auto& part : p.at("parts")) {
            ++parts;
            partSum += part.at("weightKg").get<double>();
            // weight = L * W * T * 650 kg/m3 (rectangular parts of the base template), rounded to 3 decimals.
            const double expected = part.at("length").get<double>() * part.at("width").get<double>() *
                                    part.at("thickness").get<double>() * 1e-9 * 650.0;
            EXPECT_NEAR(part.at("weightKg").get<double>(), expected, 0.0006) << part.dump();
        }
    }
    EXPECT_GE(asm0.at("packages").size(), 2u);
    EXPECT_GT(parts, 0u);
    EXPECT_NEAR(packageSum, partSum, 0.01);

    // A higher limit never needs more packages.
    r = fos({"packing", file, "--format", "json", "--max-kg", "40"});
    ASSERT_EQ(r.code, 0);
    EXPECT_LE(formats::Json::parse(r.out).at("assemblies").at(0).at("packages").size(), asm0.at("packages").size());

    r = fos({"packing", file, "--format", "csv", "--svg", dir.file("labels")});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(contains(r.out, "Assembly,Package,Package size (mm),Package weight (kg),No.,Item,Material,L,W,T,Quantity,"
                                "Weight (kg)"))
        << r.out;
    EXPECT_EQ(countFiles(dir.path / "labels", "package-labels-"), 1u);
    const std::string svg = readFile(dir.path / "labels" / "package-labels-1.svg");
    EXPECT_TRUE(contains(svg, "Package 1/")) << svg;
    EXPECT_TRUE(contains(svg, "Kitchen")) << svg;
}

TEST(CliPacking, BadArguments) {
    TempDir dir;
    const std::string file = dir.file("k.fosp");
    ASSERT_EQ(newBaseCabinet(file).code, 0);
    EXPECT_EQ(fos({"packing", file, "--max-kg", "abc"}).code, 2);
    EXPECT_EQ(fos({"packing", file, "--max-kg", "0"}).code, 2);
    EXPECT_EQ(fos({"packing", file, "--padding", "-1"}).code, 2);
    EXPECT_EQ(fos({"packing", file, "--format", "xml"}).code, 2);
    EXPECT_EQ(fos({"packing"}).code, 2);
    EXPECT_EQ(fos({"packing", dir.file("missing.fosp")}).code, 2);
    EXPECT_EQ(fos({"packing", "--help"}).code, 0);
}

TEST(CliAssembly, TextAndSvgSheets) {
    TempDir dir;
    const std::string file = dir.file("k.fosp");
    ASSERT_EQ(newBaseCabinet(file).code, 0);
    const auto r = fos({"assembly", file, "--svg", dir.file("sheets")});
    EXPECT_EQ(r.code, 0) << r.out << r.err;
    EXPECT_TRUE(contains(r.out, "Assembly sheet: ")) << r.out;
    EXPECT_TRUE(contains(r.out, "Generic step order")) << r.out;
    EXPECT_TRUE(contains(r.out, "Step 1: Carcass")) << r.out;
    // Carcass comes before the back panel in the generic order.
    EXPECT_LT(r.out.find("Step 1: Carcass"), r.out.find(": Back panel")) << r.out;
    EXPECT_EQ(countFiles(dir.path / "sheets", "assembly-"), 1u);
    EXPECT_EQ(fos({"assembly"}).code, 2);
    EXPECT_EQ(fos({"assembly", file, "--bogus"}).code, 2);
    EXPECT_TRUE(contains(fos({"--help"}).out + fos({"--help"}).err, "packing"));
}

} // namespace
} // namespace fos::cli::test
