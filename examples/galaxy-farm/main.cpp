#include <cstring>
#include <functional>
#include <memory>

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyleFactory>
#include <QSurfaceFormat>

#include <ordo/core/kernel.h>
#include <ordo/qt/view_host.h>

#include "checks.h"
#include "domain/galaxy_job_commands.h"
#include "domain/galaxy_job_events.h"
#include "domain/jump_commands.h"
#include "domain/jump_events.h"
#include "domain/star_map.h"
#include "infra/job_runner.h"
#include "view/farm_window.h"
#include "view/jump_presenter.h"

namespace {

// Dark Fusion theme; must be applied before FarmWindow is constructed.
void applyDarkTheme() {
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(0x14, 0x14, 0x14));
    darkPalette.setColor(QPalette::WindowText, QColor(0xe0, 0xe0, 0xe0));
    darkPalette.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
    darkPalette.setColor(QPalette::AlternateBase, QColor(0x14, 0x14, 0x14));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(0xe0, 0xe0, 0xe0));
    darkPalette.setColor(QPalette::ToolTipText, QColor(0xe0, 0xe0, 0xe0));
    darkPalette.setColor(QPalette::Text, QColor(0xe0, 0xe0, 0xe0));
    darkPalette.setColor(QPalette::Button, QColor(0x1e, 0x1e, 0x1e));
    darkPalette.setColor(QPalette::ButtonText, QColor(0xe0, 0xe0, 0xe0));
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(0x2a, 0x7f, 0xff));
    darkPalette.setColor(QPalette::Highlight, QColor(0x2a, 0x7f, 0xff));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);
    darkPalette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x78, 0x78, 0x78));
    darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x78, 0x78, 0x78));
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x78, 0x78, 0x78));
    QApplication::setPalette(darkPalette);
}

}  // namespace

int main(int argc, char** argv) {
    // Must be set before QApplication is constructed.
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);

    // Declaration order matters: `runner` is destroyed first (waiting for its
    // jobs), so no worker can call into a dead GenRelay or Kernel.
    QApplication app(argc, argv);

    applyDarkTheme();

    ordo::core::Kernel kernel;
    kernel.registerAgent(std::make_shared<app::StarMap>());
    app::GenRelay relay;
    app::JobRunner runner(relay);

    // Job callbacks re-enter the kernel as events. They run on the UI thread
    // (queued delivery), which is what makes touching `kernel` legal.
    relay.onStarted = [&kernel](const app::events::GalaxyJobStarted& event) { kernel.send(event); };
    relay.onProgress = [&kernel](const app::events::GalaxyJobProgress& event) { kernel.send(event); };
    relay.onFinished = [&kernel](const app::events::GalaxyJobFinished& event) { kernel.send(event); };

    // One Command per event type.
    kernel.registerCommand<app::events::JumpRequested, app::JumpCommand>(std::ref(runner));
    kernel.registerCommand<app::events::JumpArrivalReached, app::ArrivalCommand>();
    kernel.registerCommand<app::events::GalaxyJobStarted, app::GalaxyJobStartedCommand>();
    kernel.registerCommand<app::events::GalaxyJobProgress, app::GalaxyJobProgressCommand>();
    kernel.registerCommand<app::events::GalaxyJobFinished, app::GalaxyJobFinishedCommand>();

    // Matches the spinbox default, which only applies on later changes.
    runner.setMaxThreads(6);

    int exitCode = 0;
    if (argc > 1 && std::strcmp(argv[1], "--smoke") == 0) {
        // Fixed, and matches runSmoke's "4 threads" output.
        runner.setMaxThreads(4);
        auto starMap = kernel.agentAs<app::StarMap>(app::StarMap::kName);
        exitCode = runSmoke(kernel, starMap, runner);
    } else if (argc > 1 && std::strcmp(argv[1], "--render-probe") == 0) {
        exitCode = runRenderProbe();
    } else if (argc > 1 && std::strcmp(argv[1], "--gui-probe") == 0) {
        exitCode = runGuiProbe(kernel, app, runner);
    } else {
        // Declared before the ViewHost so the presenter dies before its widgets.
        app::FarmWindow window;

        ordo::qt::ViewHost host(kernel);
        auto* presenter = host.add<app::JumpPresenter>(
            window.canvas(), window.laneColumn(), window.progressBar(), window.runningLabel(),
            window.queuedLabel(), window.generationLabel(), window.staleLabel(), &window);

        // Also fires the first jump, so the app opens mid-jump.
        window.connectActions(*presenter, runner);

        window.show();
        exitCode = app.exec();
    }

    // Remove commands before the objects they reference go out of scope.
    kernel.removeCommand<app::events::JumpRequested>();
    kernel.removeCommand<app::events::JumpArrivalReached>();
    kernel.removeCommand<app::events::GalaxyJobStarted>();
    kernel.removeCommand<app::events::GalaxyJobProgress>();
    kernel.removeCommand<app::events::GalaxyJobFinished>();

    return exitCode;
}
