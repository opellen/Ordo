#pragma once

#include <cstdint>

#include <QList>
#include <QString>

#include <ordo/qt/view_model.h>

#include "model/todo_events.h"

namespace app {

// View-shaped state for the todo list: knows no widget, exposes properties
// and signals a view binds to, and slots that turn UI input into intents.
class TodoViewModel : public ordo::qt::ViewModel {
    Q_OBJECT

public:
    // Qt-friendly mirror of events::Todo (quint64/QString instead of
    // std::uint64_t/std::string) for the view to bind against.
    struct TodoItem {
        quint64 id = 0;
        QString title;
        bool completed = false;
    };

    TodoViewModel() : ViewModel(QStringLiteral("TodoViewModel")) {}

    void onRegister() override;

    const QList<TodoItem>& items() const { return items_; }
    int activeCount() const { return active_; }
    int completedCount() const { return completed_; }
    int totalCount() const { return total_; }
    events::Filter filter() const { return filter_; }

    // True once every todo is complete (and there is at least one) -- drives
    // the toggle-all checkbox's checked state.
    bool allComplete() const { return total_ > 0 && active_ == 0; }
    // True when there is at least one completed todo -- drives whether
    // clear-completed is enabled.
    bool hasCompleted() const { return completed_ > 0; }

    // False until the first TodosLoaded lands -- drives the loading
    // overlay.
    bool loaded() const { return loaded_; }
    // How many DB writes are queued but not yet confirmed -- drives the
    // "saving..." indicator.
    int pendingWrites() const { return pendingWrites_; }
    // The most recent failed write's message, or empty when the last write
    // (if any) succeeded.
    QString persistError() const { return persistError_; }

public slots:
    void addTodo(const QString& title);
    void toggleTodo(quint64 id);
    void destroyTodo(quint64 id);
    void editTodo(quint64 id, const QString& title);
    void clearCompleted();
    void toggleAll(bool completed);
    void setFilter(app::events::Filter filter);

signals:
    void itemsChanged();
    void countsChanged(int active, int completed, int total);
    void filterChanged(app::events::Filter filter);
    void loadedChanged(bool loaded);
    void pendingWritesChanged(int pendingWrites);
    void persistErrorChanged(const QString& error);

private:
    // One fact carries the filtered list and the stats together, so one
    // handler updates all of it and emits the three signals views bind to.
    void onTodosFiltered(const events::TodosFiltered& e);

    QList<TodoItem> items_;
    int total_ = 0;
    int active_ = 0;
    int completed_ = 0;
    events::Filter filter_ = events::Filter::All;
    bool loaded_ = false;
    int pendingWrites_ = 0;
    QString persistError_;
};

}  // namespace app
