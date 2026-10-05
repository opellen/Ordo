#include "interface_adapters/task_list_presenter.h"

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>

#include "use_cases/add_task_request.h"

namespace app::interface_adapters {

TaskListPresenter::TaskListPresenter(QLineEdit* input, QLabel* errorLabel, QListWidget* list, QLabel* status,
                                      QObject* root)
    : Presenter(QStringLiteral("TaskListPresenter"), root),
      input_(input),
      errorLabel_(errorLabel),
      list_(list),
      status_(status) {}

void TaskListPresenter::onRegister() { subscribe<entities::TaskAdded>(&TaskListPresenter::onTaskAdded); }

void TaskListPresenter::presentRejected(const std::string& reason) {
    errorLabel_->setText(QString::fromStdString(reason));
}
void TaskListPresenter::presentAdded(const use_cases::AddTaskResponse&) {
    errorLabel_->clear();
    input_->clear();
    input_->setFocus();
}

void TaskListPresenter::addTask(const QString& title) {
    context().send(use_cases::AddTaskRequest{title.toStdString()});
}

void TaskListPresenter::onTaskAdded(const entities::TaskAdded& e) {
    list_->addItem(QString::fromStdString(e.title));
    ++count_;
    status_->setText(QStringLiteral("%1 tasks").arg(count_));
}

}  // namespace app::interface_adapters
