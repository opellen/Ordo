#include "infra/job_runner.h"

#include <cstddef>
#include <utility>

#include <QMetaObject>
#include <QRunnable>
#include <QThread>

#include <galaxylib/galaxylib.h>

namespace app {

void GenRelay::postStarted(std::uint64_t galaxyId, std::uint64_t epoch, int slot, std::uint64_t threadId) {
    QMetaObject::invokeMethod(
        this,
        [this, galaxyId, epoch, slot, threadId] {
            if (onStarted) {
                onStarted(events::GalaxyJobStarted{galaxyId, epoch, slot, threadId});
            }
        },
        Qt::QueuedConnection);
}

void GenRelay::postProgress(std::uint64_t galaxyId, std::uint64_t epoch, std::uint64_t threadId, float fraction,
                             std::shared_ptr<const events::CloudBuffers> snapshot) {
    QMetaObject::invokeMethod(
        this,
        [this, galaxyId, epoch, threadId, fraction, snapshot = std::move(snapshot)] {
            if (onProgress) {
                onProgress(events::GalaxyJobProgress{galaxyId, epoch, threadId, fraction, snapshot});
            }
        },
        Qt::QueuedConnection);
}

void GenRelay::postFinished(std::uint64_t galaxyId, std::uint64_t epoch, int slot, std::uint64_t threadId,
                             bool ok, std::shared_ptr<const events::CloudBuffers> cloud) {
    QMetaObject::invokeMethod(
        this,
        [this, galaxyId, epoch, slot, threadId, ok, cloud = std::move(cloud)] {
            if (onFinished) {
                onFinished(events::GalaxyJobFinished{galaxyId, epoch, slot, threadId, ok, cloud});
            }
        },
        Qt::QueuedConnection);
}

namespace {

// progressThunk's `user` data; lives on the runnable's stack for one call.
struct ProgressContext {
    GenRelay* relay;
    std::uint64_t galaxyId;
    std::uint64_t epoch;
    std::uint64_t threadId;
};

// Runs on the worker thread mid-generation. No cancellation: stale
// generations run to completion and are discarded on arrival.
void progressThunk(const GalaxylibCloud* snapshot, float fraction, void* user) {
    auto* ctx = static_cast<ProgressContext*>(user);

    // Copy the finished prefix (starCount is the total across all kinds).
    events::CloudBuffers buffers;
    const std::size_t n = snapshot->starCount;
    buffers.orbitA.assign(snapshot->orbitA, snapshot->orbitA + n);
    buffers.orbitB.assign(snapshot->orbitB, snapshot->orbitB + n);
    buffers.theta0.assign(snapshot->theta0, snapshot->theta0 + n);
    buffers.velTheta.assign(snapshot->velTheta, snapshot->velTheta + n);
    buffers.tiltAngle.assign(snapshot->tiltAngle, snapshot->tiltAngle + n);
    buffers.colors.assign(snapshot->colors, snapshot->colors + n * 3);
    buffers.mags.assign(snapshot->mags, snapshot->mags + n);
    buffers.kinds.assign(snapshot->kinds, snapshot->kinds + n);
    buffers.params = snapshot->params;

    ctx->relay->postProgress(ctx->galaxyId, ctx->epoch, ctx->threadId, fraction,
                              std::make_shared<const events::CloudBuffers>(std::move(buffers)));
}

}  // namespace

void JobRunner::setMaxThreads(int n) { pool_.setMaxThreadCount(n < 1 ? 1 : n); }

int JobRunner::acquireSlot() {
    std::lock_guard lock(slotMutex_);
    for (std::size_t i = 0; i < slotBusy_.size(); ++i) {
        if (!slotBusy_[i]) {
            slotBusy_[i] = true;
            return static_cast<int>(i);
        }
    }
    slotBusy_.push_back(true);
    return static_cast<int>(slotBusy_.size() - 1);
}

void JobRunner::releaseSlot(int slot) {
    std::lock_guard lock(slotMutex_);
    slotBusy_[static_cast<std::size_t>(slot)] = false;
}

void JobRunner::enqueue(std::uint64_t epoch, const std::vector<StarMap::JumpJob>& jobs) {
    currentEpoch_.store(epoch);

    for (const StarMap::JumpJob& job : jobs) {
        JobRunner* const self = this;
        GenRelay* const relay = &relay_;
        std::atomic<std::uint64_t>* const currentEpoch = &currentEpoch_;
        const std::uint64_t galaxyId = job.galaxyId;
        const std::uint64_t seed = job.seed;
        const auto type = static_cast<std::uint32_t>(job.type);
        const auto starCount = static_cast<std::uint32_t>(job.starCount);

        pool_.start(QRunnable::create([self, relay, currentEpoch, galaxyId, seed, type, starCount, epoch] {
            // Superseded by a newer jump: skip silently.
            if (epoch != currentEpoch->load()) {
                return;
            }

            const int slot = self->acquireSlot();
            const std::uint64_t threadId = reinterpret_cast<std::uint64_t>(QThread::currentThreadId());
            relay->postStarted(galaxyId, epoch, slot, threadId);

            ProgressContext progressCtx{relay, galaxyId, epoch, threadId};

            GalaxylibCloud raw{};
            const int result =
                galaxylib_generate_galaxy(seed, type, starCount, &progressThunk, &progressCtx, &raw);
            const bool ok = (result == 0);

            // On failure `raw` is zeroed; `ok` is what signals success.
            events::CloudBuffers buffers;
            const std::size_t n = raw.starCount;
            buffers.orbitA.assign(raw.orbitA, raw.orbitA + n);
            buffers.orbitB.assign(raw.orbitB, raw.orbitB + n);
            buffers.theta0.assign(raw.theta0, raw.theta0 + n);
            buffers.velTheta.assign(raw.velTheta, raw.velTheta + n);
            buffers.tiltAngle.assign(raw.tiltAngle, raw.tiltAngle + n);
            buffers.colors.assign(raw.colors, raw.colors + n * 3);
            buffers.mags.assign(raw.mags, raw.mags + n);
            buffers.kinds.assign(raw.kinds, raw.kinds + n);
            buffers.params = raw.params;
            galaxylib_free_cloud(&raw);

            relay->postFinished(galaxyId, epoch, slot, threadId, ok,
                                 std::make_shared<const events::CloudBuffers>(std::move(buffers)));
            // Release after posting: the slot's next Started must queue behind this Finished.
            self->releaseSlot(slot);
        }));
    }
}

}  // namespace app
