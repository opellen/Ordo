#include "domain/regenerate_command.h"

#include <algorithm>

#include "domain/part_shelf.h"

namespace app {

void RegenerateCommand::execute(const events::RegenerateRequested& event, ordo::core::CommandContext& context) {
    // Clamp every dial to a sane range before queuing anything.
    const int partCount = std::clamp(event.partCount, 1, 64);
    const int smoothIters = std::clamp(event.smoothIters, 0, 10000);
    const int aoRaysPerVertex = std::clamp(event.aoRaysPerVertex, 1, 4096);

    auto shelf = context.agentAs<PartShelf>(PartShelf::kName);
    if (!shelf) {
        return;  // the shelf is always registered in this app
    }

    std::uint64_t generation = 0;
    auto jobs = shelf->beginBatch(event.seed, partCount, generation);
    runner_.enqueue(generation, jobs, smoothIters, aoRaysPerVertex);
}

}  // namespace app
