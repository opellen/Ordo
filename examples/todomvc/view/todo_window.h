#pragma once

#include <QWidget>

namespace app {

class TodoViewModel;

// The view: plain widgets bound to the view-model. No Q_OBJECT -- every
// connection is a lambda or a pointer straight to the view-model's signal.
class TodoWindow : public QWidget {
public:
    explicit TodoWindow(TodoViewModel* vm, QWidget* parent = nullptr);
};

}  // namespace app
