// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/id.hpp"

namespace fos::domain {
using MaterialId = core::TypedId<struct MaterialTag>;
using EdgeBandId = core::TypedId<struct EdgeBandTag>;
using PartId = core::TypedId<struct PartTag>;
using AssemblyId = core::TypedId<struct AssemblyTag>;
using HardwareId = core::TypedId<struct HardwareTag>;         ///< catalogue definition
using HardwareInstanceId = core::TypedId<struct HardwareInstanceTag>;
using OperationId = core::TypedId<struct OperationTag>;
} // namespace fos::domain
