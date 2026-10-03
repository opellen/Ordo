#pragma once

// Deprecated: AppKernel was renamed to Kernel. Include <ordo/core/kernel.h>.
// This header and the alias will be removed in a later minor version.

#include <ordo/core/kernel.h>

namespace ordo::core {

using AppKernel [[deprecated("renamed to ordo::core::Kernel; include <ordo/core/kernel.h>")]] = Kernel;

}  // namespace ordo::core
