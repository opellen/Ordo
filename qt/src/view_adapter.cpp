#include <ordo/qt/view_adapter.h>

Q_LOGGING_CATEGORY(ordoView, "ordo.view")

namespace ordo::qt {

ViewAdapter::ViewAdapter(const QString& name) : QObject(nullptr), name_(name) {
    qCDebug(ordoView) << name_ << "created";
}

ViewAdapter::~ViewAdapter() {
    if (context_ != nullptr) {
        context_->unsubscribe(this);
    }
}

const QString& ViewAdapter::name() const {
    return name_;
}

ordo::core::PresenterContext& ViewAdapter::context() const {
    Q_ASSERT_X(context_ != nullptr, "ViewAdapter::context",
               "context() called before registration");
    return *context_;
}

}  // namespace ordo::qt
