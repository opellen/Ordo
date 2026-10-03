#pragma once

#include <ordo/qt/view_adapter.h>

namespace ordo::qt {

// State the view binds to (properties) plus intents it sends (slots).
// Never holds a reference to a view. Empty on purpose: it is also a runtime
// role tag (qobject_cast<ViewModel*>).
class ViewModel : public ViewAdapter {
    Q_OBJECT

protected:
    explicit ViewModel(const QString& name) : ViewAdapter(name) {}
};

}  // namespace ordo::qt
