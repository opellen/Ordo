#pragma once

#include <cstdint>
#include <string>

namespace app {

// Output port for the add-task use case. The view implements it; the domain
// layer never learns who did. Only caller-directed outcomes belong here --
// outcomes every view cares about stay broadcast facts (TaskAdded).
class TaskOutput {
public:
    virtual ~TaskOutput() = default;

    // Rejected before any mutation -- meaningful only to the caller.
    virtual void taskRejected(const std::string& reason) = 0;

    // Echo to the caller specifically; events::TaskAdded still broadcasts.
    virtual void taskAccepted(std::uint64_t id, const std::string& title) = 0;
};

}  // namespace app
