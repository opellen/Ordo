#pragma once

#include <cstdint>
#include <string>

namespace app::use_cases {

// Response model handed to the output port on acceptance, carrying
// everything needed to describe the outcome.
struct AddTaskResponse {
    std::uint64_t id = 0;
    std::string title;
};

}  // namespace app::use_cases
