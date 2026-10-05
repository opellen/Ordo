#include "infra/job_runner.h"

#include <cstddef>
#include <utility>

#include <QMetaObject>
#include <QRunnable>
#include <QThread>

#include <worklib/worklib.h>

namespace app {

void BakeRelay::postStarted(std::uint64_t partId, std::uint64_t generation, int slot, std::uint64_t threadId) {
    QMetaObject::invokeMethod(
        this,
        [this, partId, generation, slot, threadId] {
            if (onStarted) {
                onStarted(events::BakeJobStarted{partId, generation, slot, threadId});
            }
        },
        Qt::QueuedConnection);
}

void BakeRelay::postFinished(std::uint64_t partId, std::uint64_t generation, int slot, std::uint64_t threadId,
                              bool ok, std::shared_ptr<const events::MeshBuffers> buffers) {
    QMetaObject::invokeMethod(
        this,
        [this, partId, generation, slot, threadId, ok, buffers = std::move(buffers)] {
            if (onFinished) {
                onFinished(events::BakeJobFinished{partId, generation, slot, threadId, ok, buffers});
            }
        },
        Qt::QueuedConnection);
}

void BakeRelay::postProgress(std::uint64_t partId, std::uint64_t generation, std::uint64_t threadId, float fraction,
                              std::shared_ptr<const events::MeshBuffers> snapshot) {
    QMetaObject::invokeMethod(
        this,
        [this, partId, generation, threadId, fraction, snapshot = std::move(snapshot)] {
            if (onProgress) {
                onProgress(events::BakeJobProgress{partId, generation, threadId, fraction, snapshot});
            }
        },
        Qt::QueuedConnection);
}

namespace {

// Handed to worklib_bake_part as `user`, read back in progressThunk.
// Lives only for the duration of one worklib_bake_part call.
struct ProgressContext {
    BakeRelay* relay;
    std::uint64_t partId;
    std::uint64_t generation;
    std::uint64_t threadId;
};

// Bridges one worklib checkpoint back into BakeRelay::postProgress, on the
// same worker thread as the bake, mid-call.
// No cancellation: a stale bake runs to completion and is dropped on arrival.
void progressThunk(const WorklibMesh* snapshot, float fraction, void* user) {
    auto* ctx = static_cast<ProgressContext*>(user);

    // positions/normals: xyz triple per vertex; ao: one scalar per vertex;
    // indices: one triangle triple per entry.
    events::MeshBuffers buffers;
    buffers.positions.assign(snapshot->positions,
                              snapshot->positions + static_cast<std::size_t>(snapshot->vertexCount) * 3);
    buffers.normals.assign(snapshot->normals,
                            snapshot->normals + static_cast<std::size_t>(snapshot->vertexCount) * 3);
    buffers.ao.assign(snapshot->aoValues, snapshot->aoValues + snapshot->vertexCount);
    buffers.indices.assign(snapshot->indices,
                            snapshot->indices + static_cast<std::size_t>(snapshot->triangleCount) * 3);

    ctx->relay->postProgress(ctx->partId, ctx->generation, ctx->threadId, fraction,
                              std::make_shared<const events::MeshBuffers>(std::move(buffers)));
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

void JobRunner::enqueue(std::uint64_t generation, const std::vector<PartShelf::BatchJob>& jobs, int smoothIters,
                         int aoRaysPerVertex) {
    currentGeneration_.store(generation);

    for (const PartShelf::BatchJob& job : jobs) {
        JobRunner* const self = this;
        BakeRelay* const relay = &relay_;
        std::atomic<std::uint64_t>* const currentGeneration = &currentGeneration_;
        const std::uint64_t partId = job.partId;
        const std::uint64_t partSeed = job.partSeed;

        // QRunnable::create's result auto-deletes itself after run().
        pool_.start(QRunnable::create([self, relay, currentGeneration, partId, partSeed, generation, smoothIters,
                                        aoRaysPerVertex] {
            // Stale-start check: a newer RegenerateRequested may have
            // superseded this batch before the runnable reached a worker
            // thread. Skip worklib and the relay silently if so.
            if (generation != currentGeneration->load()) {
                return;
            }

            const int slot = self->acquireSlot();
            const std::uint64_t threadId = reinterpret_cast<std::uint64_t>(QThread::currentThreadId());
            relay->postStarted(partId, generation, slot, threadId);

            ProgressContext progressCtx{relay, partId, generation, threadId};

            WorklibMesh raw{};
            const int result = worklib_bake_part(partSeed, static_cast<std::uint32_t>(smoothIters),
                                                  static_cast<std::uint32_t>(aoRaysPerVertex),
                                                  &progressThunk, &progressCtx, &raw);
            const bool ok = (result == 0);

            // Copy worklib's raw buffers unconditionally -- on failure `raw`
            // is zeroed, so `ok` (not the buffers) tells the shelf the result.
            events::MeshBuffers buffers;
            buffers.positions.assign(raw.positions,
                                      raw.positions + static_cast<std::size_t>(raw.vertexCount) * 3);
            buffers.normals.assign(raw.normals, raw.normals + static_cast<std::size_t>(raw.vertexCount) * 3);
            buffers.ao.assign(raw.aoValues, raw.aoValues + raw.vertexCount);
            buffers.indices.assign(raw.indices, raw.indices + static_cast<std::size_t>(raw.triangleCount) * 3);
            worklib_free_mesh(&raw);

            relay->postFinished(partId, generation, slot, threadId, ok,
                                 std::make_shared<const events::MeshBuffers>(std::move(buffers)));
            // Release after posting: the slot's next Started must queue behind this Finished.
            self->releaseSlot(slot);
        }));
    }
}

}  // namespace app
