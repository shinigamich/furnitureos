// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/version.hpp"

#ifndef FOS_APP_VERSION
#error "FOS_APP_VERSION must be defined by the build"
#endif
#ifndef FOS_APP_SOURCE_URL
#error "FOS_APP_SOURCE_URL must be defined by the build"
#endif

namespace fos::app {

const char* applicationVersion() noexcept { return FOS_APP_VERSION; }
const char* sourceUrl() noexcept { return FOS_APP_SOURCE_URL; }

} // namespace fos::app
