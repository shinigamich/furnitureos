// SPDX-License-Identifier: AGPL-3.0-only
// Value rules of `.fostools` tool tables and conversion to / from cam::ToolLibrary.
#include "fos/formats/tool_table.hpp"

#include "codec_reader.hpp"

#include "fos/core/units.hpp"

#include <map>

namespace fos::formats {

namespace {

constexpr const char* kInvalid = "formats.tools.invalid";

std::string mm(double v) { return core::formatLength(v, 3); }

struct Checker {
    core::Diagnostics& out;
    void add(core::Severity severity, const char* code, const std::string& path, const std::string& message) {
        out.push_back({severity, code, path + ": " + message, path});
    }
    void error(const std::string& path, const std::string& message) {
        add(core::Severity::Error, kInvalid, path, message);
    }
    void positive(const std::string& base, const std::string& who, const char* key, const char* what, double v,
                  const char* unit) {
        if (!(v > 0)) error(detail::joinPath(base, key), who + " has " + what + " " + mm(v) + unit +
                                                             "; it must be greater than 0.");
    }
};

std::string who(const ToolEntry& t) { return "tool '" + t.id + "'"; }

} // namespace

core::Diagnostics validateToolTable(const ToolTable& table, std::string_view basePath) {
    core::Diagnostics out;
    Checker c{out};
    const std::string base(basePath);
    if (table.tools.empty()) c.error(base, "the tool table has no tools.");
    std::map<std::string, std::size_t> ids;
    std::map<int, std::size_t> numbers;
    for (std::size_t i = 0; i < table.tools.size(); ++i) {
        const ToolEntry& t = table.tools[i];
        const std::string p = detail::joinPath(base, i);
        const std::string w = who(t);
        if (t.id.empty()) c.error(detail::joinPath(p, "id"), "a tool needs a non-empty id.");
        else if (const auto [it, inserted] = ids.emplace(t.id, i); !inserted)
            c.error(detail::joinPath(p, "id"), "duplicate id \"" + t.id + "\" (already used by " +
                                                   detail::joinPath(base, it->second) + ").");
        if (t.number < 1)
            c.error(detail::joinPath(p, "number"),
                    w + " has tool number " + std::to_string(t.number) + "; tool numbers must be >= 1.");
        else if (const auto [it, inserted] = numbers.emplace(t.number, i); !inserted)
            c.error(detail::joinPath(p, "number"), w + " uses tool number T" + std::to_string(t.number) +
                                                       ", already taken by '" + table.tools[it->second].id + "' (" +
                                                       detail::joinPath(base, it->second) + ").");
        c.positive(p, w, "diameter", "diameter", t.diameter, " mm");
        c.positive(p, w, "fluteLength", "flute length", t.fluteLength, " mm");
        if (t.overallLength) {
            c.positive(p, w, "overallLength", "overall length", *t.overallLength, " mm");
            if (*t.overallLength > 0 && t.fluteLength > 0 && *t.overallLength + core::kLengthEps < t.fluteLength)
                c.error(detail::joinPath(p, "overallLength"),
                        w + " has overall length " + mm(*t.overallLength) + " mm, shorter than its flute length " +
                            mm(t.fluteLength) + " mm.");
        }
        if (t.maxDepthPerPass) {
            c.positive(p, w, "maxDepthPerPass", "max depth per pass", *t.maxDepthPerPass, " mm");
            if (*t.maxDepthPerPass > 0 && t.fluteLength > 0 && *t.maxDepthPerPass > t.fluteLength + core::kLengthEps)
                c.error(detail::joinPath(p, "maxDepthPerPass"),
                        w + " has max depth per pass " + mm(*t.maxDepthPerPass) + " mm, more than its flute length " +
                            mm(t.fluteLength) + " mm.");
            if (t.type == cam::ToolKind::Drill)
                c.add(core::Severity::Warning, "formats.tools.unused", detail::joinPath(p, "maxDepthPerPass"),
                      w + " is a drill; max depth per pass is not used for drills (pecking is a CAM run option).");
        } else if (t.type == cam::ToolKind::EndMill) {
            c.error(detail::joinPath(p, "maxDepthPerPass"),
                    w + " is a flat end mill without max depth per pass; CAM needs it to plan step-downs.");
        }
        if (t.spindleRpm) c.positive(p, w, "spindleRpm", "spindle speed", *t.spindleRpm, " 1/min");
        if (t.feedRate) c.positive(p, w, "feedRate", "feed rate", *t.feedRate, " mm/min");
        if (t.plungeRate) c.positive(p, w, "plungeRate", "plunge rate", *t.plungeRate, " mm/min");
    }
    return out;
}

ToolTable toolTableFromLibrary(const cam::ToolLibrary& library) {
    ToolTable table;
    for (const cam::Tool& t : library.tools()) {
        ToolEntry e;
        e.id = t.id;
        e.number = t.number;
        e.name = t.description;
        e.type = t.kind;
        e.diameter = t.diameter;
        e.fluteLength = t.fluteLength;
        if (t.kind == cam::ToolKind::EndMill) e.maxDepthPerPass = t.maxDepthOfCut;
        e.centerCutting = t.centerCutting;
        e.spindleRpm = t.rpm;
        e.feedRate = t.feedRate;
        e.plungeRate = t.plungeRate;
        table.tools.push_back(std::move(e));
    }
    return table;
}

core::Result<cam::ToolLibrary> toToolLibrary(const ToolTable& table, std::string_view basePath) {
    for (const auto& d : validateToolTable(table, basePath))
        if (d.severity == core::Severity::Error)
            return core::makeError(core::ErrorCode::InvalidArgument, d.message, d.entityId);
    cam::ToolLibrary library;
    const std::string base(basePath);
    for (std::size_t i = 0; i < table.tools.size(); ++i) {
        const ToolEntry& e = table.tools[i];
        const std::string p = detail::joinPath(base, i);
        const std::pair<const std::optional<double>*, const char*> needed[] = {
            {&e.spindleRpm, "spindleRpm"}, {&e.feedRate, "feedRate"}, {&e.plungeRate, "plungeRate"}};
        for (const auto& [value, key] : needed)
            if (!value->has_value()) {
                const std::string what = std::string(key) == "spindleRpm" ? "spindle speed"
                                         : std::string(key) == "feedRate" ? "feed rate"
                                                                          : "plunge rate";
                return core::makeError(core::ErrorCode::InvalidArgument,
                                       detail::joinPath(p, key) + ": " + who(e) + " has no " + what +
                                           "; CAM needs one for every tool.",
                                       detail::joinPath(p, key));
            }
        cam::Tool t;
        t.id = e.id;
        t.number = e.number;
        t.kind = e.type;
        t.description = e.name;
        t.diameter = e.diameter;
        t.fluteLength = e.fluteLength;
        t.feedRate = *e.feedRate;
        t.plungeRate = *e.plungeRate;
        t.rpm = *e.spindleRpm;
        t.maxDepthOfCut = e.type == cam::ToolKind::EndMill ? *e.maxDepthPerPass : 0.0;
        t.centerCutting = e.centerCutting;
        if (auto added = library.add(std::move(t)); !added) {
            core::Error error = added.error();
            error.message = p + ": " + error.message;
            error.context = p;
            return error;
        }
    }
    return library;
}

} // namespace fos::formats
