#pragma once

#include <ordo/core/kernel.h>

#include "infra/db_worker.h"

// Command registration shared by the app and its self-check.

// Registers every command; removeCommands() undoes it.
void registerCommands(ordo::core::Kernel& kernel, app::DbWorker& dbWorker);
void removeCommands(ordo::core::Kernel& kernel);
