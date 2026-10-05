#pragma once

#include <QObject>
#include <QString>

#include <ordo/qt/presenter.h>

#include "entities/task_added.h"
#include "use_cases/add_task_response.h"
#include "use_cases/task_output.h"

class QLabel;
class QLineEdit;
class QListWidget;

namespace app::interface_adapters {

// Imperative presenter: holds widget pointers and writes them directly,
// rather than exposing observable properties. Also implements the output
// port, so caller-directed results reach the screen the same way as facts.
class TaskListPresenter : public ordo::qt::Presenter, public app::use_cases::TaskOutput {
public:
    TaskListPresenter(QLineEdit* input, QLabel* errorLabel, QListWidget* list, QLabel* status,
                       QObject* root = nullptr);

    void onRegister() override;

    // ---- TaskOutput (caller-directed channel) ------------------------------
    void presentRejected(const std::string& reason) override;
    void presentAdded(const use_cases::AddTaskResponse&) override;

    // The Controller here is just this send() call; main.cpp's signal
    // handlers call it on user action, turning input into the intent event.
    void addTask(const QString& title);

private:
    // ---- broadcast channel (1:N) -------------------------------------------
    void onTaskAdded(const entities::TaskAdded& e);

    QLineEdit* input_;
    QLabel* errorLabel_;
    QListWidget* list_;
    QLabel* status_;
    int count_ = 0;
};

}  // namespace app::interface_adapters
