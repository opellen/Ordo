#include <cstdlib>
#include <cstring>
#include <memory>

#include <QApplication>
#include <QDir>
#include <QStandardPaths>

#include <ordo/core/kernel.h>
#include <ordo/qt/view_host.h>

#include "checks.h"
#include "infra/db_worker.h"
#include "model/persistence_events.h"
#include "model/todo_list.h"
#include "view/todo_view_model.h"
#include "view/todo_window.h"
#include "wiring.h"

int main(int argc, char** argv) {
    // Plain loop, before QApplication, so it doesn't need QCommandLineParser.
    // --smoke ignores --db/--slow-io and uses its own db path.
    bool smoke = false;
    QString dbPath;
    int slowIoMs = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke") == 0) {
            smoke = true;
        } else if (std::strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            dbPath = QString::fromLocal8Bit(argv[++i]);
        } else if (std::strcmp(argv[i], "--slow-io") == 0 && i + 1 < argc) {
            slowIoMs = std::atoi(argv[++i]);
        }
    }
    if (dbPath.isEmpty()) {
        // Real app default; --smoke overrides with its own temp-file path.
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        dbPath = dir + QStringLiteral("/todomvc.db");
    }

    QApplication application(argc, argv);

    int exitCode = 0;
    if (smoke) {
        exitCode = runSmoke();
    } else {
        // `dbWorker`, declared after kernel/dbRelay, is destroyed first, so its
        // drain-then-quit destructor completes every queued write while they're still alive.
        ordo::core::Kernel kernel;
        kernel.registerAgent(std::make_shared<app::TodoListAgent>());

        ordo::qt::ViewHost host(kernel);
        auto* vm = host.add<app::TodoViewModel>();

        app::DbRelay dbRelay;
        app::DbWorker dbWorker(dbRelay, dbPath, slowIoMs);

        // Runs on the UI thread (invokeMethod's queued delivery), so calling kernel here is safe.
        dbRelay.onLoaded = [&kernel](const app::events::TodosLoaded& e) { kernel.send(e); };
        dbRelay.onPersistCompleted = [&kernel](const app::events::PersistCompleted& e) { kernel.send(e); };

        registerCommands(kernel, dbWorker);

        // Kicks off the pipeline by asking DbWorker to load the table's current rows.
        kernel.send(app::events::LoadTodosRequested{});

        // `window` is declared after host/dbWorker so it's destroyed first, leaving them alive underneath it.
        app::TodoWindow window(vm);
        window.show();
        exitCode = application.exec();

        // Drop every command registration before kernel/view-model/dbWorker go out of scope.
        removeCommands(kernel);
    }

    return exitCode;
}
