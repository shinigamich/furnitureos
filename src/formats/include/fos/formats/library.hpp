// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/model.hpp"
#include "fos/formats/atomic_file.hpp"

#include <cstddef>

/// `.foslib` library files: materials, edge bands and hardware definitions that can be
/// shared between projects. Container kind "furnitureos-library", version 1.
namespace fos::formats {

inline constexpr std::string_view kLibraryFormatKind = "furnitureos-library";
inline constexpr int kLibraryFormatVersion = 1;
inline constexpr std::string_view kLibraryFileExtension = ".foslib";

struct Library {
    std::vector<domain::Material> materials;
    std::vector<domain::EdgeBand> edgeBands;
    std::vector<domain::HardwareDefinition> hardware;
    /// Payload keys other than "materials", "edgeBands", "hardware" found in a loaded
    /// file; written back unchanged.
    Json extra = Json::object();
};

/// Kind, current version and migrations (none yet) of library files.
const FormatSpec& libraryFormatSpec();

/// The library part of a model (materials, edge bands, hardware catalogue).
Library libraryOf(const domain::Model& model);

/// Payload object {"materials": [...], "edgeBands": [...], "hardware": [...], <extra>}.
Json libraryToJson(const Library& library);

/// Strict decode (see codec.hpp). Ids must be unique within each list.
core::Result<Library> libraryFromJson(const Json& payload, std::string_view path = "");

/// Writes `library` as a `.foslib` container (atomic, see saveDocument). Fails without
/// writing when the library would not load again (duplicate ids, invalid values).
core::Result<SaveReport> exportLibrary(const std::filesystem::path& path, const Library& library,
                                       const AppInfo& app, const SaveOptions& options = {});

/// Reads a `.foslib` file (readFile: regular files only, at most options.maxFileBytes) and
/// parses it with importLibraryText(). Container warnings (e.g. accepted checksum mismatch) are
/// appended to `warnings` when given.
core::Result<Library> importLibrary(const std::filesystem::path& path, const LoadOptions& options = {},
                                    core::Diagnostics* warnings = nullptr);

/// Parses `.foslib` container text already in memory (e.g. read once through a bounded
/// reader) exactly as importLibrary() parses a file; `sourceName` names it in messages.
/// Fails (ConstraintViolation, nothing parsed) when `text` is longer than
/// options.maxFileBytes, so callers get the same size limit as for files.
core::Result<Library> importLibraryText(std::string_view text, std::string_view sourceName,
                                        const LoadOptions& options = {}, core::Diagnostics* warnings = nullptr);

/// What to do when an incoming entry has the id of an existing entry of the same category.
enum class ConflictPolicy {
    Skip,           ///< Keep the existing entry, ignore the incoming one.
    Replace,        ///< Overwrite the existing entry in place (its list position is kept).
    RenameWithNewId ///< Add the incoming entry under a freshly generated id.
};

enum class MergeAction { Added, Skipped, Replaced, Renamed };
enum class LibraryCategory { Material, EdgeBand, Hardware };
const char* toString(MergeAction action) noexcept;
const char* toString(LibraryCategory category) noexcept;

struct MergeEntry {
    LibraryCategory category = LibraryCategory::Material;
    MergeAction action = MergeAction::Added;
    std::string incomingId; ///< Id in the imported library.
    std::string resultId;   ///< Id the entry has in the target afterwards (existing id when skipped).
    std::string name;       ///< Name of the incoming entry.
};

struct MergeReport {
    std::vector<MergeEntry> entries; ///< One per incoming entry: materials, edge bands, hardware; file order.
    std::size_t count(MergeAction action) const noexcept;
};

using IdGenerator = std::function<std::string()>;

/// Merges `incoming` into `target`. Entries with new ids are appended in incoming order;
/// conflicts follow `policy`. `newId` supplies ids for RenameWithNewId (default: random
/// UUID v4); it is called again when it returns an id that is empty or already in use,
/// and the merge fails after 100 unusable ids. NOTE: renaming gives the entry a new
/// identity — nothing that referred to the incoming id (e.g. parts of an imported
/// model) is rewritten; use MergeEntry::incomingId/resultId to remap.
/// All-or-nothing: on error (duplicate ids inside `incoming` or `target`, id generator
/// failure) `target` is unchanged. `incoming.extra` is not merged.
core::Result<MergeReport> mergeLibrary(Library& target, const Library& incoming, ConflictPolicy policy,
                                       const IdGenerator& newId = {});

} // namespace fos::formats
