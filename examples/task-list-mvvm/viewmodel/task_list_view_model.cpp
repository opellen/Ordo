#include "viewmodel/task_list_view_model.h"

namespace app {

void TaskListViewModel::onRegister() { subscribe<events::TaskAdded>(&TaskListViewModel::onTaskAdded); }

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

void TaskListViewModel::onTaskAdded(const events::TaskAdded& e) {
    ++count_;
    emit taskAdded(QString::fromStdString(e.title));
    emit countChanged(count_);
}

}  // namespace app
