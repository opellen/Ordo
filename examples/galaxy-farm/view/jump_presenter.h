#pragma once

#include <cstdint>
#include <map>
#include <unordered_map>

#include <QElapsedTimer>
#include <QFrame>
#include <QLabel>
#include <QProgressBar>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>

#include <ordo/qt/presenter.h>

#include "domain/star_map_changed.h"
#include "view/galaxy_gl_widget.h"

namespace app {

// Writes widgets directly. Owns the ~4s jump transit clock, since StarMap has none.
class JumpPresenter : public ordo::qt::Presenter {
public:
    JumpPresenter(GalaxyGlWidget* canvas, QVBoxLayout* laneColumn, QProgressBar* progressBar,
                  QLabel* runningLabel, QLabel* queuedLabel, QLabel* generationLabel, QLabel* staleLabel,
                  QObject* root = nullptr);

    void onRegister() override;

    void jump(std::uint64_t seed, int galaxiesPerSector, int starsPerGalaxy);

    // Hides lanes for slots >= n once they go idle (the pool never uses them again).
    void setWorkerLimit(int n);

private:
    struct WorkerLane {
        QFrame* card = nullptr;
        QLabel* threadLabel = nullptr;
        bool busy = false;
        QLabel* dot = nullptr;
        QLabel* countLabel = nullptr;
        QLabel* line2 = nullptr;
        // Current galaxy's progress; 0 when idle.
        QProgressBar* bar = nullptr;
    };

    void onStarMapChanged(const events::StarMapChanged& fact);

    // Advances the jump visual; sends JumpArrivalReached when the transit ends.
    void onTransitTick();

    // Creates the lane's card on first sight of slot, in slot order.
    WorkerLane& laneFor(int slot);

    void updateLaneVisibility();

    // "sector (x,y)" of the board galaxyId currently lives on.
    static QString sectorLabelFor(const events::StarMapChanged& fact, std::uint64_t galaxyId);

    GalaxyGlWidget* canvas_;
    QVBoxLayout* laneColumn_;
    QProgressBar* progressBar_;
    QLabel* runningLabel_;
    QLabel* queuedLabel_;
    QLabel* generationLabel_;
    QLabel* staleLabel_;

    // Transit clock, restarted per epoch. Each tick adds at most kTickClampMs,
    // so a UI stall pauses the journey instead of skipping it.
    QTimer tickTimer_;
    QElapsedTimer transitClock_;
    qint64 transitElapsedMs_ = 0;
    static constexpr int kTransitMs = 4000;
    static constexpr qint64 kTickClampMs = 100;

    // 0 = none yet; real facts never carry epoch 0.
    std::uint64_t lastEpoch_ = 0;

    // Last uploaded fraction per galaxy; 2.0f means the final cloud is uploaded.
    std::unordered_map<std::uint64_t, float> uploadedFraction_;
    std::map<int, WorkerLane> lanes_;
    int workerLimit_ = 1 << 30;
};

}  // namespace app
