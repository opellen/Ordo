#pragma once

#include <QAbstractItemModel>
#include <QStandardItemModel>
#include <QString>

#include <ordo/qt/view_model.h>

#include "model/task_events.h"
#include "model/task_output.h"

namespace app {

// View-shaped state for the task list: a QStandardItemModel projection of
// TaskList, written only by broadcast facts. Invokables are the only path
// for intent -- QML never edits rows directly. Also implements TaskOutput,
// so caller-directed results land as state the same way.
class TaskListViewModel : public ordo::qt::ViewModel, public TaskOutput {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* items READ items CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    enum Roles { TitleRole = Qt::UserRole + 1, DoneRole, IdRole };

    TaskListViewModel();

    void onRegister() override;

    QAbstractItemModel* items() { return &items_; }
    QString lastError() const { return lastError_; }

    // ---- TaskOutput (caller-directed channel) ------------------------------
    void taskRejected(const std::string& reason) override;
    void taskAccepted(std::uint64_t, const std::string&) override;

    // ---- intents (the only path from QML into the kernel) ------------------
    Q_INVOKABLE void addTask(const QString& title);
    Q_INVOKABLE void toggleTask(qulonglong id);
    Q_INVOKABLE void removeTask(qulonglong id);

signals:
    void lastErrorChanged(const QString& error);
    void inputAccepted();

private:
    // ---- broadcast channel (1:N) -- the only writers of items_ -------------
    void onTaskAdded(const events::TaskAdded& e);
    void onTaskToggled(const events::TaskToggled& e);
    void onTaskRemoved(const events::TaskRemoved& e);

    int rowOfTask(qulonglong id) const;

    QStandardItemModel items_;
    QString lastError_;
};

}  // namespace app
