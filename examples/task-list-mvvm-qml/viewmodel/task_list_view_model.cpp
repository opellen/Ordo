#include "viewmodel/task_list_view_model.h"

#include <QVariant>

namespace app {

TaskListViewModel::TaskListViewModel() : ViewModel(QStringLiteral("TaskListViewModel")), items_(this) {
    items_.setItemRoleNames({{TitleRole, "title"}, {DoneRole, "done"}, {IdRole, "taskId"}});
}

void TaskListViewModel::onRegister() {
    subscribe<events::TaskAdded>(&TaskListViewModel::onTaskAdded);
    subscribe<events::TaskToggled>(&TaskListViewModel::onTaskToggled);
    subscribe<events::TaskRemoved>(&TaskListViewModel::onTaskRemoved);
}

void TaskListViewModel::taskRejected(const std::string& reason) {
    lastError_ = QString::fromStdString(reason);
    emit lastErrorChanged(lastError_);
}
void TaskListViewModel::taskAccepted(std::uint64_t, const std::string&) {
    if (!lastError_.isEmpty()) {
        lastError_.clear();
        emit lastErrorChanged(lastError_);
    }
    emit inputAccepted();   // the view clears/refocuses its input field
}

void TaskListViewModel::addTask(const QString& title) { context().send(events::AddTaskRequested{title.toStdString()}); }

void TaskListViewModel::toggleTask(qulonglong id) {
    context().send(events::ToggleTaskRequested{static_cast<std::uint64_t>(id)});
}

void TaskListViewModel::removeTask(qulonglong id) {
    context().send(events::RemoveTaskRequested{static_cast<std::uint64_t>(id)});
}

void TaskListViewModel::onTaskAdded(const events::TaskAdded& e) {
    auto* item = new QStandardItem();
    item->setEditable(false);
    item->setData(QString::fromStdString(e.title), TitleRole);
    item->setData(false, DoneRole);
    item->setData(QVariant::fromValue<qulonglong>(e.id), IdRole);
    items_.appendRow(item);
}

void TaskListViewModel::onTaskToggled(const events::TaskToggled& e) {
    const int row = rowOfTask(e.id);
    if (row < 0) return;
    items_.item(row)->setData(e.done, DoneRole);
}

void TaskListViewModel::onTaskRemoved(const events::TaskRemoved& e) {
    const int row = rowOfTask(e.id);
    if (row < 0) return;
    items_.removeRow(row);
}

int TaskListViewModel::rowOfTask(qulonglong id) const {
    for (int row = 0; row < items_.rowCount(); ++row) {
        if (items_.item(row)->data(IdRole).toULongLong() == id) return row;
    }
    return -1;
}

}  // namespace app
