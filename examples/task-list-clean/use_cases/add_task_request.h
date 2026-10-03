#pragma once

#include <string>
#include <string_view>

namespace app::use_cases {

// The request model and the intent event are the same struct: submitting IS
// sending it via context().send(). registerCommand binds a dispatched
// AddTaskRequest to AddTaskCommand::execute(), so no separate input-port
// interface is needed.
struct AddTaskRequest {
    static constexpr std::string_view eventName = "AddTaskRequest";
    std::string title;
};

}  // namespace app::use_cases
