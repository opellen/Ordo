#include "model/task_list.h"

#include <algorithm>

namespace app {

std::uint64_t TaskList::add(const std::string& title) {
    const std::uint64_t newId = nextId_++;
    tasks_.push_back(Task{newId, title});
    context().send(events::TaskAdded{.id = newId, .title = title});
    return newId;
}

std::optional<bool> TaskList::toggle(std::uint64_t id) {
    auto it = std::find_if(tasks_.begin(), tasks_.end(), [id](const Task& t) { return t.id == id; });
    if (it == tasks_.end()) {
        return std::nullopt;
    }
    it->done = !it->done;
    context().send(events::TaskToggled{.id = id, .done = it->done});
    return it->done;
}

bool TaskList::remove(std::uint64_t id) {
    auto it = std::find_if(tasks_.begin(), tasks_.end(), [id](const Task& t) { return t.id == id; });
    if (it == tasks_.end()) {
        return false;
    }
    tasks_.erase(it);
    context().send(events::TaskRemoved{.id = id});
    return true;
}

}  // namespace app
