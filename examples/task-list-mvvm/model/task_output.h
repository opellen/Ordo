#pragma once

#include <cstdint>
#include <string>

namespace app {

// Output port for the add-task use case. Domain-owned; the view layer
// implements it. Only caller-directed outcomes belong here -- shared state
// changes go out as broadcast facts (TaskAdded).
class TaskOutput {
public:
    virtual ~TaskOutput() = default;

    // The intent was rejected before any mutation -- meaningful only to
    // whoever asked (shown next to the input field, not on every view).
    virtual void taskRejected(const std::string& reason) = 0;

    // Echo of the accepted task, delivered to the caller specifically
    // (clear and refocus the input). The 1:N announcement still goes out
    // as events::TaskAdded.
    virtual void taskAccepted(std::uint64_t id, const std::string& title) = 0;
};

}  // namespace app
