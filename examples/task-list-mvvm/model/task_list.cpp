#include "model/task_list.h"

namespace app {

std::uint64_t TaskList::add(const std::string& title) {
    const std::uint64_t newId = nextId_++;
    tasks_.push_back(Task{newId, title});
    context().send(events::TaskAdded{.id = newId, .title = title});
    return newId;
}

}  // namespace app
