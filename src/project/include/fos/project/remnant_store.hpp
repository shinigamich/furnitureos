// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Workshop remnant (offcut) stock, independent of any project: the `.fosremnants` file, stored
/// in the versioned container (formats/container.hpp) as kind "furnitureos-remnants", version 1.
///
/// Payload: {"remnants": [ {entry}, ... ], <other keys kept unchanged>}, entry:
///   "id" (string, required, unique, non-empty), "material" (material id, required, non-empty),
///   "length", "width", "thickness" (mm, > 0, required),
///   "grain" ("none" | "length" | "width", default "none"),
///   "dateAdded" ("YYYY-MM-DD", required), and optional free-text strings "location", "note",
///   "reservedBy" (job holding the piece; empty = free), "source" (where it came from).
/// Unknown keys inside an entry are refused (typo protection). Every piece is ONE physical
/// board (no quantity): using it removes the entry.
///
/// "length" is the extent along the board's grain when grain == "length"; with grain == "width"
/// the grain runs along "width"; with "none" the board has no visible direction.
#include "fos/core/diagnostic.hpp"
#include "fos/core/result.hpp"
#include "fos/formats/atomic_file.hpp"
#include "fos/formats/container.hpp"
#include "fos/formats/json.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fos::project {

inline constexpr const char* kRemnantsFormatKind = "furnitureos-remnants";
inline constexpr int kRemnantsFormatVersion = 1;
inline constexpr const char* kRemnantsFileExtension = ".fosremnants";
/// Largest number of entries a store file may hold (bounds the work a file can request).
inline constexpr std::size_t kMaxStoredRemnants = 100000;

/// Direction of the visible grain of a stored board.
enum class RemnantGrain { None, AlongLength, AlongWidth };
/// "none" / "length" / "width".
const char* remnantGrainToken(RemnantGrain grain) noexcept;
std::optional<RemnantGrain> remnantGrainFromToken(std::string_view token) noexcept;

/// One physical leftover board in the workshop.
struct StoredRemnant {
    std::string id;
    std::string materialId; ///< Matches domain::Material::id of the projects that may use it.
    double length = 0;      ///< mm
    double width = 0;       ///< mm
    double thickness = 0;   ///< mm
    RemnantGrain grain = RemnantGrain::None;
    std::string location;   ///< Rack / shelf, free text.
    std::string note;
    std::string dateAdded;  ///< "YYYY-MM-DD".
    std::string reservedBy; ///< Job holding it; empty = free to use.
    std::string source;     ///< E.g. "offcut of 'Kitchen' (kitchen.fosp)".
};

struct RemnantStore {
    std::vector<StoredRemnant> remnants; ///< File order.
    formats::Json extra = formats::Json::object(); ///< Payload keys other than "remnants".

    const StoredRemnant* find(std::string_view id) const;
};

/// Kind, current version and migrations (none yet) of `.fosremnants` files.
const formats::FormatSpec& remnantsFormatSpec();

/// True for "YYYY-MM-DD" with month 1..12 and a day that exists in that month.
bool isIsoDate(std::string_view text) noexcept;

/// Payload object (empty optional strings omitted).
formats::Json remnantStoreToJson(const RemnantStore& store);
/// Structural decode + value rules (as validateRemnantStore); fails at the first problem with
/// ParseError "<path>: <message>", e.g. "/payload/remnants/2/length: expected a positive number, got -5".
core::Result<RemnantStore> remnantStoreFromJson(const formats::Json& payload, std::string_view path = "/payload");

/// Value rules: unique non-empty ids, non-empty material, positive finite sizes, valid date,
/// at most kMaxStoredRemnants entries. Error diagnostics, code "project.remnants.invalid",
/// entity = remnant id (or "" for the store).
core::Diagnostics validateRemnantStore(const RemnantStore& store);

/// Container load (checksum, version) + remnantStoreFromJson.
core::Result<RemnantStore> loadRemnantStore(const std::filesystem::path& path, const formats::LoadOptions& options = {});
/// Atomic save (formats::saveDocument: temp file, verify, rotate backups, rename). Refuses a store
/// with validation errors (InvalidArgument, nothing written).
core::Result<formats::SaveReport> saveRemnantStore(const std::filesystem::path& path, const RemnantStore& store,
                                                   const formats::SaveOptions& options = {});

/// Next free automatic id: "R" + 4 digits, one above the highest "R<number>" id in the store
/// (and never below "R0001").
std::string nextRemnantId(const RemnantStore& store);

/// One line: "<id>  <material>  <L> x <W> x <T> mm, grain <g>, ..." (used in change listings).
std::string describeRemnant(const StoredRemnant& remnant);

} // namespace fos::project
