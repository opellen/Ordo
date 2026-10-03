#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <QObject>
#include <QThreadPool>

#include "domain/bake_job_events.h"
#include "domain/mesh_buffers.h"
#include "domain/part_shelf.h"

namespace app {

// Marshalling seam: every worker thread's result crosses to the UI thread
// through this object's post* methods. Uses invokeMethod-with-lambda
// instead of declared signals, avoiding moc.
class BakeRelay : public QObject {
public:
    explicit BakeRelay(QObject* parent = nullptr) : QObject(parent) {}

    // Wired once at bootstrap, e.g. to kernel.send<events::BakeJobStarted>.
    // Run on the UI thread, so touching the ordo dispatcher inside is safe.
    std::function<void(const events::BakeJobStarted&)> onStarted;
    std::function<void(const events::BakeJobFinished&)> onFinished;
    std::function<void(const events::BakeJobProgress&)> onProgress;

    // Worker-thread callable: queues the notification onto the UI thread.
    // Safe from any thread; BakeRelay holds no mutable state.
    void postStarted(std::uint64_t partId, std::uint64_t generation, int slot, std::uint64_t threadId);

    // Worker-thread callable: a worker finished a job, successfully or not.
    void postFinished(std::uint64_t partId, std::uint64_t generation, int slot, std::uint64_t threadId, bool ok,
                       std::shared_ptr<const events::MeshBuffers> buffers);

    // Worker-thread callable: a bake hit an AO checkpoint. `snapshot` rides
    // the queued lambda so it stays alive until onProgress runs.
    void postProgress(std::uint64_t partId, std::uint64_t generation, std::uint64_t threadId, float fraction,
                       std::shared_ptr<const events::MeshBuffers> snapshot);
};

// Runs bakes on a bounded worker-thread pool. Code inside a submitted
// QRunnable may only touch the worklib C API and BakeRelay's post*
// methods -- never the ordo dispatcher, PartShelf, or Qt widgets.
class JobRunner {
public:
    explicit JobRunner(BakeRelay& relay) : relay_(relay) {}

    // Clamped to at least 1: a max of 0 threads would never drain the queue.
    void setMaxThreads(int n);

    // Submits one QRunnable per job, all stamped with `generation`.
    void enqueue(std::uint64_t generation, const std::vector<PartShelf::BatchJob>& jobs, int smoothIters,
                 int aoRaysPerVertex);

private:
    // Lowest free concurrency slot. At most maxThreads jobs run at once, so
    // after a shrink to N new jobs only ever get slots 0..N-1.
    int acquireSlot();
    void releaseSlot(int slot);

    BakeRelay& relay_;
    // Declared before pool_: the pool's destructor waits for jobs that use them.
    std::mutex slotMutex_;
    std::vector<bool> slotBusy_;
    // Own pool, not QThreadPool::globalInstance(): isolated from the rest
    // of the process.
    QThreadPool pool_;
    std::atomic<std::uint64_t> currentGeneration_{0};
};

}  // namespace app
