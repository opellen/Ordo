#include "checks.h"

#include <cstdio>

#include <QApplication>
#include <QDir>
#include <QImageWriter>
#include <QPixmap>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <ordo/core/kernel.h>
#include <ordo/qt/view_host.h>

#include "domain/part_shelf.h"
#include "infra/job_runner.h"
#include "view/farm_window.h"
#include "view/shelf_presenter.h"

// GUI probe: shows the window, grabs screenshots on timers into a
// mesh-farm-probe folder under the system temp dir, then quits.
int runGuiProbe(ordo::core::Kernel& kernel, QApplication& app, app::JobRunner& runner) {
    const QString probeDir = QDir(QDir::tempPath()).filePath(QStringLiteral("mesh-farm-probe"));
    QDir().mkpath(probeDir);

    // `window` is declared before the ViewHost so it outlives it.
    app::FarmWindow window;

    ordo::qt::ViewHost host(kernel);
    auto* presenter = host.add<app::ShelfPresenter>(window.canvas(), window.laneColumn(), window.progressText(),
                                                      window.progressBar(), window.statsLabel(), &window);

    // connectActions fires one batch before the window is shown.
    window.connectActions(*presenter, runner);
    window.show();

    // A plain window grab captures the GL scene.
    auto saveWindowFrame = [](app::FarmWindow& w, const QString& path) {
        const QPixmap windowPixmap = w.grab();
        if (windowPixmap.isNull()) {
            std::fprintf(stderr, "FAIL: null pixmap for %s\n", qUtf8Printable(path));
            return false;
        }
        QImageWriter writer(path, "png");
        if (!writer.write(windowPixmap.toImage())) {
            std::fprintf(stderr, "FAIL: QImageWriter failed for %s error=%s\n", qUtf8Printable(path),
                         qUtf8Printable(writer.errorString()));
            return false;
        }
        std::printf("wrote %s\n", qUtf8Printable(path));
        return true;
    };

    int exitCode = 0;

    // ~2s: mid-bake.
    QTimer::singleShot(2000, &window, [&] {
        if (!saveWindowFrame(window, QDir(probeDir).filePath(QStringLiteral("gui-baking.png")))) {
            exitCode = 1;
            QApplication::quit();
        }
    });

    // ~13s: past the batch, every part Baked.
    QTimer::singleShot(13000, &window, [&] {
        if (!saveWindowFrame(window, QDir(probeDir).filePath(QStringLiteral("gui-idle.png")))) {
            exitCode = 1;
        }

        // Shrinking Max threads hides the lanes above the new limit.
        auto shelf = kernel.agentAs<app::PartShelf>(app::PartShelf::kName);
        if (!pump([&] { return allIdle(*shelf); }, 30000)) {
            std::fprintf(stderr, "FAIL: workers not idle before lane-shrink check\n");
            exitCode = 1;
        } else {
            QWidget* laneHost = window.laneColumn()->parentWidget();
            auto visibleLanes = [&] {
                int n = 0;
                for (QWidget* card : laneHost->findChildren<QWidget*>(QStringLiteral("workerLane"))) {
                    n += card->isVisibleTo(&window) ? 1 : 0;
                }
                return n;
            };
            const int before = visibleLanes();
            window.maxThreadsSpin()->setValue(2);
            const int after = visibleLanes();
            if (before < 3 || after != 2) {
                std::fprintf(stderr, "FAIL: visible lanes before=%d after=%d expected after=2\n", before, after);
                exitCode = 1;
            } else {
                std::printf("PASS: worker lanes %d -> %d after Max threads 4 -> 2\n", before, after);
            }
            laneHost->grab().save(QDir(probeDir).filePath(QStringLiteral("gui-lanes-shrunk.png")));
        }
        QApplication::quit();
    });

    app.exec();
    return exitCode;
}
