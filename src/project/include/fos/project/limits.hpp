// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Hard size limits of a project (README "Limits"). With the per-cabinet limits of
/// fos/furniture/limits.hpp they bound the work of loading, generating and saving any
/// project, so that a hostile or corrupted `.fosp` file cannot hang the application. The
/// codec rejects files beyond them; validateStructure() (and therefore resolve() and every
/// Document command) refuses projects beyond them.
#include "fos/core/result.hpp"
#include "fos/formats/atomic_file.hpp"
#include "fos/project/project.hpp"

#include <cstddef>

namespace fos::project {

namespace limits {
/// Furniture items (cabinets + panels) of one project.
inline constexpr std::size_t kMaxItems = 500;
/// Sum over all items of furniture::estimatePartCount() (cabinets) and 1 per panel item.
inline constexpr std::size_t kMaxEstimatedParts = 20000;
/// ProjectSettings::backupCount range is 0 ..= kMaxBackupCount (= formats::kMaxBackupCount).
inline constexpr int kMaxBackupCount = formats::kMaxBackupCount;
} // namespace limits

/// Upper bound of the parts resolve() generates for `project`.
std::size_t estimatePartCount(const Project& project);

/// ConstraintViolation when the project has more than limits::kMaxItems items or more than
/// limits::kMaxEstimatedParts estimated parts; InvalidArgument when settings.backupCount is
/// outside 0 ..= limits::kMaxBackupCount. Per-cabinet limits are checked by the furniture
/// generator for each item (furniture::checkDefinitionLimits).
core::Result<void> checkProjectLimits(const Project& project);

} // namespace fos::project
