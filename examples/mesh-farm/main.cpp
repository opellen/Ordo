#include <cstring>
#include <functional>
#include <memory>

#include <QApplication>
#include <QSurfaceFormat>

#include <ordo/core/kernel.h>
#include <ordo/qt/view_host.h>

#include "checks.h"
#include "domain/bake_job_commands.h"
#include "domain/bake_job_events.h"
#include "domain/part_shelf.h"
#include "domain/regenerate_command.h"
#include "domain/regenerate_requested.h"
#include "infra/job_runner.h"
#include "view/farm_window.h"
#include "view/shelf_presenter.h"

int main(int argc, char** argv) {
    // Must be set before QApplication creates the GL context.
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);

    // Destruction order: `runner` first (it joins its workers), then `relay`, `kernel`, `app`.
    QApplication app(argc, argv);
    ordo::core::Kernel kernel;
    kernel.registerAgent(std::make_shared<app::PartShelf>());
    app::BakeRelay relay;
    app::JobRunner runner(relay);

    // BakeRelay's sinks re-enter the kernel as events; they run on the UI thread.
    relay.onStarted = [&kernel](const app::events::BakeJobStarted& event) { kernel.send(event); };
    relay.onProgress = [&kernel](const app::events::BakeJobProgress& event) { kernel.send(event); };
    relay.onFinished = [&kernel](const app::events::BakeJobFinished& event) { kernel.send(event); };

    // One Command per event type; RegenerateCommand also takes the runner.
    kernel.registerCommand<app::events::RegenerateRequested, app::RegenerateCommand>(std::ref(runner));
    kernel.registerCommand<app::events::BakeJobStarted, app::BakeJobStartedCommand>();
    kernel.registerCommand<app::events::BakeJobProgress, app::BakeJobProgressCommand>();
    kernel.registerCommand<app::events::BakeJobFinished, app::BakeJobFinishedCommand>();

    // Fixed so the smoke does not depend on the machine's core count.
    runner.setMaxThreads(4);

    int exitCode = 0;
    if (argc > 1 && std::strcmp(argv[1], "--smoke") == 0) {
        auto shelf = kernel.agentAs<app::PartShelf>(app::PartShelf::kName);
        exitCode = runSmoke(kernel, shelf, runner);
    } else if (argc > 1 && std::strcmp(argv[1], "--gui-probe") == 0) {
        exitCode = runGuiProbe(kernel, app, runner);
    } else {
        // `window` is declared before `host`, so the presenter is destroyed first.
        app::FarmWindow window;

        ordo::qt::ViewHost host(kernel);
        auto* presenter = host.add<app::ShelfPresenter>(window.canvas(), window.laneColumn(),
                                                          window.progressText(), window.progressBar(),
                                                          window.statsLabel(), &window);

        // connectActions fires one batch before the window is shown.
        window.connectActions(*presenter, runner);

        window.show();
        exitCode = app.exec();
    }

    // Remove the commands before the objects they reference go out of scope.
    kernel.removeCommand<app::events::RegenerateRequested>();
    kernel.removeCommand<app::events::BakeJobStarted>();
    kernel.removeCommand<app::events::BakeJobProgress>();
    kernel.removeCommand<app::events::BakeJobFinished>();

    return exitCode;
}
