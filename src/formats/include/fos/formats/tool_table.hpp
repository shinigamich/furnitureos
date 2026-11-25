// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/cam/tool.hpp"
#include "fos/core/diagnostic.hpp"
#include "fos/core/result.hpp"
#include "fos/formats/atomic_file.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// `.fostools` tool table files: the cutting tools fos::cam uses, stored in the versioned
/// container (container.hpp) as kind "furnitureos-tools", version 1. Target `fos::formats_cam`
/// (built only together with fos::cam, so that fos::formats itself stays free of CAM).
///
/// Payload: {"tools": [ {tool}, ... ], <other keys kept unchanged>}, tool:
///   "id" (string, required, unique), "number" (integer >= 1, required, unique: Tn),
///   "name" (string), "type" ("drill" | "flat_end_mill", required),
///   "diameter", "fluteLength" (mm, required), "overallLength" (mm, optional),
///   "maxDepthPerPass" (mm; required for flat end mills, unused for drills),
///   "centerCutting" (bool, flat end mills, default true),
///   "spindleRpm" (1/min), "feedRate", "plungeRate" (mm/min) (optional), "notes" (string).
/// Unknown keys inside a tool are refused (typo protection). All lengths in mm.
/// Tool data is user data: nothing here checks it against a real tool, a manufacturer
/// recommendation or a machine.
namespace fos::formats {

inline constexpr std::string_view kToolsFormatKind = "furnitureos-tools";
inline constexpr int kToolsFormatVersion = 1;
inline constexpr std::string_view kToolsFileExtension = ".fostools";

/// One tool as stored in a `.fostools` file. Unlike cam::Tool, speeds and feeds may be absent.
struct ToolEntry {
    std::string id;
    int number = 0;                         ///< Tool number in the changer (Tn), >= 1.
    std::string name;                       ///< Free text (cam::Tool::description).
    cam::ToolKind type = cam::ToolKind::Drill;
    double diameter = 0;                    ///< mm
    double fluteLength = 0;                 ///< Usable cutting length, mm.
    std::optional<double> overallLength;    ///< mm, >= fluteLength. Informational only.
    std::optional<double> maxDepthPerPass;  ///< mm, <= fluteLength. Required for flat end mills.
    bool centerCutting = true;              ///< Flat end mills: may plunge axially.
    std::optional<double> spindleRpm;       ///< 1/min
    std::optional<double> feedRate;         ///< Horizontal cutting feed, mm/min.
    std::optional<double> plungeRate;       ///< Axial feed, mm/min.
    std::string notes;
};

struct ToolTable {
    std::vector<ToolEntry> tools; ///< File order.
    Json extra = Json::object();  ///< Payload keys other than "tools"; written back unchanged.
};

/// "drill" / "flat_end_mill".
const char* toolTypeToken(cam::ToolKind kind) noexcept;
/// Inverse of toolTypeToken; nullopt for anything else.
std::optional<cam::ToolKind> toolTypeFromToken(std::string_view token) noexcept;

/// Kind, current version and migrations (none yet) of tool table files.
const FormatSpec& toolsFormatSpec();

/// Payload object (absent optionals are omitted; centerCutting only for flat end mills).
Json toolTableToJson(const ToolTable& table);

/// Structural decode: types, required keys, known type tokens, unknown keys. Fails at the
/// first problem with ParseError "<path>: <message>", e.g.
/// "/payload/tools/2/type: expected one of "drill", "flat_end_mill", got "ball_end_mill" ...".
/// Value rules (positive sizes, unique ids, ...) are checked by validateToolTable().
core::Result<ToolTable> toolTableFromJson(const Json& payload, std::string_view path = "");

/// Every value-rule finding, in file order; `basePath` is the JSON path of the tools array
/// (default "/payload/tools", i.e. paths in a `.fostools` file). Errors (code
/// "formats.tools.invalid", entity = path such as "/payload/tools/1/maxDepthPerPass"):
/// empty table, duplicate id or number, number < 1, diameter / flute length / overall length /
/// max depth per pass / rpm / feeds <= 0, overall length < flute length, max depth per pass >
/// flute length, flat end mill without max depth per pass. Warning ("formats.tools.unused"):
/// max depth per pass on a drill (CAM does not use it).
core::Diagnostics validateToolTable(const ToolTable& table, std::string_view basePath = "/payload/tools");

/// File form of a CAM tool library (no overall length, no notes; speeds and feeds present).
ToolTable toolTableFromLibrary(const cam::ToolLibrary& library);

/// CAM tool library of a table. Fails (InvalidArgument) on the first validateToolTable() error,
/// or when a tool lacks spindleRpm / feedRate / plungeRate, which CAM needs, e.g.
/// "/payload/tools/0/spindleRpm: tool 'drill.5' has no spindle speed; CAM needs one for every tool."
core::Result<cam::ToolLibrary> toToolLibrary(const ToolTable& table, std::string_view basePath = "/payload/tools");

/// Writes `table` as a `.fostools` container (atomic, see saveDocument). Fails without
/// writing when validateToolTable() reports an error.
core::Result<SaveReport> saveToolTable(const std::filesystem::path& path, const ToolTable& table, const AppInfo& app,
                                       const SaveOptions& options = {});

struct LoadedToolTable {
    ToolTable table;
    /// Container warnings followed by validateToolTable() findings. The table is returned even
    /// when this holds errors (so that `fos tools validate` can list them all): check
    /// core::hasErrors() before using it.
    core::Diagnostics diagnostics;
};

/// Parses `.fostools` container text (checksum verified, see parseDocument) and decodes it.
/// Fails on container or structural errors; value-rule findings go to diagnostics.
core::Result<LoadedToolTable> parseToolTableText(std::string_view text, std::string_view sourceName,
                                                 const LoadOptions& options = {});
/// readFile (limit options.maxFileBytes) + parseToolTableText.
core::Result<LoadedToolTable> loadToolTable(const std::filesystem::path& path, const LoadOptions& options = {});

} // namespace fos::formats
