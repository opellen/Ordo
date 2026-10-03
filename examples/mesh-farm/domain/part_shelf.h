#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include <ordo/core/agent.h>

#include "domain/mesh_buffers.h"
#include "domain/shelf_changed.h"

namespace app {

// Owns every part ever queued and the worker pool's last-known status;
// broadcasts every mutation as one ShelfChanged fact. Generations track
// staleness: each RegenerateRequested bumps generation_, so a late result
// naming an already-superseded part is dropped instead of resurrecting it.
class PartShelf : public ordo::core::Agent {
public:
    static constexpr const char* kName = "shelf";

    PartShelf() : Agent(kName) {}

    // One unit of work handed to JobRunner::enqueue: just the two numbers a
    // worker thread needs to call worklib_bake_part.
    struct BatchJob {
        std::uint64_t partId = 0;
        std::uint64_t partSeed = 0;
    };

    // Starts a new batch: drops the whole previous board immediately, so
    // late arrivals for those parts count toward discardedArrivals instead
    // of growing the board. Writes the new generation to outGeneration.
    std::vector<BatchJob> beginBatch(std::uint64_t seed, int partCount, std::uint64_t& outGeneration);

    // A worker thread picked up partId's job (via BakeJobStartedCommand).
    void markStarted(std::uint64_t partId, std::uint64_t generation, int slot, std::uint64_t threadId);

    // A worker thread finished partId's job, successfully or not.
    void storeBaked(std::uint64_t partId, std::uint64_t generation, int slot, std::uint64_t threadId, bool ok,
                     std::shared_ptr<const events::MeshBuffers> buffers);

    // A worker thread's bake reached an AO checkpoint. Unlike storeBaked, a
    // stale generation here is dropped silently without counting toward
    // discardedArrivals, since progress is ephemeral.
    void markProgress(std::uint64_t partId, std::uint64_t generation, float fraction,
                       std::shared_ptr<const events::MeshBuffers> snapshot);

    // The latest mesh worth showing for partId, or nullptr if there's none
    // yet. A Running part's mesh is a partial preview (its ao mixes real
    // values with worklib's -1.0 sentinel); a Baked part's mesh is final.
    const events::MeshBuffers* mesh(std::uint64_t partId) const;

    std::uint64_t generation() const { return generation_; }
    int discardedArrivals() const { return discardedArrivals_; }

    // Read-only snapshots matching ShelfChanged's shape, for callers that
    // want current state without waiting on a broadcast.
    std::vector<events::PartStatus> partStatuses() const { return buildPartStatuses(); }
    std::vector<events::WorkerInfo> workerInfos() const { return buildWorkerInfos(); }

private:
    // Everything this agent tracks about one part; never leaves this file.
    struct Part {
        std::uint64_t partId = 0;
        std::uint64_t partSeed = 0;
        events::PartState state = events::PartState::Queued;
        std::uint64_t threadId = 0;
        float progress = 0.0f;
        std::shared_ptr<const events::MeshBuffers> buffers;
    };

    Part* findPart(std::uint64_t partId) {
        for (Part& part : parts_) {
            if (part.partId == partId) {
                return &part;
            }
        }
        return nullptr;
    }

    const Part* findPart(std::uint64_t partId) const {
        for (const Part& part : parts_) {
            if (part.partId == partId) {
                return &part;
            }
        }
        return nullptr;
    }

    std::vector<events::PartStatus> buildPartStatuses() const;

    // Ascending by slot.
    std::vector<events::WorkerInfo> buildWorkerInfos() const;

    // Recomputes every derived count and broadcasts one fact. Counts are
    // scoped to the current batch; discardedArrivals counts notifications,
    // not parts.
    void publishSnapshot();

    std::vector<Part> parts_;
    std::map<int, events::WorkerInfo> workers_;
    std::uint64_t generation_ = 0;
    std::uint64_t nextPartId_ = 1;
    int discardedArrivals_ = 0;
};

}  // namespace app
