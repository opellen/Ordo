#pragma once

#include <ordo/qt/view_adapter.h>

namespace ordo::qt {

// State the view binds to (properties) plus the events it sends (slots).
// Holds no reference to a view. Empty marker base: qobject_cast<ViewModel*>
// identifies the role at runtime.
class ViewModel : public ViewAdapter {
    Q_OBJECT

protected:
    explicit ViewModel(const QString& name) : ViewAdapter(name) {}
};

}  // namespace ordo::qt
