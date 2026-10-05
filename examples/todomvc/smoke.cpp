#include "checks.h"

#include <cstdio>
#include <functional>
#include <memory>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QString>
#include <QThread>

#include <ordo/core/kernel.h>
#include <ordo/qt/view_host.h>

#include "infra/db_worker.h"
#include "model/persistence_events.h"
#include "model/todo_list.h"
#include "view/todo_view_model.h"
#include "wiring.h"

namespace {

// Finds the id of the (first) item with the given title, or 0 if none.
quint64 idOf(const app::TodoViewModel& vm, const QString& title) {
    for (const auto& item : vm.items()) {
        if (item.title == title) {
            return item.id;
        }
    }
    return 0;
}

// Pumps the Qt event loop until `predicate` becomes true or `timeoutMs` elapses.
bool pump(const std::function<bool()>& predicate, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (!timer.hasExpired(timeoutMs)) {
        QCoreApplication::processEvents();
        if (predicate()) {
            return true;
        }
        QThread::msleep(5);
    }
    return false;
}

// Full bootstrap for one session, reused by the smoke below to reopen the
// same db three times. `dbWorker` is declared after `kernel`/`dbRelay` so
// it destructs first, draining queued writes while they're still alive.
struct SmokeSession {
    ordo::core::Kernel kernel;
    ordo::qt::ViewHost host;
    app::DbRelay dbRelay;
    app::DbWorker dbWorker;
    app::TodoViewModel* vm = nullptr;

    SmokeSession(const QString& dbPath, int slowIoMs) : host(kernel), dbWorker(dbRelay, dbPath, slowIoMs) {
        kernel.registerAgent(std::make_shared<app::TodoListAgent>());
        vm = host.add<app::TodoViewModel>();

        // Runs on the UI thread (invokeMethod's queued delivery), so calling kernel here is safe.
        dbRelay.onLoaded = [this](const app::events::TodosLoaded& e) { kernel.send(e); };
        dbRelay.onPersistCompleted = [this](const app::events::PersistCompleted& e) { kernel.send(e); };

        registerCommands(kernel, dbWorker);

        // Kicks off the pipeline by asking DbWorker to load the table's current rows.
        kernel.send(app::events::LoadTodosRequested{});
    }

    ~SmokeSession() {
        // Drop every command registration before kernel/host/dbWorker go out of scope.
        removeCommands(kernel);
    }
};

// Drives the view-model's slots like the widgets would, covering the full TodoMVC feature set.
int runScenario(app::TodoViewModel& vm) {
    vm.addTodo(QStringLiteral("buy milk"));
    vm.addTodo(QStringLiteral("ship ordo"));
    if (vm.totalCount() != 2 || vm.activeCount() != 2) {
        std::fprintf(stderr, "FAIL: after two adds totalCount=%d activeCount=%d (want 2, 2)\n", vm.totalCount(),
                     vm.activeCount());
        return 1;
    }

    // A blank title is ignored.
    vm.addTodo(QStringLiteral("   "));
    if (vm.totalCount() != 2) {
        std::fprintf(stderr, "FAIL: blank add changed totalCount to %d (want 2)\n", vm.totalCount());
        return 1;
    }

    // Toggle one todo complete.
    const quint64 buyMilkId = idOf(vm, QStringLiteral("buy milk"));
    vm.toggleTodo(buyMilkId);
    if (vm.activeCount() != 1 || vm.completedCount() != 1 || vm.allComplete() || !vm.hasCompleted()) {
        std::fprintf(stderr,
                     "FAIL: after toggle activeCount=%d completedCount=%d allComplete=%d hasCompleted=%d\n",
                     vm.activeCount(), vm.completedCount(), vm.allComplete(), vm.hasCompleted());
        return 1;
    }

    // All / Active / Completed filters.
    vm.setFilter(app::events::Filter::Active);
    if (vm.items().size() != 1 || vm.items()[0].title != QStringLiteral("ship ordo")) {
        std::fprintf(stderr, "FAIL: Active filter returned %d item(s)\n", static_cast<int>(vm.items().size()));
        return 1;
    }
    vm.setFilter(app::events::Filter::Completed);
    if (vm.items().size() != 1 || vm.items()[0].title != QStringLiteral("buy milk")) {
        std::fprintf(stderr, "FAIL: Completed filter returned %d item(s)\n", static_cast<int>(vm.items().size()));
        return 1;
    }
    vm.setFilter(app::events::Filter::All);
    if (vm.items().size() != 2) {
        std::fprintf(stderr, "FAIL: All filter returned %d item(s) (want 2)\n", static_cast<int>(vm.items().size()));
        return 1;
    }

    // Edit a title.
    const quint64 shipOrdoId = idOf(vm, QStringLiteral("ship ordo"));
    vm.editTodo(shipOrdoId, QStringLiteral("ship ordo v1"));
    QString editedTitle;
    for (const auto& item : vm.items()) {
        if (item.id == shipOrdoId) {
            editedTitle = item.title;
        }
    }
    if (editedTitle != QStringLiteral("ship ordo v1")) {
        std::fprintf(stderr, "FAIL: edit title is '%s'\n", qUtf8Printable(editedTitle));
        return 1;
    }

    // Editing a title down to blank destroys the todo.
    vm.editTodo(shipOrdoId, QStringLiteral("  "));
    if (vm.totalCount() != 1) {
        std::fprintf(stderr, "FAIL: edit-to-empty totalCount=%d (want 1)\n",
                     vm.totalCount());
        return 1;
    }

    // Toggle-all, then clear-completed.
    vm.toggleAll(true);
    if (vm.activeCount() != 0 || !vm.allComplete()) {
        std::fprintf(stderr, "FAIL: toggleAll(true) activeCount=%d allComplete=%d\n", vm.activeCount(),
                     vm.allComplete());
        return 1;
    }
    vm.clearCompleted();
    if (vm.totalCount() != 0 || !vm.items().empty() || vm.hasCompleted()) {
        std::fprintf(stderr, "FAIL: clearCompleted left totalCount=%d items=%d hasCompleted=%d\n", vm.totalCount(),
                     static_cast<int>(vm.items().size()), vm.hasCompleted());
        return 1;
    }

    // Toggle-all round trip.
    vm.addTodo(QStringLiteral("a"));
    vm.toggleAll(true);
    vm.toggleAll(false);
    if (vm.activeCount() != 1 || vm.completedCount() != 0) {
        std::fprintf(stderr, "FAIL: toggleAll round trip activeCount=%d completedCount=%d\n", vm.activeCount(),
                     vm.completedCount());
        return 1;
    }

    return 0;
}

}  // namespace

// Headless self-check: spec drive on a fresh db, reload to verify persistence
// and id continuity, then a slow-io reload to verify the loading state is real.
int runSmoke() {
    const QString dbPath =
        QDir::temp().filePath(QStringLiteral("ordo-todomvc-smoke-%1.db").arg(QCoreApplication::applicationPid()));
    // A stale file from a killed prior run (or recycled pid) must not leak rows into phase 1.
    QFile::remove(dbPath);

    // ---- Phase 1: spec drive, then a persisted add + toggle, fast io ------
    {
        SmokeSession session(dbPath, /*slowIoMs=*/0);
        if (!pump([&] { return session.vm->loaded(); }, 30000)) {
            std::fprintf(stderr, "FAIL: phase 1 loaded() timed out\n");
            return 1;
        }
        if (const int rc = runScenario(*session.vm)) {
            return rc;
        }

        session.vm->addTodo(QStringLiteral("persist me"));
        if (!pump([&] { return session.vm->totalCount() == 2; }, 30000)) {
            std::fprintf(stderr, "FAIL: phase 1 add timed out (totalCount=%d, want 2)\n",
                         session.vm->totalCount());
            return 1;
        }

        const quint64 persistMeId = idOf(*session.vm, QStringLiteral("persist me"));
        session.vm->toggleTodo(persistMeId);
        if (!pump(
                [&] {
                    for (const auto& item : session.vm->items()) {
                        if (item.id == persistMeId) {
                            return item.completed;
                        }
                    }
                    return false;
                },
                30000)) {
            std::fprintf(stderr, "FAIL: phase 1 toggle timed out\n");
            return 1;
        }

        // "a" has the lower id, so insertion order puts it first regardless of toggle order.
        const auto& items = session.vm->items();
        if (session.vm->totalCount() != 2 || items.size() != 2 || items[0].title != QStringLiteral("a") ||
            items[0].completed || items[1].title != QStringLiteral("persist me") || !items[1].completed) {
            std::fprintf(stderr,
                         "FAIL: phase 1 final board mismatch (totalCount=%d items=%d)\n",
                         session.vm->totalCount(), static_cast<int>(items.size()));
            return 1;
        }

        if (!pump([&] { return session.vm->pendingWrites() == 0; }, 30000)) {
            std::fprintf(stderr, "FAIL: phase 1 pendingWrites timed out (pendingWrites=%d)\n",
                         session.vm->pendingWrites());
            return 1;
        }
    }  // session destructs here -- drains (a no-op by now: pendingWrites was already 0).

    // ---- Phase 2: a fresh session over the same file, fast io -------------
    {
        SmokeSession session(dbPath, /*slowIoMs=*/0);
        if (!pump([&] { return session.vm->loaded(); }, 30000)) {
            std::fprintf(stderr, "FAIL: phase 2 loaded() timed out\n");
            return 1;
        }

        const auto& items = session.vm->items();
        if (items.size() != 2 || items[0].title != QStringLiteral("a") || items[0].completed ||
            items[1].title != QStringLiteral("persist me") || !items[1].completed) {
            std::fprintf(stderr,
                         "FAIL: phase 2 reload mismatch (size=%d)\n",
                         static_cast<int>(items.size()));
            return 1;
        }

        // Proves id continuity survives a reload -- else this would collide with an existing row.
        session.vm->addTodo(QStringLiteral("post reload"));
        if (!pump([&] { return session.vm->totalCount() == 3; }, 30000)) {
            std::fprintf(stderr,
                         "FAIL: phase 2 add timed out (totalCount=%d, want 3)\n",
                         session.vm->totalCount());
            return 1;
        }

        if (!pump([&] { return session.vm->pendingWrites() == 0; }, 30000)) {
            std::fprintf(stderr, "FAIL: phase 2 pendingWrites timed out (pendingWrites=%d)\n",
                         session.vm->pendingWrites());
            return 1;
        }
    }

    // ---- Phase 3: a third session over the same file, slow io -------------
    {
        SmokeSession session(dbPath, /*slowIoMs=*/80);
        bool observedLoading = false;
        if (!pump(
                [&] {
                    if (!session.vm->loaded()) {
                        observedLoading = true;
                    }
                    return session.vm->loaded();
                },
                30000)) {
            std::fprintf(stderr, "FAIL: phase 3 loaded() timed out\n");
            return 1;
        }
        if (!observedLoading) {
            std::fprintf(stderr,
                         "FAIL: phase 3 never observed loaded()==false\n");
            return 1;
        }
        if (session.vm->items().size() != 3) {
            std::fprintf(stderr, "FAIL: phase 3 items size=%d (want 3)\n",
                         static_cast<int>(session.vm->items().size()));
            return 1;
        }
    }

    std::printf("PASS: todomvc smoke (spec + sqlite round trip, worker-thread io)\n");
    return 0;
}
