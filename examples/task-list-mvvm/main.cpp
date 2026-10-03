#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>

#include <QApplication>
#include <QString>

#include <ordo/core/kernel.h>
#include <ordo/qt/view_host.h>

#include "model/task_commands.h"
#include "model/task_events.h"
#include "model/task_list.h"
#include "view/task_list_window.h"
#include "viewmodel/task_list_view_model.h"

namespace {

// Headless self-check: a blank title exercises port rejection, a valid one
// exercises fact broadcast + acceptance echo.
int runSmoke(ordo::core::Kernel& kernel, app::TaskListViewModel& viewModel) {
    kernel.send(app::events::AddTaskRequested{"  "});
    if (viewModel.lastError() != QStringLiteral("title is empty") || viewModel.count() != 0) {
        std::fprintf(stderr, "FAIL: rejection not observed via the port\n");
        return 1;
    }

    kernel.send(app::events::AddTaskRequested{"ship ordo"});
    if (viewModel.count() != 1 || !viewModel.lastError().isEmpty()) {
        std::fprintf(stderr, "FAIL: count == %d, lastError == '%s'\n", viewModel.count(),
                     viewModel.lastError().toUtf8().constData());
        return 1;
    }

    std::printf("PASS: task-list-mvvm smoke (port rejection + fact broadcast)\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);

    // Bootstrap: the only place Kernel appears.
    ordo::core::Kernel kernel;
    kernel.registerAgent(std::make_shared<app::TaskList>());

    ordo::qt::ViewHost host(kernel);
    auto* viewModel = host.add<app::TaskListViewModel>();

    // The command's output port is the view-model itself, constructor-injected
    // through registerCommand's argument forwarding.
    kernel.registerCommand<app::events::AddTaskRequested, app::AddTaskCommand>(std::ref(*viewModel));

    int exitCode = 0;
    if (argc > 1 && std::strcmp(argv[1], "--smoke") == 0) {
        exitCode = runSmoke(kernel, *viewModel);
    } else {
        // Declared after `host` so it is destroyed first (reverse
        // declaration order) -- the view-model it binds to must outlive it.
        app::TaskListWindow window(viewModel);
        window.show();
        exitCode = application.exec();
    }

    // The ViewHost (and the view-model the port references) dies before
    // `kernel`; drop the registration first.
    kernel.removeCommand<app::events::AddTaskRequested>();

    return exitCode;
}
