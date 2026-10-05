#pragma once

#include <cstdint>
#include <map>
#include <unordered_map>

#include <QFrame>
#include <QLabel>
#include <QProgressBar>
#include <QString>
#include <QVBoxLayout>

#include <ordo/qt/presenter.h>

#include "domain/shelf_changed.h"
#include "view/shelf_gl_widget.h"

namespace app {

// Imperative presenter: writes widgets directly, no observable properties
// or view-model for a view to watch.
class ShelfPresenter : public ordo::qt::Presenter {
public:
    ShelfPresenter(ShelfGlWidget* canvas, QVBoxLayout* laneColumn, QLabel* progressText,
                   QProgressBar* progressBar, QLabel* statsLabel, QObject* root = nullptr)
        : Presenter(QStringLiteral("ShelfPresenter"), root),
          canvas_(canvas),
          laneColumn_(laneColumn),
          progressText_(progressText),
          progressBar_(progressBar),
          statsLabel_(statsLabel) {}

    void onRegister() override { subscribe<events::ShelfChanged>(&ShelfPresenter::onShelfChanged); }

    // A send() call site, not a class. Called by the Regenerate button and
    // the initial batch fired before the window is shown.
    void regenerate(std::uint64_t seed, int partCount, int smoothIters, int aoRaysPerVertex);

    // Hides lanes for slots >= n once they go idle (the pool never uses them again).
    void setWorkerLimit(int n);

private:
    struct WorkerLane {
        QFrame* card = nullptr;
        QLabel* threadLabel = nullptr;
        bool busy = false;
        QLabel* dot = nullptr;
        QLabel* line2 = nullptr;
        QLabel* line3 = nullptr;
    };

    void onShelfChanged(const events::ShelfChanged& fact);

    // Returns the lane for slot, building its card (in slot order) on first sight.
    WorkerLane& laneFor(int slot);

    void updateLaneVisibility();

    ShelfGlWidget* canvas_;
    QVBoxLayout* laneColumn_;
    QLabel* progressText_;
    QProgressBar* progressBar_;
    QLabel* statsLabel_;

    std::uint64_t lastGeneration_ = 0;
    // Fraction last uploaded per part, so re-upload only happens when
    // progress has moved. 2.0f marks a part's final (Baked) mesh uploaded --
    // no legitimate fraction reaches 2.0.
    std::unordered_map<std::uint64_t, float> uploadedFraction_;
    std::map<int, WorkerLane> lanes_;
    int workerLimit_ = 1 << 30;
};

}  // namespace app
