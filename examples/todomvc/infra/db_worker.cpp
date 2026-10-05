#include "infra/db_worker.h"

#include <atomic>
#include <utility>

#include <QMetaObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace app {

void DbRelay::postLoaded(std::vector<events::Todo> todos, bool ok, std::string error) {
    QMetaObject::invokeMethod(
        this,
        [this, todos = std::move(todos), ok, error = std::move(error)]() mutable {
            if (onLoaded) {
                onLoaded(events::TodosLoaded{std::move(todos), ok, std::move(error)});
            }
        },
        Qt::QueuedConnection);
}

void DbRelay::postPersistCompleted(bool ok, std::string error) {
    QMetaObject::invokeMethod(
        this,
        [this, ok, error = std::move(error)]() mutable {
            if (onPersistCompleted) {
                onPersistCompleted(events::PersistCompleted{ok, std::move(error)});
            }
        },
        Qt::QueuedConnection);
}

// Moved onto its own QThread in DbWorker's constructor; every method below
// runs there only, via queued invokeMethod calls.
class DbExecutor : public QObject {
public:
    DbExecutor(DbRelay& relay, QString dbPath, int slowIoMs)
        : relay_(&relay),
          dbPath_(std::move(dbPath)),
          slowIoMs_(slowIoMs),
          connName_(QStringLiteral("todomvc_db_%1").arg(nextConnId_.fetch_add(1))) {}

    void open() {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName_);
        db.setDatabaseName(dbPath_);
        if (!db.open()) {
            failed_ = true;
            failedError_ = db.lastError().text().toStdString();
            return;
        }

        QSqlQuery createTable(db);
        if (!createTable.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS todos ("
                "id INTEGER PRIMARY KEY, title TEXT NOT NULL, completed INTEGER NOT NULL)"))) {
            failed_ = true;
            failedError_ = createTable.lastError().text().toStdString();
        }
    }

    void load() {
        sleepIfSlow();

        if (failed_) {
            relay_->postLoaded({}, false, failedError_);
            return;
        }

        QSqlQuery query(database());
        if (!query.exec(QStringLiteral("SELECT id, title, completed FROM todos ORDER BY id"))) {
            relay_->postLoaded({}, false, query.lastError().text().toStdString());
            return;
        }

        // ORDER BY id keeps result order == insertion order, as the rest of the app expects.
        std::vector<events::Todo> todos;
        while (query.next()) {
            events::Todo todo;
            todo.id = query.value(0).toULongLong();
            todo.title = query.value(1).toString().toStdString();
            todo.completed = query.value(2).toInt() != 0;
            todos.push_back(std::move(todo));
        }
        relay_->postLoaded(std::move(todos), true, "");
    }

    void add(std::uint64_t id, const std::string& title) {
        sleepIfSlow();
        if (shortCircuitIfFailed()) {
            return;
        }

        QSqlQuery query(database());
        query.prepare(QStringLiteral("INSERT INTO todos (id, title, completed) VALUES (?, ?, 0)"));
        query.addBindValue(static_cast<qulonglong>(id));
        query.addBindValue(QString::fromStdString(title));
        reportPersist(query);
    }

    void setTitle(std::uint64_t id, const std::string& title) {
        sleepIfSlow();
        if (shortCircuitIfFailed()) {
            return;
        }

        QSqlQuery query(database());
        query.prepare(QStringLiteral("UPDATE todos SET title = ? WHERE id = ?"));
        query.addBindValue(QString::fromStdString(title));
        query.addBindValue(static_cast<qulonglong>(id));
        reportPersist(query);
    }

    void setCompleted(std::uint64_t id, bool done) {
        sleepIfSlow();
        if (shortCircuitIfFailed()) {
            return;
        }

        QSqlQuery query(database());
        query.prepare(QStringLiteral("UPDATE todos SET completed = ? WHERE id = ?"));
        query.addBindValue(done ? 1 : 0);
        query.addBindValue(static_cast<qulonglong>(id));
        reportPersist(query);
    }

    void remove(std::uint64_t id) {
        sleepIfSlow();
        if (shortCircuitIfFailed()) {
            return;
        }

        QSqlQuery query(database());
        query.prepare(QStringLiteral("DELETE FROM todos WHERE id = ?"));
        query.addBindValue(static_cast<qulonglong>(id));
        reportPersist(query);
    }

    void removeCompleted() {
        sleepIfSlow();
        if (shortCircuitIfFailed()) {
            return;
        }

        QSqlQuery query(database());
        query.prepare(QStringLiteral("DELETE FROM todos WHERE completed = 1"));
        reportPersist(query);
    }

    void setAllCompleted(bool done) {
        sleepIfSlow();
        if (shortCircuitIfFailed()) {
            return;
        }

        QSqlQuery query(database());
        query.prepare(QStringLiteral("UPDATE todos SET completed = ?"));
        query.addBindValue(done ? 1 : 0);
        reportPersist(query);
    }

    void close() {
        {
            QSqlDatabase db = database();
            if (db.isOpen()) {
                db.close();
            }
        }
        // Connection-name registry is process-global; must remove or a later open collides.
        QSqlDatabase::removeDatabase(connName_);
    }

private:
    QSqlDatabase database() const { return QSqlDatabase::database(connName_); }

    void sleepIfSlow() {
        if (slowIoMs_ > 0) {
            // Sleeps the worker thread only; the UI keeps running.
            QThread::msleep(static_cast<unsigned long>(slowIoMs_));
        }
    }

    // If open() already failed, reports the remembered error instead of querying a dead connection.
    bool shortCircuitIfFailed() {
        if (failed_) {
            relay_->postPersistCompleted(false, failedError_);
        }
        return failed_;
    }

    void reportPersist(QSqlQuery& query) {
        if (query.exec()) {
            relay_->postPersistCompleted(true, "");
        } else {
            relay_->postPersistCompleted(false, query.lastError().text().toStdString());
        }
    }

    DbRelay* relay_;
    QString dbPath_;
    int slowIoMs_;
    QString connName_;
    bool failed_ = false;
    std::string failedError_;

    // Process-wide: connection names must be unique across the whole process, not just per instance.
    static inline std::atomic<int> nextConnId_{0};
};

DbWorker::DbWorker(DbRelay& relay, const QString& dbPath, int slowIoMs)
    : executor_(std::make_unique<DbExecutor>(relay, dbPath, slowIoMs)) {
    executor_->moveToThread(&thread_);
    thread_.start();

    // Queued first, so the FIFO order guarantees open() runs before any later op.
    DbExecutor* ex = executor_.get();
    QMetaObject::invokeMethod(ex, [ex] { ex->open(); }, Qt::QueuedConnection);
}

// Out-of-line: unique_ptr's destructor needs DbExecutor complete, which it
// only is in this translation unit.
DbWorker::~DbWorker() {
    DbExecutor* ex = executor_.get();

    // Queued through the same FIFO path as every op, so close+quit runs last, after all writes drain.
    QMetaObject::invokeMethod(
        ex,
        [ex] {
            ex->close();
            QThread::currentThread()->quit();
        },
        Qt::QueuedConnection);
    thread_.wait();

    // Thread is dead once wait() returns, so it's safe to delete the executor here.
    executor_.reset();
}

void DbWorker::load() {
    DbExecutor* ex = executor_.get();
    QMetaObject::invokeMethod(ex, [ex] { ex->load(); }, Qt::QueuedConnection);
}

void DbWorker::add(std::uint64_t id, const std::string& title) {
    DbExecutor* ex = executor_.get();
    QMetaObject::invokeMethod(
        ex, [ex, id, title] { ex->add(id, title); }, Qt::QueuedConnection);
}

void DbWorker::setTitle(std::uint64_t id, const std::string& title) {
    DbExecutor* ex = executor_.get();
    QMetaObject::invokeMethod(
        ex, [ex, id, title] { ex->setTitle(id, title); }, Qt::QueuedConnection);
}

void DbWorker::setCompleted(std::uint64_t id, bool done) {
    DbExecutor* ex = executor_.get();
    QMetaObject::invokeMethod(
        ex, [ex, id, done] { ex->setCompleted(id, done); }, Qt::QueuedConnection);
}

void DbWorker::remove(std::uint64_t id) {
    DbExecutor* ex = executor_.get();
    QMetaObject::invokeMethod(
        ex, [ex, id] { ex->remove(id); }, Qt::QueuedConnection);
}

void DbWorker::removeCompleted() {
    DbExecutor* ex = executor_.get();
    QMetaObject::invokeMethod(ex, [ex] { ex->removeCompleted(); }, Qt::QueuedConnection);
}

void DbWorker::setAllCompleted(bool done) {
    DbExecutor* ex = executor_.get();
    QMetaObject::invokeMethod(
        ex, [ex, done] { ex->setAllCompleted(done); }, Qt::QueuedConnection);
}

}  // namespace app
