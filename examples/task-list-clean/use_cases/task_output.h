#pragma once

#include <string>

#include "use_cases/add_task_response.h"

namespace app::use_cases {

// Output port for the add-task use case. Lives in the use-case module; the
// adapter layer implements it without the use case knowing who. Only
// caller-directed outcomes belong here -- broadcast facts (TaskAdded) go
// elsewhere.
class TaskOutput {
public:
    virtual ~TaskOutput() = default;

    // Rejected before any mutation; meaningful only to the caller (shown
    // next to the input field).
    virtual void presentRejected(const std::string& reason) = 0;

    // Echo of the accepted task to the caller (clear + refocus input); the
    // 1:N announcement still goes out separately as TaskAdded.
    virtual void presentAdded(const AddTaskResponse& response) = 0;
};

}  // namespace app::use_cases
