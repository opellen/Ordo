#pragma once

#include <QWidget>

namespace app {

class TaskListViewModel;

// Plain widgets bound to the view-model; updates flow through the domain
// loop only. No Q_OBJECT -- it declares no signals/slots of its own, so moc
// has nothing to generate.
class TaskListWindow : public QWidget {
public:
    explicit TaskListWindow(TaskListViewModel* viewModel, QWidget* parent = nullptr);
};

}  // namespace app
