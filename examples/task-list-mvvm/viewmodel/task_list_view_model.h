#pragma once

#include <QString>

#include <ordo/qt/view_model.h>

#include "model/task_events.h"
#include "model/task_output.h"

namespace app {

// State the view binds to (properties/signals) plus a slot for its intent.
// Also the output-port implementation, so caller-directed results become
// observable state too; TaskAdded stays a subscription since it's 1:N.
class TaskListViewModel : public ordo::qt::ViewModel, public TaskOutput {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    TaskListViewModel() : ViewModel(QStringLiteral("TaskListViewModel")) {}

    void onRegister() override;

    int count() const { return count_; }
    QString lastError() const { return lastError_; }

    // ---- TaskOutput (caller-directed channel) ------------------------------
    void taskRejected(const std::string& reason) override;
    void taskAccepted(std::uint64_t, const std::string&) override;

public slots:
    void addTask(const QString& title);

signals:
    void countChanged(int count);
    void taskAdded(const QString& title);
    void lastErrorChanged(const QString& error);
    void inputAccepted();

private:
    // ---- broadcast channel (1:N) -------------------------------------------
    void onTaskAdded(const events::TaskAdded& e);

    int count_ = 0;
    QString lastError_;
};

}  // namespace app
