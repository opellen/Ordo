#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QObject>
#include <QString>
#include <QThread>

#include "model/persistence_events.h"

namespace app {

// Forward-declared; defined in db_worker.cpp.
class DbExecutor;

// Marshals worker-thread DB results back to the UI thread via invokeMethod,
// queued, so no moc-declared signals are needed.
class DbRelay : public QObject {
public:
    explicit DbRelay(QObject* parent = nullptr) : QObject(parent) {}

    // Set once at bootstrap; both run on the UI thread.
    std::function<void(const events::TodosLoaded&)> onLoaded;
    std::function<void(const events::PersistCompleted&)> onPersistCompleted;

    // Callable from any thread; queues delivery onto the UI thread.
    void postLoaded(std::vector<events::Todo> todos, bool ok, std::string error);

    // `opSeq` is the id DbWorker stamped on the op when it queued it.
    void postPersistCompleted(std::uint64_t opSeq, bool ok, std::string error);
};

// Owns the worker thread and its one DbExecutor. Only code running on that
// thread may touch the QSqlDatabase connection; everything else (model,
// dispatcher, widgets) stays off-limits to it.
class DbWorker {
public:
    DbWorker(DbRelay& relay, const QString& dbPath, int slowIoMs);
    ~DbWorker();

    void load();
    void add(std::uint64_t id, const std::string& title);
    void setTitle(std::uint64_t id, const std::string& title);
    void setCompleted(std::uint64_t id, bool done);
    void remove(std::uint64_t id);
    void removeCompleted();
    void setAllCompleted(bool done);

private:
    std::unique_ptr<DbExecutor> executor_;
    QThread thread_;
    std::uint64_t opSeq_ = 0;
};

}  // namespace app
