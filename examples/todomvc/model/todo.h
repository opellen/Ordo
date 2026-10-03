#pragma once

#include <cstdint>
#include <string>

namespace app::events {

// Shared vocabulary for the TodoMVC feature and persistence event headers.

// Which subset of the list a view wants to see.
enum class Filter { All, Active, Completed };

// One todo item.
struct Todo {
    std::uint64_t id = 0;
    std::string title;
    bool completed = false;
};

}  // namespace app::events
