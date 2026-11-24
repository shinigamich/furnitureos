// SPDX-License-Identifier: AGPL-3.0-only
// ProjectSettings::room: codec (optional, byte-identical when absent), validation, setRoom undo.
#include "test_support.hpp"

#include "fos/project/codec.hpp"

#include <cmath>

namespace fos::project::test {
namespace {

RoomSettings kitchen() {
    RoomSettings r;
    r.width = 3600;
    r.depth = 2750.5;
    r.height = 2500;
    r.wallThickness = 120;
    r.origin = {-100, 250};
    return r;
}

TEST(Room, AbsentRoomIsNotWritten) {
    Project p = newProject(meta());
    const auto j = projectToJson(p);
    ASSERT_TRUE(j.contains("settings"));
    EXPECT_FALSE(j["settings"].contains("room"));
    // Removing a room restores the exact JSON of a project that never had one.
    Project withRoom = p;
    withRoom.settings.room = kitchen();
    withRoom.settings.room.reset();
    EXPECT_EQ(projectToJson(withRoom).dump(), j.dump());
}

TEST(Room, RoundTripsAllFields) {
    Project p = newProject(meta());
    p.settings.room = kitchen();
    const auto j = projectToJson(p);
    const auto& room = j["settings"]["room"];
    EXPECT_EQ(room["width"], 3600.0);
    EXPECT_EQ(room["depth"], 2750.5);
    EXPECT_EQ(room["height"], 2500.0);
    EXPECT_EQ(room["wallThickness"], 120.0);
    auto back = projectFromJson(j);
    ASSERT_TRUE(back.ok()) << back.error().message;
    ASSERT_TRUE(back.value().settings.room.has_value());
    EXPECT_EQ(*back.value().settings.room, kitchen());
    EXPECT_EQ(projectToJson(back.value()).dump(), j.dump());

    // Height is optional: not written when unset, read back as nullopt.
    p.settings.room->height.reset();
    const auto j2 = projectToJson(p);
    EXPECT_FALSE(j2["settings"]["room"].contains("height"));
    auto back2 = projectFromJson(j2);
    ASSERT_TRUE(back2.ok()) << back2.error().message;
    EXPECT_FALSE(back2.value().settings.room->height.has_value());
}

TEST(Room, InvalidRoomInFileIsRejected) {
    Project p = newProject(meta());
    p.settings.room = kitchen();
    auto j = projectToJson(p);
    j["settings"]["room"]["width"] = 0;
    auto r = projectFromJson(j);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("width"), std::string::npos) << r.error().message;

    j = projectToJson(p);
    j["settings"]["room"].erase("depth"); // required
    EXPECT_FALSE(projectFromJson(j).ok());

    j = projectToJson(p);
    j["settings"]["room"]["colour"] = "white"; // closed object
    EXPECT_FALSE(projectFromJson(j).ok());

    j = projectToJson(p);
    j["settings"]["room"]["width"] = 200000; // > 100 m
    EXPECT_FALSE(projectFromJson(j).ok());
}

TEST(Room, CheckRoomSettingsNamesFieldAndValue) {
    EXPECT_TRUE(checkRoomSettings(kitchen()).ok());
    RoomSettings r = kitchen();
    r.wallThickness = 0; // walls not drawn: valid
    EXPECT_TRUE(checkRoomSettings(r).ok());
    r.wallThickness = -5;
    auto e = checkRoomSettings(r);
    ASSERT_FALSE(e.ok());
    EXPECT_EQ(e.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_NE(e.error().message.find("wallThickness is -5"), std::string::npos) << e.error().message;
    r = kitchen();
    r.height = -1;
    EXPECT_FALSE(checkRoomSettings(r).ok());
    r = kitchen();
    r.origin.x = std::nan("");
    e = checkRoomSettings(r);
    ASSERT_FALSE(e.ok());
    EXPECT_NE(e.error().message.find("origin x"), std::string::npos) << e.error().message;
    r = kitchen();
    r.depth = kMaxRoomDimension; // boundary inclusive
    EXPECT_TRUE(checkRoomSettings(r).ok());
}

TEST(Room, SetRoomIsUndoableAndValidated) {
    Document doc(newProject(meta()));
    ASSERT_TRUE(doc.execute(commands::setRoom(kitchen())).ok());
    ASSERT_TRUE(doc.project().settings.room.has_value());
    EXPECT_EQ(*doc.project().settings.room, kitchen());
    EXPECT_TRUE(doc.isDirty());

    RoomSettings bad = kitchen();
    bad.width = -1;
    EXPECT_FALSE(doc.execute(commands::setRoom(bad)).ok());
    EXPECT_EQ(*doc.project().settings.room, kitchen()); // unchanged

    ASSERT_TRUE(doc.execute(commands::setRoom(std::nullopt)).ok());
    EXPECT_FALSE(doc.project().settings.room.has_value());
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_EQ(*doc.project().settings.room, kitchen());
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_FALSE(doc.project().settings.room.has_value());
    EXPECT_FALSE(doc.isDirty());
    ASSERT_TRUE(doc.redo().ok());
    EXPECT_EQ(*doc.project().settings.room, kitchen());

    // setSettings validates the room as well.
    ProjectSettings s = doc.project().settings;
    s.room->depth = 0;
    EXPECT_FALSE(doc.execute(commands::setSettings(s)).ok());
}

} // namespace
} // namespace fos::project::test
