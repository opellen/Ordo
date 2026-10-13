# Code Patterns

The shapes [conventions.md](conventions.md) and [post-verify.md](post-verify.md)
refer to, in code. Every snippet is a trimmed excerpt of ordo's `todomvc`
example (namespace `app`, a SQLite-backed todo list with a worker thread)
unless its heading says *illustrative*. This skill does not depend on that
example being present: a vendored ordo tree often carries only `core/`, `qt/`
and the build files, so the excerpts here are the reference.

For exact signatures, read the headers in the vendored ordo source — they are
always there, because your build compiles against them:

| Header (from the ordo source root) | What it declares |
|:---|:---|
| `core/include/ordo/core/kernel.h` | `Kernel`: `registerAgent`, `registerCommand`, `removeCommand`, `send`, `agentAs` |
| `core/include/ordo/core/agent.h`, `agent_context.h` | `Agent`, and the `AgentContext` it publishes through (`send`, `agentAs`) |
| `core/include/ordo/core/command.h`, `command_context.h` | `Command<EventT>`, and the `CommandContext` passed to `execute` |
| `core/include/ordo/core/dispatcher.h`, `presenter_context.h` | the synchronous dispatcher and the subscribe-capable context adapters receive |
| `qt/include/ordo/qt/view_host.h` | `ViewHost`: `add<AdapterT>(...)`, `clear()` |
| `qt/include/ordo/qt/view_adapter.h` | `ViewAdapter`: `onRegister()`, `onRemove()`, `context()`, `subscribe<EventT>(...)` |
| `qt/include/ordo/qt/presenter.h`, `view_model.h` | the two adapter shapes |

---

## Events

One file per feature lane, intents and the facts they produce together.
`eventName` is optional — the dispatcher records it for tracing when present.

```cpp
// model/todo_events.h
namespace app::events {

// ---- Intents: requests to perform an action ----
struct AddTodoRequested {
    static constexpr std::string_view eventName = "AddTodoRequested";
    std::string title;
};

struct ToggleTodoRequested {
    static constexpr std::string_view eventName = "ToggleTodoRequested";
    std::uint64_t id = 0;
};

// ---- Fact: notification that state has changed ----
struct TodosFiltered {
    static constexpr std::string_view eventName = "TodosFiltered";
    std::vector<Todo> todos;
    int totalCount = 0;
    int activeCount = 0;
    // ...
};

}  // namespace app::events
```

A second lane (here, persistence) gets its own `persistence_events.h` with
`LoadTodosRequested`, `TodosLoaded`, `PersistCompleted`.

## Agent

Named for the thing it is, keyed by its own `kName`. Mutates, then publishes a
fact through `context()`. It never subscribes.

```cpp
// model/todo_list.h
class TodoListAgent : public ordo::core::Agent {
public:
    static constexpr const char* kName = "todos";

    TodoListAgent() : Agent(kName) {}

    std::uint64_t add(const std::string& title);
    const std::vector<events::Todo>& todos() const { return todos_; }

private:
    void publishSnapshot();   // every mutation ends here

    std::vector<events::Todo> todos_;
    std::uint64_t nextId_ = 1;
};

// model/todo_list.cpp
std::uint64_t TodoListAgent::add(const std::string& title) {
    const std::uint64_t newId = nextId_++;
    todos_.push_back(events::Todo{newId, title, false});
    publishSnapshot();
    return newId;
}

void TodoListAgent::publishSnapshot() {
    // ... compute counts and the filtered list ...
    context().send(events::TodosFiltered{
        .todos = std::move(filtered),
        .totalCount = static_cast<int>(todos_.size()),
        .activeCount = active,
        // ...
    });
}
```

## Command

One intent type, one command type, constructed fresh per dispatch. Policy
(validation, rejection) lives here. A rejected intent sends nothing. Look the
agent up through its `kName`, never a string literal — `agentAs` returns
`nullptr` for an unknown name and reports nothing.

```cpp
// controller/todo_commands.h
class AddTodoCommand : public ordo::core::Command<events::AddTodoRequested> {
public:
    explicit AddTodoCommand(DbWorker& db) : db_(db) {}

    void execute(const events::AddTodoRequested& event, ordo::core::CommandContext& context) override;

private:
    DbWorker& db_;
};

// A command with no dependency needs no constructor.
class SetFilterCommand : public ordo::core::Command<events::SetFilterRequested> {
public:
    void execute(const events::SetFilterRequested& event, ordo::core::CommandContext& context) override;
};

// controller/todo_commands.cpp
void AddTodoCommand::execute(const events::AddTodoRequested& event, ordo::core::CommandContext& context) {
    const std::string title = detail::trimmed(event.title);
    if (title.empty()) {
        return;   // policy: blank titles are rejected -- no mutation, no fact
    }

    auto todos = context.agentAs<TodoListAgent>(TodoListAgent::kName);
    if (!todos) {
        return;
    }
    const std::uint64_t id = todos->add(title);
    db_.add(id, title);
    todos->notePersistQueued();
}
```

## View adapter — ViewModel shape

Subscribe in `onRegister()` (the context is unset in the constructor); turn UI
input into intents with `context().send(...)`. No unsubscribe bookkeeping —
`~ViewAdapter` drops every subscription the adapter made.

```cpp
// view/todo_view_model.h
class TodoViewModel : public ordo::qt::ViewModel {
    Q_OBJECT

public:
    TodoViewModel() : ViewModel(QStringLiteral("TodoViewModel")) {}

    void onRegister() override;

public slots:
    void addTodo(const QString& title);

signals:
    void itemsChanged();

private:
    void onTodosFiltered(const events::TodosFiltered& e);
};

// view/todo_view_model.cpp
void TodoViewModel::onRegister() { subscribe<events::TodosFiltered>(&TodoViewModel::onTodosFiltered); }

void TodoViewModel::addTodo(const QString& title) { context().send(events::AddTodoRequested{title.toStdString()}); }
```

## View adapter — Presenter shape (illustrative)

Not from todomvc; `TaskListPresenter` and its widget are placeholder names.
A `Presenter` holds a typed pointer to the widget it writes into and does not
own it.

```cpp
class TaskListPresenter : public ordo::qt::Presenter {
    Q_OBJECT

public:
    explicit TaskListPresenter(QListWidget* list)
        : Presenter(QStringLiteral("TaskListPresenter"), list), list_(list) {}

    void onRegister() override { subscribe<events::TaskAdded>(&TaskListPresenter::onTaskAdded); }

private:
    void onTaskAdded(const events::TaskAdded& e) { list_->addItem(QString::fromStdString(e.title)); }

    QListWidget* list_;
};
```

## Wiring: register and remove as a pair

Keep registration in one function and its exact inverse in another, so the
smoke path and the real app share them and the one-for-one match is visible.
A reference dependency goes in through `std::ref`.

```cpp
// wiring.cpp
void registerCommands(ordo::core::Kernel& kernel, app::DbWorker& dbWorker) {
    kernel.registerCommand<app::events::AddTodoRequested, app::AddTodoCommand>(std::ref(dbWorker));
    kernel.registerCommand<app::events::SetFilterRequested, app::SetFilterCommand>();
    kernel.registerCommand<app::events::LoadTodosRequested, app::LoadTodosCommand>(std::ref(dbWorker));
    kernel.registerCommand<app::events::TodosLoaded, app::TodosLoadedCommand>();
    // ... one line per intent ...
}

void removeCommands(ordo::core::Kernel& kernel) {
    kernel.removeCommand<app::events::AddTodoRequested>();
    kernel.removeCommand<app::events::SetFilterRequested>();
    kernel.removeCommand<app::events::LoadTodosRequested>();
    kernel.removeCommand<app::events::TodosLoaded>();
    // ... exactly the same list ...
}
```

## Bootstrap and teardown (ViewModel shape)

Declaration order is reverse teardown order. Here the window *reads* the
view-model, so it is declared last and dies first; `dbWorker` is declared after
`kernel`/`dbRelay`, so its drain-then-quit destructor runs while they still
exist.

```cpp
// main.cpp, the non-smoke branch
ordo::core::Kernel kernel;
kernel.registerAgent(std::make_shared<app::TodoListAgent>());

ordo::qt::ViewHost host(kernel);
auto* vm = host.add<app::TodoViewModel>();

app::DbRelay dbRelay;
app::DbWorker dbWorker(dbRelay, dbPath, slowIoMs);

// Runs on the UI thread (invokeMethod's queued delivery), so calling kernel here is safe.
dbRelay.onLoaded = [&kernel](const app::events::TodosLoaded& e) { kernel.send(e); };
dbRelay.onPersistCompleted = [&kernel](const app::events::PersistCompleted& e) { kernel.send(e); };

registerCommands(kernel, dbWorker);

// Kicks off the pipeline -- the first event is just an event.
kernel.send(app::events::LoadTodosRequested{});

app::TodoWindow window(vm);
window.show();
exitCode = application.exec();

// Drop every command registration before kernel/view-model/dbWorker go out of scope.
removeCommands(kernel);
```

In the **Presenter shape** the order of the view side flips: the presenter
writes into widgets, so the widgets (the window) are declared *before* the
`ViewHost`, and the host — with its presenters — dies first.

## Thread relay

The only legal way back into the kernel from a worker thread. The worker calls
`post*`; the queued lambda runs on the UI thread and invokes the sink that
`main.cpp` wired to `kernel.send`.

```cpp
// infra/db_worker.h
class DbRelay : public QObject {
public:
    explicit DbRelay(QObject* parent = nullptr) : QObject(parent) {}

    // Set once at bootstrap; both run on the UI thread.
    std::function<void(const events::TodosLoaded&)> onLoaded;
    std::function<void(const events::PersistCompleted&)> onPersistCompleted;

    // Callable from any thread; queues delivery onto the UI thread.
    void postLoaded(std::vector<events::Todo> todos, bool ok, std::string error);
    void postPersistCompleted(bool ok, std::string error);
};

// infra/db_worker.cpp
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
```

## `--smoke` self-check

Same bootstrap, no window: drive the adapter's slots the way widgets would,
assert on observable state, print `FAIL:` and return 1 on mismatch. A session
struct keeps the declaration-order rule in its member order and calls
`removeCommands` in its destructor.

```cpp
// smoke.cpp
struct SmokeSession {
    ordo::core::Kernel kernel;
    ordo::qt::ViewHost host;
    app::DbRelay dbRelay;
    app::DbWorker dbWorker;
    app::TodoViewModel* vm = nullptr;

    SmokeSession(const QString& dbPath, int slowIoMs) : host(kernel), dbWorker(dbRelay, dbPath, slowIoMs) {
        kernel.registerAgent(std::make_shared<app::TodoListAgent>());
        vm = host.add<app::TodoViewModel>();
        dbRelay.onLoaded = [this](const app::events::TodosLoaded& e) { kernel.send(e); };
        dbRelay.onPersistCompleted = [this](const app::events::PersistCompleted& e) { kernel.send(e); };
        registerCommands(kernel, dbWorker);
        kernel.send(app::events::LoadTodosRequested{});
    }

    ~SmokeSession() { removeCommands(kernel); }
};

int runScenario(app::TodoViewModel& vm) {
    vm.addTodo(QStringLiteral("buy milk"));
    vm.addTodo(QStringLiteral("ship ordo"));
    if (vm.totalCount() != 2 || vm.activeCount() != 2) {
        std::fprintf(stderr, "FAIL: after two adds totalCount=%d activeCount=%d (want 2, 2)\n", vm.totalCount(),
                     vm.activeCount());
        return 1;
    }
    // ... every feature, then printf("PASS: ...") and return 0 ...
}
```

When a worker thread is involved, pump the event loop
(`QCoreApplication::processEvents()` in a bounded loop) until the relayed
reply lands before asserting on it.

## Unit test (illustrative)

`Kernel` is instantiable and shares nothing with other instances, so a test
builds its own, registers only what it needs, sends, and asserts. Dispatch is
synchronous: `send` returns after the command ran, the agent mutated and every
subscriber was called. `TaskList`, `AddTaskCommand` and `TaskOutput` are
placeholder names.

```cpp
struct RecordingOutput : app::TaskOutput {   // the command's output port
    void taskRejected(const std::string& reason) override { rejections.push_back(reason); }
    void taskAccepted(std::uint64_t, const std::string& title) override { accepted.push_back(title); }

    std::vector<std::string> rejections;
    std::vector<std::string> accepted;
};

TEST(TaskList, RejectsABlankTitle) {
    RecordingOutput output;   // declared first: the port must outlive the registration
    ordo::core::Kernel kernel;
    auto tasks = std::make_shared<app::TaskList>();
    kernel.registerAgent(tasks);
    kernel.registerCommand<app::events::AddTaskRequested, app::AddTaskCommand>(std::ref(output));

    kernel.send(app::events::AddTaskRequested{.title = "   "});

    EXPECT_TRUE(tasks->tasks().empty());      // no mutation, and no fact was sent
    EXPECT_EQ(output.rejections.size(), 1u);  // the caller alone hears why
}
```

Adapters need no application object in a test: `Presenter` and `ViewModel`
are plain `QObject`s. Put a `ViewHost` on the test kernel,
`host.add<...>()`, `kernel.send(...)`, and assert on the adapter's state. Use
`QApplication`/`QGuiApplication` only once real widgets or a QML engine are
under test.
