#include "view/shelf_presenter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <QFrame>
#include <QHBoxLayout>

#include "domain/part_shelf.h"
#include "domain/regenerate_requested.h"

namespace app {

void ShelfPresenter::regenerate(std::uint64_t seed, int partCount, int smoothIters, int aoRaysPerVertex) {
    context().send(events::RegenerateRequested{seed, partCount, smoothIters, aoRaysPerVertex});
}

void ShelfPresenter::onShelfChanged(const events::ShelfChanged& fact) {
    // A new generation means a new grid. No real fact ever carries
    // generation 0 (beginBatch increments first), so lastGeneration_'s
    // initial 0 safely fires this branch on the first fact too.
    if (fact.generation != lastGeneration_) {
        canvas_->beginGeneration(fact.total);
        uploadedFraction_.clear();
        lastGeneration_ = fact.generation;
    }

    // Pulls the mesh for every part whose on-screen preview is behind: a
    // Running part whose progress moved past last upload, or a Baked part
    // not yet uploaded. Runs synchronously so the agent's buffers stay
    // valid while the canvas copies them.
    auto shelf = context().agentAs<PartShelf>(PartShelf::kName);
    if (shelf) {
        for (std::size_t slotIndex = 0; slotIndex < fact.parts.size(); ++slotIndex) {
            const events::PartStatus& part = fact.parts[slotIndex];

            const auto storedIt = uploadedFraction_.find(part.partId);
            const float stored = storedIt != uploadedFraction_.end() ? storedIt->second : -1.0f;
            const bool worthUploading =
                (part.state == events::PartState::Running && part.progress > stored) ||
                (part.state == events::PartState::Baked && stored < 2.0f);
            if (!worthUploading) {
                continue;
            }

            // shelf->mesh() hands back previews too (partial, sentinel-mixed)
            // as well as finished meshes -- one pull serves both cases.
            const events::MeshBuffers* mesh = shelf->mesh(part.partId);
            if (!mesh) {
                continue;  // superseded between this fact and the pull -- skip, not fatal
            }

            ShelfGlWidget::PartMeshData data;
            data.positions = mesh->positions;
            data.normals = mesh->normals;
            data.ao = mesh->ao;
            data.indices.assign(mesh->indices.begin(), mesh->indices.end());  // uint32_t -> quint32
            canvas_->setPartMesh(part.partId, static_cast<int>(slotIndex), data);
            uploadedFraction_[part.partId] =
                part.state == events::PartState::Baked ? 2.0f : part.progress;
        }
    }

    // Status row.
    progressText_->setText(QStringLiteral("%1 / %2 parts").arg(fact.baked).arg(fact.total));
    progressBar_->setRange(0, std::max(fact.total, 1));
    progressBar_->setValue(fact.baked);
    statsLabel_->setText(QStringLiteral("running %1 · queued %2 · failed %3 · gen %4 · stale %5")
                              .arg(fact.running)
                              .arg(fact.queued)
                              .arg(fact.failed)
                              .arg(fact.generation)
                              .arg(fact.discardedArrivals));

    // One card per worker slot, created on first sight.
    for (const events::WorkerInfo& worker : fact.workers) {
        WorkerLane& lane = laneFor(worker.slot);
        lane.busy = worker.busy;
        lane.threadLabel->setText(
            QStringLiteral("worker %1 · 0x%2").arg(worker.slot + 1).arg(worker.threadId, 0, 16));
        lane.dot->setStyleSheet(worker.busy ? QStringLiteral("color: #4caf50;")
                                             : QStringLiteral("color: #9e9e9e;"));

        // Part may be momentarily missing from fact.parts at a generation
        // boundary -- fall back to just the part id.
        QString line2Text = QStringLiteral("idle");
        if (worker.busy) {
            line2Text = QStringLiteral("part %1").arg(worker.currentPartId);
            for (const events::PartStatus& part : fact.parts) {
                if (part.partId == worker.currentPartId) {
                    const int percent = static_cast<int>(std::lround(part.progress * 100.0f));
                    line2Text = QStringLiteral("part %1 · %2%").arg(worker.currentPartId).arg(percent);
                    break;
                }
            }
        }
        lane.line2->setText(line2Text);
        lane.line3->setText(QStringLiteral("completed: %1").arg(worker.completedCount));
    }
    updateLaneVisibility();
}

void ShelfPresenter::setWorkerLimit(int n) {
    workerLimit_ = n;
    updateLaneVisibility();
}

void ShelfPresenter::updateLaneVisibility() {
    for (auto& [slot, lane] : lanes_) {
        lane.card->setVisible(slot < workerLimit_ || lane.busy);
    }
}

ShelfPresenter::WorkerLane& ShelfPresenter::laneFor(int slot) {
    auto it = lanes_.find(slot);
    if (it != lanes_.end()) {
        return it->second;
    }

    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("workerLane"));
    card->setFrameShape(QFrame::StyledPanel);
    auto* cardLayout = new QVBoxLayout(card);

    auto* headerRow = new QHBoxLayout;
    auto* dot = new QLabel(QStringLiteral("●"));  // colored via stylesheet, below
    auto* threadLabel = new QLabel(QStringLiteral("worker %1").arg(slot + 1));
    headerRow->addWidget(dot);
    headerRow->addWidget(threadLabel);
    headerRow->addStretch();
    cardLayout->addLayout(headerRow);

    auto* line2 = new QLabel(QStringLiteral("idle"));
    auto* line3 = new QLabel(QStringLiteral("completed: 0"));
    cardLayout->addWidget(line2);
    cardLayout->addWidget(line3);

    // Slot order: before the next-higher slot's card, else at the end.
    const auto next = lanes_.upper_bound(slot);
    if (next != lanes_.end()) {
        laneColumn_->insertWidget(laneColumn_->indexOf(next->second.card), card);
    } else {
        laneColumn_->addWidget(card);
    }

    return lanes_.emplace(slot, WorkerLane{card, threadLabel, false, dot, line2, line3}).first->second;
}

}  // namespace app
