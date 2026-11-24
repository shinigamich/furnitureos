// SPDX-License-Identifier: AGPL-3.0-only
// Room / plan settings of the project (README section 2.7): validation and the setRoom command.
#include "fos/core/units.hpp"
#include "fos/project/commands.hpp"
#include "fos/project/project.hpp"

#include <cmath>

namespace fos::project {
namespace {

core::Result<void> outOfRange(const std::string& field, double value, const std::string& rule) {
    return core::makeError(core::ErrorCode::InvalidArgument, "Room " + field + " is " +
                                                                 core::formatLength(value, 3) + " mm; it must be " +
                                                                 rule + ".");
}

} // namespace

core::Result<void> checkRoomSettings(const RoomSettings& room) {
    const std::string max = core::formatLength(kMaxRoomDimension, 0);
    const std::string positive = "a finite number greater than 0 and at most " + max + " mm";
    for (const auto& [field, v] : {std::pair{"width", room.width}, std::pair{"depth", room.depth}})
        if (!std::isfinite(v) || v <= 0.0 || v > kMaxRoomDimension) return outOfRange(field, v, positive);
    if (room.height && (!std::isfinite(*room.height) || *room.height <= 0.0 || *room.height > kMaxRoomDimension))
        return outOfRange("height", *room.height, positive);
    if (!std::isfinite(room.wallThickness) || room.wallThickness < 0.0 || room.wallThickness > kMaxRoomDimension)
        return outOfRange("wallThickness", room.wallThickness, "from 0 to " + max + " mm");
    for (const auto& [field, v] : {std::pair{"origin x", room.origin.x}, std::pair{"origin y", room.origin.y}})
        if (!std::isfinite(v) || std::abs(v) > kMaxRoomDimension)
            return outOfRange(field, v, "finite and from -" + max + " to " + max + " mm");
    return {};
}

namespace commands {

Command setRoom(std::optional<RoomSettings> room) {
    Command c;
    c.label = CommandLabel{room ? "Change room" : "Remove room", {}};
    c.apply = [room](Project& p) -> core::Result<Affected> {
        if (room)
            if (auto ok = checkRoomSettings(*room); !ok) return ok.error();
        p.settings.room = room;
        Affected a;
        a.settings = true;
        return a;
    };
    return c;
}

} // namespace commands
} // namespace fos::project
