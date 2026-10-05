#include <ordo/qt/view_host.h>

namespace ordo::qt {

ViewHost::~ViewHost() {
    clear();
}

void ViewHost::clear() {
    // onRemove() and destruction both run newest-first, like local variables.
    for (auto it = adapters_.rbegin(); it != adapters_.rend(); ++it) {
        (*it)->onRemove();
    }
    while (!adapters_.empty()) {
        adapters_.pop_back();
    }
}

}  // namespace ordo::qt
