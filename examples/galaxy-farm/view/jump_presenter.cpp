#include "view/jump_presenter.h"

#include <algorithm>
#include <cmath>

#include <QFrame>
#include <QHBoxLayout>

#include "domain/star_map.h"
#include "domain/jump_events.h"

namespace app {

JumpPresenter::JumpPresenter(GalaxyGlWidget* canvas, QVBoxLayout* laneColumn, QProgressBar* progressBar,
                              QLabel* runningLabel, QLabel* queuedLabel, QLabel* generationLabel,
                              QLabel* staleLabel, QObject* root)
    : Presenter(QStringLiteral("JumpPresenter"), root),
      canvas_(canvas),
      laneColumn_(laneColumn),
      progressBar_(progressBar),
      runningLabel_(runningLabel),
      queuedLabel_(queuedLabel),
      generationLabel_(generationLabel),
      staleLabel_(staleLabel) {
    tickTimer_.setInterval(33);
    QObject::connect(&tickTimer_, &QTimer::timeout, this, [this] { onTransitTick(); });
}

void JumpPresenter::onRegister() { subscribe<events::StarMapChanged>(&JumpPresenter::onStarMapChanged); }

void JumpPresenter::jump(std::uint64_t seed, int galaxiesPerSector, int starsPerGalaxy) {
    context().send(events::JumpRequested{seed, galaxiesPerSector, starsPerGalaxy});
}

void JumpPresenter::onStarMapChanged(const events::StarMapChanged& fact) {
    // 1. New epoch = new jump: fresh sector slots and a restarted transit clock.
    if (fact.epoch != lastEpoch_) {
        // Launches keep the user's pose; only the very first board reframes.
        const bool reframeCamera = lastEpoch_ == 0 || fact.phase != events::JumpPhase::Jumping;
        lastEpoch_ = fact.epoch;
        // Slot layout is keyed by the sector's coordinates.
        const events::SectorCoord sec = fact.phase == events::JumpPhase::Jumping ? fact.destinationSector
                                                                                  : fact.currentSector;
        const quint64 layoutSeed =
            (static_cast<quint64>(static_cast<quint32>(sec.x)) << 32) ^ static_cast<quint32>(sec.y) ^
            0x9E3779B97F4A7C15ULL;
        canvas_->beginSector(fact.total, reframeCamera, layoutSeed);
        uploadedFraction_.clear();

        transitClock_.restart();
        transitElapsedMs_ = 0;
        // Switch to Jumping now: the departing board is only drawn while Jumping.
        if (fact.phase == events::JumpPhase::Jumping) {
            canvas_->setJumpVisual(GalaxyGlWidget::JumpVisual::Jumping, 0.0f);
        }
        if (!tickTimer_.isActive()) {
            tickTimer_.start();
        }
    }

    // 2. Sky and HUD. Once Idle the destination reads (0,0), so pass the current sector twice.
    if (fact.phase == events::JumpPhase::Jumping) {
        canvas_->setSectorSky(fact.currentSector.x, fact.currentSector.y, fact.destinationSector.x,
                               fact.destinationSector.y);
    } else {
        canvas_->setSectorSky(fact.currentSector.x, fact.currentSector.y, fact.currentSector.x,
                               fact.currentSector.y);
    }
    if (fact.phase == events::JumpPhase::Idle) {
        tickTimer_.stop();
        canvas_->setJumpVisual(GalaxyGlWidget::JumpVisual::Idle, 0.0f);
        canvas_->setHudLines(QString(), QString());
    } else {
        canvas_->setHudLines(QStringLiteral("jumping to sector (%1,%2)")
                                  .arg(fact.destinationSector.x)
                                  .arg(fact.destinationSector.y),
                              QStringLiteral("building %1 galaxies").arg(fact.total));
    }

    // 3. Active board -> canvas. Arrival keeps galaxy order, so slot indices stay stable.
    const events::Board activeBoard =
        (fact.phase == events::JumpPhase::Jumping) ? events::Board::Destination : events::Board::Current;

    auto starMap = context().agentAs<StarMap>(StarMap::kName);
    if (starMap) {
        int slotIndex = 0;
        for (const events::GalaxyStatus& galaxy : fact.galaxies) {
            if (galaxy.board != activeBoard) {
                continue;
            }
            const int mySlot = slotIndex++;

            const auto storedIt = uploadedFraction_.find(galaxy.galaxyId);
            const float stored = storedIt != uploadedFraction_.end() ? storedIt->second : -1.0f;
            const bool worthUploading =
                (galaxy.state == events::GalaxyState::Generating && galaxy.progress > stored) ||
                (galaxy.state == events::GalaxyState::Ready && stored < 2.0f);
            if (!worthUploading) {
                continue;
            }

            const events::CloudBuffers* cloud = starMap->cloud(galaxy.galaxyId);
            if (!cloud) {
                continue;  // superseded since the fact
            }

            GalaxyGlWidget::GalaxyCloudData data;
            data.orbitA = cloud->orbitA;
            data.orbitB = cloud->orbitB;
            data.theta0 = cloud->theta0;
            data.velTheta = cloud->velTheta;
            data.tiltAngle = cloud->tiltAngle;
            data.colors = cloud->colors;
            data.mags = cloud->mags;
            data.kinds = cloud->kinds;
            data.params.radCoreN = cloud->params.radCoreN;
            data.params.radFarFieldN = cloud->params.radFarFieldN;
            data.params.exInner = cloud->params.exInner;
            data.params.exOuter = cloud->params.exOuter;
            data.params.angleOffsetN = cloud->params.angleOffsetN;
            data.params.barRadiusN = cloud->params.barRadiusN;
            data.params.barEx = cloud->params.barEx;
            data.params.pertN = cloud->params.pertN;
            data.params.pertAmp = cloud->params.pertAmp;
            data.params.dustRenderSize = cloud->params.dustRenderSize;
            data.params.h2SizeMax = cloud->params.h2SizeMax;
            data.params.h2Threshold = cloud->params.h2Threshold;
            canvas_->setGalaxyCloud(galaxy.galaxyId, mySlot, data);
            uploadedFraction_[galaxy.galaxyId] =
                galaxy.state == events::GalaxyState::Ready ? 2.0f : galaxy.progress;
        }
    }

    // 4. Status row.
    progressBar_->setRange(0, std::max(fact.total, 1));
    progressBar_->setValue(fact.ready);
    runningLabel_->setText(
        QStringLiteral("<span style='color:#4caf50;'>●</span> running %1").arg(fact.generating));
    queuedLabel_->setText(QStringLiteral("<span style='color:#2a7fff;'>●</span> queued %1").arg(fact.queued));
    generationLabel_->setText(
        QStringLiteral("<span style='color:#b073ff;'>●</span> generation %1").arg(fact.epoch));
    if (fact.staleArrivals > 0) {
        staleLabel_->setText(QStringLiteral("stale %1").arg(fact.staleArrivals));
        staleLabel_->setVisible(true);
    } else {
        staleLabel_->setVisible(false);
    }

    // 5. Worker lanes, keyed by slot.
    for (const events::WorkerInfo& worker : fact.workers) {
        WorkerLane& lane = laneFor(worker.slot);
        lane.busy = worker.busy;
        lane.threadLabel->setText(
            QStringLiteral("worker %1 · 0x%2").arg(worker.slot + 1).arg(worker.threadId, 0, 16));
        lane.dot->setStyleSheet(worker.busy ? QStringLiteral("color: #4caf50;")
                                             : QStringLiteral("color: #6b6b6b;"));
        lane.countLabel->setText(QString::number(worker.completedCount));
        lane.line2->setText(worker.busy ? QStringLiteral("galaxy #%1 · %2")
                                               .arg(worker.currentGalaxyId)
                                               .arg(sectorLabelFor(fact, worker.currentGalaxyId))
                                         : QStringLiteral("idle"));

        // A galaxy missing from the board (stale) reads as 0.
        int progressPct = 0;
        if (worker.busy) {
            for (const events::GalaxyStatus& galaxy : fact.galaxies) {
                if (galaxy.galaxyId == worker.currentGalaxyId) {
                    progressPct = galaxy.state == events::GalaxyState::Ready
                                      ? 100
                                      : static_cast<int>(galaxy.progress * 100.0f);
                    break;
                }
            }
        }
        lane.bar->setValue(progressPct);
    }
    updateLaneVisibility();
}

void JumpPresenter::setWorkerLimit(int n) {
    workerLimit_ = n;
    updateLaneVisibility();
}

void JumpPresenter::updateLaneVisibility() {
    for (auto& [slot, lane] : lanes_) {
        lane.card->setVisible(slot < workerLimit_ || lane.busy);
    }
}

void JumpPresenter::onTransitTick() {
    // Clamped so a stall can't skip part of the journey.
    transitElapsedMs_ += std::min<qint64>(transitClock_.restart(), kTickClampMs);
    const float t =
        std::clamp(static_cast<float>(transitElapsedMs_) / static_cast<float>(kTransitMs), 0.0f, 1.0f);
    canvas_->setJumpVisual(GalaxyGlWidget::JumpVisual::Jumping, t);

    if (transitElapsedMs_ >= kTransitMs) {
        // Stop first: send() re-enters onStarMapChanged synchronously.
        tickTimer_.stop();
        context().send(events::JumpArrivalReached{});
    }
}

QString JumpPresenter::sectorLabelFor(const events::StarMapChanged& fact, std::uint64_t galaxyId) {
    for (const events::GalaxyStatus& galaxy : fact.galaxies) {
        if (galaxy.galaxyId == galaxyId) {
            const events::SectorCoord& sector =
                galaxy.board == events::Board::Destination ? fact.destinationSector : fact.currentSector;
            return QStringLiteral("sector (%1,%2)").arg(sector.x).arg(sector.y);
        }
    }
    return QStringLiteral("sector (?,?)");
}

JumpPresenter::WorkerLane& JumpPresenter::laneFor(int slot) {
    auto it = lanes_.find(slot);
    if (it != lanes_.end()) {
        return it->second;
    }

    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("workerLane"));
    card->setFrameShape(QFrame::StyledPanel);
    auto* cardLayout = new QVBoxLayout(card);

    auto* headerRow = new QHBoxLayout;
    auto* dot = new QLabel(QStringLiteral("●"));
    auto* threadLabel = new QLabel(QStringLiteral("worker %1").arg(slot + 1));
    auto* countLabel = new QLabel(QStringLiteral("0"));
    headerRow->addWidget(dot);
    headerRow->addWidget(threadLabel);
    headerRow->addStretch();
    headerRow->addWidget(countLabel);
    cardLayout->addLayout(headerRow);

    auto* line2 = new QLabel(QStringLiteral("idle"));
    cardLayout->addWidget(line2);

    auto* bar = new QProgressBar;
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setTextVisible(false);
    bar->setFixedHeight(6);
    cardLayout->addWidget(bar);

    // Slot order; the column's widgets before the lanes are not ours, so
    // anchor on the first lane card, else the trailing stretch.
    const auto next = lanes_.upper_bound(slot);
    const int index = next != lanes_.end() ? laneColumn_->indexOf(next->second.card) : laneColumn_->count() - 1;
    laneColumn_->insertWidget(index, card);

    return lanes_.emplace(slot, WorkerLane{card, threadLabel, false, dot, countLabel, line2, bar}).first->second;
}

}  // namespace app
