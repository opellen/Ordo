#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QString>
#include <QVariant>

#include <ordo/core/kernel.h>
#include <ordo/qt/view_host.h>

#include "model/task_commands.h"
#include "model/task_events.h"
#include "model/task_list.h"
#include "viewmodel/task_list_view_model.h"

namespace {

// Headless self-check: drives the kernel directly and verifies both the
// port (lastError) and the projection (items()).
int runSmoke(ordo::core::Kernel& kernel, app::TaskListViewModel& viewModel) {
    kernel.send(app::events::AddTaskRequested{"  "});
    if (viewModel.lastError() != QStringLiteral("title is empty") || viewModel.items()->rowCount() != 0) {
        std::fprintf(stderr, "FAIL: rejection not observed via the port\n");
        return 1;
    }

    kernel.send(app::events::AddTaskRequested{"ship ordo"});
    if (viewModel.items()->rowCount() != 1 || !viewModel.lastError().isEmpty()) {
        std::fprintf(stderr, "FAIL: rowCount == %d, lastError == '%s'\n", viewModel.items()->rowCount(),
                     viewModel.lastError().toUtf8().constData());
        return 1;
    }

    const QModelIndex row0 = viewModel.items()->index(0, 0);
    const QString title = viewModel.items()->data(row0, app::TaskListViewModel::TitleRole).toString();
    const bool done = viewModel.items()->data(row0, app::TaskListViewModel::DoneRole).toBool();
    const std::uint64_t id = viewModel.items()->data(row0, app::TaskListViewModel::IdRole).toULongLong();
    if (title != QStringLiteral("ship ordo") || done) {
        std::fprintf(stderr, "FAIL: projection roles wrong after add (title='%s', done=%d)\n",
                     title.toUtf8().constData(), done);
        return 1;
    }

    kernel.send(app::events::ToggleTaskRequested{id});
    if (!viewModel.items()->data(row0, app::TaskListViewModel::DoneRole).toBool()) {
        std::fprintf(stderr, "FAIL: DoneRole not true after toggle\n");
        return 1;
    }

    kernel.send(app::events::RemoveTaskRequested{id});
    if (viewModel.items()->rowCount() != 0) {
        std::fprintf(stderr, "FAIL: rowCount == %d after remove, expected 0\n", viewModel.items()->rowCount());
        return 1;
    }

    std::printf("PASS: task-list-mvvm-qml smoke (port rejection + projection sync)\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    // The platform must be chosen before QGuiApplication exists.
    const bool qmlSmoke = argc > 1 && std::strcmp(argv[1], "--qml-smoke") == 0;
    if (qmlSmoke) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    QGuiApplication application(argc, argv);

    // Bootstrap: the only place Kernel appears.
    ordo::core::Kernel kernel;
    kernel.registerAgent(std::make_shared<app::TaskList>());

    ordo::qt::ViewHost host(kernel);
    auto* viewModel = host.add<app::TaskListViewModel>();

    // AddTaskCommand's output port is the view-model, injected via
    // registerCommand's argument forwarding; toggle/remove take no port.
    kernel.registerCommand<app::events::AddTaskRequested, app::AddTaskCommand>(std::ref(*viewModel));
    kernel.registerCommand<app::events::ToggleTaskRequested, app::ToggleTaskCommand>();
    kernel.registerCommand<app::events::RemoveTaskRequested, app::RemoveTaskCommand>();

    int exitCode = 0;
    if (argc > 1 && std::strcmp(argv[1], "--smoke") == 0) {
        exitCode = runSmoke(kernel, *viewModel);
    } else {
        // Scoped so the engine (and its QML connections into viewModel) is
        // destroyed before `host` at the end of main.
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("taskListViewModel", viewModel);
        engine.loadFromModule("TaskListMvvmQml", "Main");

        if (qmlSmoke) {
            if (engine.rootObjects().isEmpty()) {
                std::fprintf(stderr, "FAIL: task-list-mvvm-qml qml-smoke (QML failed to load)\n");
                exitCode = 1;
            } else {
                std::printf("PASS: task-list-mvvm-qml qml-smoke (offscreen QML load)\n");
                exitCode = 0;
            }
        } else {
            exitCode = application.exec();
        }
    }

    // ViewHost (and the view-model the commands' ports reference) dies
    // before `kernel` -- drop the registrations first.
    kernel.removeCommand<app::events::AddTaskRequested>();
    kernel.removeCommand<app::events::ToggleTaskRequested>();
    kernel.removeCommand<app::events::RemoveTaskRequested>();

    return exitCode;
}
