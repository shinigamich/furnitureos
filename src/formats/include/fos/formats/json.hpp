// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <nlohmann/json.hpp>

namespace fos::formats {

/// JSON value used throughout the module. Object keys keep their insertion / file order,
/// which makes saved files stable and diff-friendly. Checksums do NOT depend on key
/// order (see canonicalDump in container.hpp).
using Json = nlohmann::ordered_json;

} // namespace fos::formats
