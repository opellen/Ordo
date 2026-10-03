#pragma once

#include <functional>
#include <typeinfo>
#include <utility>

#include <QLoggingCategory>
#include <QObject>
#include <QString>

#include <ordo/core/presenter_context.h>

Q_DECLARE_LOGGING_CATEGORY(ordoView)

namespace ordo::qt {

class ViewHost;

// Passkey minted only by ViewHost: gates context injection the same way
// KernelKey gates kernel-only wiring on the core side.
class HostKey {
    friend class ViewHost;
    HostKey() = default;

public:
    HostKey(const HostKey&) = delete;
    HostKey& operator=(const HostKey&) = delete;
};

// Base of Presenter and ViewModel. context() is valid from onRegister() on,
// never in the constructor.
class ViewAdapter : public QObject {
    Q_OBJECT

public:
    // Unsubscribes everything this adapter subscribed, regardless of event
    // type, so a destroyed adapter can never leave a dangling handler behind.
    ~ViewAdapter() override;

    const QString& name() const;

    // Lifecycle hooks, called by the host after construction / before teardown.
    virtual void onRegister() {}
    virtual void onRemove() {}

    // Injection seam for ViewHost (HostKey passkey).
    void setContext(HostKey, ordo::core::PresenterContext* context) noexcept { context_ = context; }

protected:
    // name is an identity used for logging/debugging only.
    explicit ViewAdapter(const QString& name);

    // Precondition: registered (context injected). Kernel-owned, never dangles.
    ordo::core::PresenterContext& context() const;

    // Subscribes a member function of the most-derived class:
    //   subscribe<events::TaskAdded>(&TaskListViewModel::onTaskAdded);
    template <typename EventT, typename SelfT>
    void subscribe(void (SelfT::*memberFn)(const EventT&)) {
        auto* self = static_cast<SelfT*>(this);
        subscribe<EventT>(
            std::function<void(const EventT&)>([self, memberFn](const EventT& event) { (self->*memberFn)(event); }));
    }

    // Subscribes an arbitrary callable. Owner cookie is always `this`, so the
    // destructor cleans it up.
    template <typename EventT>
    void subscribe(std::function<void(const EventT&)> handler) {
        qCDebug(ordoView) << name_ << "subscribing to" << typeid(EventT).name();
        context().subscribe<EventT>(this, std::move(handler));
    }

private:
    ordo::core::PresenterContext* context_ = nullptr;
    QString name_;
};

}  // namespace ordo::qt
