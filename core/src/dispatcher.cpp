#include <ordo/core/dispatcher.h>

#include <algorithm>

namespace ordo::core {

void Dispatcher::unsubscribe(const void* owner) {
    for (auto& [hash, entries] : handlers_) {
        (void)hash;
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                      [owner](const Entry& entry) { return entry.owner == owner; }),
                      entries.end());
    }
}

void Dispatcher::setObserver(std::function<void(const DispatchRecord&)> observer) {
    observer_ = std::move(observer);
}

}  // namespace ordo::core
