#include "domain/part_shelf.h"

namespace app {

namespace detail {

// One splitmix64 step (Sebastiano Vigna's public-domain mixer): scrambles a
// 64-bit input. Local rather than pulling in <random> for one function.
inline std::uint64_t splitmix64(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = x;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// Combines the batch's seed with a part's index into one per-part seed.
// Same (seed, index) always yields the same part.
inline std::uint64_t mix(std::uint64_t seed, int index) {
    return splitmix64(seed + static_cast<std::uint64_t>(index));
}

}  // namespace detail

std::vector<PartShelf::BatchJob> PartShelf::beginBatch(std::uint64_t seed, int partCount,
                                                        std::uint64_t& outGeneration) {
    ++generation_;
    parts_.clear();

    std::vector<BatchJob> jobs;
    jobs.reserve(static_cast<std::size_t>(partCount));
    for (int index = 0; index < partCount; ++index) {
        const std::uint64_t partId = nextPartId_++;  // a global counter, never reused
        const std::uint64_t partSeed = detail::mix(seed, index);
        parts_.push_back(Part{partId, partSeed, events::PartState::Queued});
        jobs.push_back(BatchJob{partId, partSeed});
    }

    outGeneration = generation_;
    publishSnapshot();
    return jobs;
}

void PartShelf::markStarted(std::uint64_t partId, std::uint64_t generation, int slot, std::uint64_t threadId) {
    if (generation != generation_) {
        // Stale start: beginBatch already dropped this part's whole batch.
        // Guards the same race JobRunner checks closer to the source.
        return;
    }

    Part* part = findPart(partId);
    if (!part) {
        return;  // defensive: shouldn't happen for a live generation
    }
    part->state = events::PartState::Running;
    part->threadId = threadId;

    events::WorkerInfo& worker = workers_[slot];
    worker.slot = slot;
    worker.threadId = threadId;
    worker.busy = true;
    worker.currentPartId = partId;

    publishSnapshot();
}

void PartShelf::storeBaked(std::uint64_t partId, std::uint64_t generation, int slot, std::uint64_t threadId, bool ok,
                            std::shared_ptr<const events::MeshBuffers> buffers) {
    events::WorkerInfo& worker = workers_[slot];
    worker.slot = slot;
    worker.threadId = threadId;
    worker.busy = false;
    worker.currentPartId = 0;

    if (generation != generation_) {
        // Stale result: part already dropped by a newer beginBatch.
        // Worker status above still stands; `buffers` is simply dropped.
        ++discardedArrivals_;
        publishSnapshot();
        return;
    }

    Part* part = findPart(partId);
    if (!part) {
        publishSnapshot();  // defensive: shouldn't happen for a live generation
        return;
    }

    part->state = ok ? events::PartState::Baked : events::PartState::Failed;
    part->threadId = threadId;
    if (ok) {
        part->buffers = std::move(buffers);
        part->progress = 1.0f;
    }
    ++worker.completedCount;

    publishSnapshot();
}

void PartShelf::markProgress(std::uint64_t partId, std::uint64_t generation, float fraction,
                              std::shared_ptr<const events::MeshBuffers> snapshot) {
    if (generation != generation_) {
        return;
    }

    Part* part = findPart(partId);
    if (!part) {
        return;  // defensive: shouldn't happen for a live generation
    }
    part->progress = fraction;
    part->buffers = std::move(snapshot);

    publishSnapshot();
}

const events::MeshBuffers* PartShelf::mesh(std::uint64_t partId) const {
    const Part* part = findPart(partId);
    if (!part || !part->buffers) {
        return nullptr;
    }
    return part->buffers.get();
}

std::vector<events::PartStatus> PartShelf::buildPartStatuses() const {
    std::vector<events::PartStatus> statuses;
    statuses.reserve(parts_.size());
    for (const Part& part : parts_) {
        statuses.push_back(events::PartStatus{part.partId, part.state, part.threadId, part.progress});
    }
    return statuses;
}

std::vector<events::WorkerInfo> PartShelf::buildWorkerInfos() const {
    std::vector<events::WorkerInfo> infos;
    infos.reserve(workers_.size());
    for (const auto& [slot, info] : workers_) {
        infos.push_back(info);
    }
    return infos;
}

void PartShelf::publishSnapshot() {
    int queued = 0;
    int running = 0;
    int baked = 0;
    int failed = 0;
    for (const Part& part : parts_) {
        switch (part.state) {
            case events::PartState::Queued: ++queued; break;
            case events::PartState::Running: ++running; break;
            case events::PartState::Baked: ++baked; break;
            case events::PartState::Failed: ++failed; break;
        }
    }

    context().send(events::ShelfChanged{
        .generation = generation_,
        .queued = queued,
        .running = running,
        .baked = baked,
        .failed = failed,
        .total = static_cast<int>(parts_.size()),
        .discardedArrivals = discardedArrivals_,
        .parts = buildPartStatuses(),
        .workers = buildWorkerInfos(),
    });
}

}  // namespace app
