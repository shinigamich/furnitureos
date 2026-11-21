// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/formats/library.hpp"

#include <string>
#include <vector>

/// `.foscomp` component files: reusable furniture components (a cabinet or a free panel
/// definition plus the library entries it references). Container kind
/// "furnitureos-component", version 1. Schema: src/project/README.md "Component files".
///
/// This module reads the container, the metadata and the embedded library strictly; the
/// item definition ("definition") is kept as JSON here and decoded by the project module
/// (fos/project/components.hpp), which owns the item codec. At this level a definition only
/// has to be an object whose "type" is "cabinet" or "panel".
namespace fos::formats {

inline constexpr std::string_view kComponentFormatKind = "furnitureos-component";
inline constexpr int kComponentFormatVersion = 1;
inline constexpr std::string_view kComponentFileExtension = ".foscomp";

/// Overall size of the component for previews, mm (width along x, height along z, depth
/// along y of the item frame). 0 = unknown. Never used for generation.
struct PreviewSize {
    double width = 0;
    double height = 0;
    double depth = 0;
    bool operator==(const PreviewSize&) const = default;
};

/// Descriptive data of one component.
struct ComponentMeta {
    std::string id;          ///< Unique within a file; non-empty. Plugins: "<plugin-id>:<local>".
    std::string name;        ///< Display name; non-empty, unique within a file.
    std::string description;
    std::string category;    ///< Free text, e.g. "Base cabinets"; empty = uncategorised.
    std::vector<std::string> tags; ///< Non-empty, unique strings, file order.
    std::string author;
    PreviewSize previewSize;
};

/// One component as stored in a file.
struct ComponentRecord {
    ComponentMeta meta;
    /// Exactly the library entries the definition references (self-contained component).
    Library library;
    /// Item object as written by project::toJson(const Item&) ("type": "cabinet" | "panel").
    Json definition = Json::object();
    /// Unknown keys of the component object (preserved and written back).
    Json extra = Json::object();
};

struct ComponentFile {
    std::vector<ComponentRecord> components; ///< File order; at least one.
    Json extra = Json::object();             ///< Unknown payload keys (preserved).
};

/// Kind, current version and migrations (none yet) of component files.
const FormatSpec& componentFormatSpec();

/// Payload object {"components": [...], <extra>}.
Json componentFileToJson(const ComponentFile& file);

/// Strict decode of a payload. Errors (ParseError, message starts with a JSON pointer such
/// as "/payload/components/0/previewSize/width: ..."): missing/ill-typed keys, an empty
/// component list, empty or duplicate ids/names, empty or duplicate tags, negative or
/// non-finite preview sizes, an invalid embedded library (libraryFromJson), a definition that
/// is not an object with "type" "cabinet" or "panel".
core::Result<ComponentFile> componentFileFromJson(const Json& payload, std::string_view path = "");

/// Writes a `.foscomp` container atomically (saveDocument). Fails without writing when the
/// file would not load again.
core::Result<SaveReport> exportComponentFile(const std::filesystem::path& path, const ComponentFile& file,
                                             const AppInfo& app, const SaveOptions& options = {});

/// Parses `.foscomp` container text already in memory; `sourceName` names it in messages
/// (every error message starts with "'<sourceName>' ..."). Refuses text longer than
/// options.maxFileBytes (ConstraintViolation). Container warnings go to `warnings`.
core::Result<ComponentFile> importComponentFileText(std::string_view text, std::string_view sourceName,
                                                    const LoadOptions& options = {},
                                                    core::Diagnostics* warnings = nullptr);

/// readFile (regular files only, at most options.maxFileBytes) + importComponentFileText with
/// the full path as source name, so every error names the file.
core::Result<ComponentFile> importComponentFile(const std::filesystem::path& path, const LoadOptions& options = {},
                                                core::Diagnostics* warnings = nullptr);

} // namespace fos::formats
