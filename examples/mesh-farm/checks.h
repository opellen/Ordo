// Headless self-check and developer screenshot check. Not part of the app.
#pragma once

#include <functional>
#include <memory>

class QApplication;

namespace ordo::core {
class Kernel;
}

namespace app {
class JobRunner;
class PartShelf;
}  // namespace app

// Pumps the Qt event loop until `predicate` is true or `timeoutMs` elapses.
bool pump(const std::function<bool()>& predicate, int timeoutMs);

// True when no worker is busy.
bool allIdle(const app::PartShelf& shelf);

int runSmoke(ordo::core::Kernel& kernel, const std::shared_ptr<app::PartShelf>& shelf, app::JobRunner& runner);
int runGuiProbe(ordo::core::Kernel& kernel, QApplication& app, app::JobRunner& runner);
