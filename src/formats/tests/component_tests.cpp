// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/formats/component.hpp"
#include "test_support.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace fos;
using namespace fos::formats;
using core::ErrorCode;
using formats::test::TempDir;
using ::testing::HasSubstr;

namespace {

ComponentFile sampleFile() {
    ComponentRecord rec;
    rec.meta.id = "comp.base-600";
    rec.meta.name = "Base 600";
    rec.meta.category = "Base cabinets";
    rec.meta.tags = {"kitchen", "base"};
    rec.meta.previewSize = {600, 720, 560};
    domain::Material m;
    m.id = domain::MaterialId("mat.a");
    m.name = "A";
    m.thickness = 18;
    rec.library.materials.push_back(m);
    rec.definition = Json{{"type", "panel"}, {"id", "x"}};
    rec.extra["futureKey"] = 42;
    ComponentFile file;
    file.components.push_back(rec);
    return file;
}

TEST(ComponentFile, RoundTripsThroughDiskKeepingUnknownKeys) {
    TempDir dir;
    const auto path = dir / "a.foscomp";
    ASSERT_TRUE(exportComponentFile(path, sampleFile(), AppInfo{"FurnitureOS", "test"}).ok());
    auto loaded = importComponentFile(path);
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    ASSERT_EQ(loaded.value().components.size(), 1u);
    const auto& c = loaded.value().components[0];
    EXPECT_EQ(c.meta.name, "Base 600");
    EXPECT_EQ(c.meta.tags, (std::vector<std::string>{"kitchen", "base"}));
    EXPECT_EQ(c.meta.previewSize, (PreviewSize{600, 720, 560}));
    ASSERT_EQ(c.library.materials.size(), 1u);
    EXPECT_EQ(c.library.materials[0].thickness, 18.0);
    EXPECT_EQ(c.extra.at("futureKey"), 42);
    EXPECT_EQ(componentFileToJson(loaded.value()), componentFileToJson(sampleFile()));
}

TEST(ComponentFile, MalformedPayloadIsRefusedWithPointerAndFileName) {
    Json payload = componentFileToJson(sampleFile());
    payload["components"][0]["previewSize"]["width"] = -1;
    auto r = componentFileFromJson(payload, "/payload");
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("/payload/components/0/previewSize/width"));

    payload = componentFileToJson(sampleFile());
    payload["components"][0]["definition"]["type"] = "wardrobe";
    r = componentFileFromJson(payload);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("/components/0/definition/type"));

    payload = componentFileToJson(sampleFile());
    payload["components"].push_back(payload["components"][0]);
    r = componentFileFromJson(payload);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("/components/1/id"));

    payload["components"] = Json::array();
    EXPECT_FALSE(componentFileFromJson(payload).ok());

    TempDir dir;
    const auto bad = dir / "bad.foscomp";
    formats::test::writeText(bad, "{ not json");
    auto f = importComponentFile(bad);
    ASSERT_FALSE(f.ok());
    EXPECT_THAT(f.error().message, HasSubstr(bad.string()));
}

TEST(ComponentFile, OtherContainerKindIsRefused) {
    Document doc;
    doc.format = "furnitureos-library";
    doc.payload = componentFileToJson(sampleFile());
    auto text = serializeDocument(doc);
    ASSERT_TRUE(text.ok());
    auto r = importComponentFileText(text.value(), "lib.foslib");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::Unsupported);
    EXPECT_THAT(r.error().message, HasSubstr("lib.foslib"));
}

} // namespace
