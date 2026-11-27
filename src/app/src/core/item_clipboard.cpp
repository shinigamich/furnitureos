// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/item_clipboard.hpp"

#include "edit_tools_detail.hpp"
#include "fos/app/session.hpp"
#include "fos/project/codec.hpp"
#include "fos/project/merge.hpp"

#include <QtGlobal>

#include <algorithm>

namespace fos::app {
namespace {

[[maybe_unused]] const char* const kClipboardKeys[] = {
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Paste {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Paste {0} items"),
};

constexpr const char* kFormat = "furnitureos-items";
constexpr int kVersion = 1;

core::Error parseError(std::string message) {
    return core::makeError(core::ErrorCode::ParseError, "The clipboard does not hold FurnitureOS items: " + std::move(message));
}

/// Boxes of every generated item of `project` (project frame); empty when it does not resolve.
std::vector<geometry::Aabb> boxesOf(const project::Project& project) {
    std::vector<geometry::Aabb> boxes;
    const auto resolved = project::resolve(project);
    if (!resolved) return boxes;
    for (const auto& item : project.items)
        if (const auto box = itemBounds(resolved.value(), item.id())) boxes.push_back(*box);
    return boxes;
}

bool overlaps(const geometry::Aabb& a, const geometry::Aabb& b) {
    constexpr double eps = 1e-6; // touching faces do not overlap
    return a.min.x < b.max.x - eps && b.min.x < a.max.x - eps && a.min.y < b.max.y - eps && b.min.y < a.max.y - eps &&
           a.min.z < b.max.z - eps && b.min.z < a.max.z - eps;
}

} // namespace

core::Result<std::string> itemsToClipboard(const project::Project& project, const std::vector<std::string>& itemIds) {
    if (auto r = detail::checkItemIds(project, itemIds); !r) return r.error();
    project::Project copy = project;
    copy.items.clear();
    for (const auto& item : project.items)
        if (std::find(itemIds.begin(), itemIds.end(), item.id()) != itemIds.end()) copy.items.push_back(item);
    copy.drawingEdits.clear();
    formats::Json json = formats::Json::object();
    json["format"] = kFormat;
    json["version"] = kVersion;
    json["project"] = project::projectToJson(copy);
    return json.dump();
}

core::Result<project::Project> itemsFromClipboard(const std::string& text) {
    const formats::Json json = formats::Json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object()) return parseError("the text is not a JSON object.");
    const auto format = json.find("format");
    if (format == json.end() || !format->is_string() || format->get<std::string>() != kFormat)
        return parseError("the \"format\" is not \"furnitureos-items\".");
    const auto version = json.find("version");
    if (version == json.end() || !version->is_number_integer() || version->get<int>() != kVersion)
        return parseError("only version 1 is supported.");
    const auto payload = json.find("project");
    if (payload == json.end() || !payload->is_object()) return parseError("the \"project\" object is missing.");
    auto project = project::projectFromJson(*payload, "/project");
    if (!project) return parseError(project.error().message);
    if (project.value().items.empty()) return parseError("it holds no items.");
    return project;
}

geometry::Vec3 pasteOffset(const project::Project& target, const project::Project& source) {
    const auto targetBoxes = boxesOf(target);
    const auto sourceBoxes = boxesOf(source);
    bool clash = false;
    for (const auto& s : sourceBoxes)
        for (const auto& t : targetBoxes) clash = clash || overlaps(s, t);
    if (!clash || sourceBoxes.empty()) return {};
    double right = targetBoxes.front().max.x, left = sourceBoxes.front().min.x;
    for (const auto& t : targetBoxes) right = std::max(right, t.max.x);
    for (const auto& s : sourceBoxes) left = std::min(left, s.min.x);
    return {right - left, 0, 0};
}

core::Result<std::vector<std::string>> pasteItems(Session& session, const std::string& text) {
    auto source = itemsFromClipboard(text);
    if (!source) return source.error();
    project::MergeRequest request;
    request.conflictPolicy = formats::ConflictPolicy::RenameWithNewId;
    request.offset = geometry::Transform::translation(pasteOffset(session.project(), source.value()));
    const auto plan = project::planMerge(session.project(), source.value(), request);
    if (!plan) return plan.error();
    std::vector<std::string> ids;
    for (const auto& item : plan.value().items) ids.push_back(item.newId);
    auto command = project::commands::mergeItems(std::move(source).value(), request);
    command.label = ids.size() == 1 ? project::CommandLabel{"Paste {0}", {ids.front()}}
                                    : project::CommandLabel{"Paste {0} items", {std::to_string(ids.size())}};
    if (auto r = session.execute(command); !r) return r.error();
    session.selectItems(ids);
    return ids;
}

core::Result<std::string> copySelectedItems(const Session& session) {
    return itemsToClipboard(session.project(), session.selection().allItems());
}

core::Result<std::string> cutSelectedItems(Session& session) {
    auto text = copySelectedItems(session);
    if (!text) return text.error();
    if (auto r = deleteSelectedItems(session); !r) return r.error();
    return text;
}

} // namespace fos::app
