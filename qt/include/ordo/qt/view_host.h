#pragma once

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <ordo/core/kernel.h>
#include <ordo/qt/view_adapter.h>

namespace ordo::qt {

// Owns view adapters (Presenters, ViewModels) and drives their
// register/remove lifecycle; removal is LIFO. add() injects the kernel's
// PresenterContext before onRegister().
class ViewHost {
public:
    explicit ViewHost(ordo::core::Kernel& kernel) : context_(kernel.presenterContext()) {}
    ~ViewHost();

    ViewHost(const ViewHost&) = delete;
    ViewHost& operator=(const ViewHost&) = delete;

    // Constructs AdapterT in place, takes ownership, injects the context,
    // calls onRegister(). Returns a non-owning pointer valid until
    // clear()/destruction.
    template <typename AdapterT, typename... Args>
    AdapterT* add(Args&&... args) {
        static_assert(std::is_base_of_v<ViewAdapter, AdapterT>,
                      "AdapterT must derive from ViewAdapter (Presenter or ViewModel)");
        auto adapter = std::make_unique<AdapterT>(std::forward<Args>(args)...);
        AdapterT* raw = adapter.get();
        adapters_.push_back(std::move(adapter));
        raw->setContext(HostKey{}, &context_);
        raw->onRegister();
        return raw;
    }

    // LIFO: onRemove() then destroy, newest first.
    void clear();

private:
    ordo::core::PresenterContext& context_;
    std::vector<std::unique_ptr<ViewAdapter>> adapters_;
};

}  // namespace ordo::qt
