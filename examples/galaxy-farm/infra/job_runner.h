#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <QObject>
#include <QThreadPool>

#include "domain/cloud_buffers.h"
#include "domain/galaxy_job_events.h"
#include "domain/star_map.h"

namespace app {

// The only path from worker threads back to the UI thread: post* methods
// are callable from any thread and deliver queued. Must live on the UI thread.
class GenRelay : public QObject {
public:
    explicit GenRelay(QObject* parent = nullptr) : QObject(parent) {}

    // Run on the UI thread, so they may call into the kernel.
    std::function<void(const events::GalaxyJobStarted&)> onStarted;
    std::function<void(const events::GalaxyJobProgress&)> onProgress;
    std::function<void(const events::GalaxyJobFinished&)> onFinished;

    void postStarted(std::uint64_t galaxyId, std::uint64_t epoch, int slot, std::uint64_t threadId);

    // `snapshot` is a finished prefix of the cloud.
    void postProgress(std::uint64_t galaxyId, std::uint64_t epoch, std::uint64_t threadId, float fraction,
                       std::shared_ptr<const events::CloudBuffers> snapshot);

    void postFinished(std::uint64_t galaxyId, std::uint64_t epoch, int slot, std::uint64_t threadId, bool ok,
                       std::shared_ptr<const events::CloudBuffers> cloud);
};

// Runs generations on a bounded worker pool. Job code may touch only the
// galaxylib C API and GenRelay's post* methods -- never the kernel, StarMap
// or widgets.
class JobRunner {
public:
    explicit JobRunner(GenRelay& relay) : relay_(relay) {
        // Idle threads would otherwise expire and return with new ids (new worker lanes).
        pool_.setExpiryTimeout(-1);
    }

    // Clamped to at least 1; a zero-thread pool never runs anything.
    void setMaxThreads(int n);

    // Submits one job per entry, all stamped with `epoch`; older epochs are skipped at start.
    void enqueue(std::uint64_t epoch, const std::vector<StarMap::JumpJob>& jobs);

private:
    // Lowest free concurrency slot. At most maxThreads jobs run at once, so
    // after a shrink to N new jobs only ever get slots 0..N-1.
    int acquireSlot();
    void releaseSlot(int slot);

    GenRelay& relay_;
    // Declared before pool_: the pool's destructor waits for jobs that use them.
    std::mutex slotMutex_;
    std::vector<bool> slotBusy_;
    // Private pool, not QThreadPool::globalInstance().
    QThreadPool pool_;
    std::atomic<std::uint64_t> currentEpoch_{0};
};

}  // namespace app
