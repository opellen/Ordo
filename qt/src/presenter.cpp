#include <ordo/qt/presenter.h>

namespace ordo::qt {

Presenter::Presenter(const QString& name, QObject* viewComponent)
    : ViewAdapter(name), viewComponent_(viewComponent) {}

QObject* Presenter::viewComponent() const {
    return viewComponent_;
}

void Presenter::setViewComponent(QObject* view) {
    viewComponent_ = view;
}

}  // namespace ordo::qt
