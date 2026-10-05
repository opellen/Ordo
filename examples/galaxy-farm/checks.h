#pragma once

// Headless self-check and developer screenshot checks. Not part of the app.

#include <functional>
#include <memory>

class QApplication;

namespace ordo::core {
class Kernel;
}

namespace app {
class JobRunner;
class StarMap;
}  // namespace app

// Pumps events until `predicate` holds or `timeoutMs` elapses. Defined in smoke.cpp.
bool pump(const std::function<bool()>& predicate, int timeoutMs);

// True when no worker is busy. Defined in smoke.cpp.
bool allWorkersIdle(const app::StarMap& starMap);

int runSmoke(ordo::core::Kernel& kernel, const std::shared_ptr<app::StarMap>& starMap, app::JobRunner& runner);
int runRenderProbe();
int runGuiProbe(ordo::core::Kernel& kernel, QApplication& app, app::JobRunner& runner);
