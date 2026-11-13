// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/project/limits.hpp"

#include "fos/furniture/limits.hpp"

#include <string>

namespace fos::project {

std::size_t estimatePartCount(const Project& project) {
    std::size_t parts = 0;
    for (const Item& item : project.items)
        parts += item.cabinet() != nullptr ? furniture::estimatePartCount(item.cabinet()->interior) : 1;
    return parts;
}

core::Result<void> checkProjectLimits(const Project& project) {
    if (project.items.size() > limits::kMaxItems)
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               "The project has " + std::to_string(project.items.size()) +
                                   " furniture items; the limit is " + std::to_string(limits::kMaxItems) +
                                   " items per project.");
    if (const std::size_t parts = estimatePartCount(project); parts > limits::kMaxEstimatedParts)
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               "The furniture items of the project would generate up to " + std::to_string(parts) +
                                   " parts; the limit is " + std::to_string(limits::kMaxEstimatedParts) +
                                   " parts per project. Split the project into several files.");
    const int backups = project.settings.backupCount;
    if (backups < 0 || backups > limits::kMaxBackupCount)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "The backup count must be from 0 to " + std::to_string(limits::kMaxBackupCount) +
                                   " (is " + std::to_string(backups) + ").");
    return {};
}

} // namespace fos::project
