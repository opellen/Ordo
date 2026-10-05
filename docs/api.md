# API Reference

Reference for the public types of `ordo::core` and `ordo::qt`. Signatures are
reproduced from the headers; each entry states the rules a signature cannot
show. For the narrative walkthrough — how the roles fit together in a running
feature — see the [usage guide](usage-guide.md).

---

## Requirements

| | |
|:---|:---|
| Language | C++20 (`cxx_std_20` is a `PUBLIC` compile feature of `ordo::core`) |
| `ordo::core` | No Qt dependency. Standard library only — usable from console tools, tests, and non-Qt hosts |
| `ordo::qt` | Qt 6.5 or newer (the QML example's `loadFromModule` sets the floor; developed against Qt 6.11). The headers use only Qt Core types (`QObject`, `QString`, `QLoggingCategory`); the built target links `Qt6::Core` and `Qt6::Widgets` publicly, and consumers add what their own view layer needs (Widgets, Quick) |

Include paths mirror the namespaces: `<ordo/core/kernel.h>`,
`<ordo/qt/view_host.h>`. CMake targets are `ordo::core` and `ordo::qt`
(`ORDO_BUILD_QT=OFF` builds core alone).

---

## Roles at a glance

| Role | Header | One line |
|:---|:---|:---|
| **Event** | *(yours)* | A plain struct: the type is the identity, the members are the payload — no base class, no registration |
| **Agent** | `core/agent.h` | Named owner of domain state; mutates, then publishes facts. Send-only, never subscribes |
| **Command** | `core/command.h` | Stateless transaction constructed fresh per dispatch; business policy lives here |
| **Kernel** | `core/kernel.h` | Facade over one dispatcher, the named agents, and the per-event command factories. Instantiable, never a singleton |
| **Dispatcher** | `core/dispatcher.h` | Synchronous typed-struct event bus: 1:N broadcast in subscribe order |
| **AgentContext** | `core/agent_context.h` | Capability view for agents: `send` + agent lookup |
| **CommandContext** | `core/command_context.h` | Capability view for commands: `send` + agent lookup |
| **PresenterContext** | `core/presenter_context.h` | Capability view for view adapters: `subscribe`/`unsubscribe` + `send` + agent lookup |
| **ViewHost** | `qt/view_host.h` | Owns view adapters, injects their context, drives register/remove (LIFO) |
| **ViewAdapter** | `qt/view_adapter.h` | Base of the view-side roles: capability plumbing, lifecycle hooks, auto-unsubscribe |
| **Presenter** | `qt/presenter.h` | View mediator: reacts to facts by writing a view component imperatively |
| **ViewModel** | `qt/view_model.h` | View-shaped state holder that binds to no view — a role marker, deliberately member-less |

---

## ordo::core

### Kernel

Facade over one `Dispatcher` plus named `Agent`s and per-event `Command`
factories. An ordinary object, never a singleton — two instances are fully
isolated, which is what makes per-test kernels cheap. In application code it
should appear only in bootstrap; every other role reaches it through a
narrower context.

```cpp
Kernel();
Dispatcher& dispatcher();
void registerAgent(std::shared_ptr<Agent> agent);
std::shared_ptr<Agent> agent(std::string_view name);
template <typename AgentT> std::shared_ptr<AgentT> agentAs(std::string_view name);
bool removeAgent(std::string_view name);
template <typename EventT, typename CommandT, typename... Args> void registerCommand(Args&&... args);
template <typename EventT> void removeCommand();
template <typename EventT> void send(const EventT& event);
CommandContext& commandContext();
PresenterContext& presenterContext();
```

- **`registerAgent` registers by name, and replaces.** The agent's own
  `name()` is the key; an incumbent under that name gets `onRemove()` and has
  its context cleared before the newcomer is stored, handed the kernel's
  `AgentContext`, and given `onRegister()`.
- **`registerCommand` replaces too.** Re-registering for the same `EventT`
  drops the previous factory first. Each dispatched `EventT` constructs a
  fresh `CommandT` and calls `execute(event, commandContext())`. One intent
  has exactly one command — the replace semantics above are the contract,
  not an accident. When one intent needs two policy lanes, either a single
  command orchestrates both in order (todomvc's mutating commands mutate the
  agent, then enqueue the database op) or a follow-up command reacts to the
  fact the first one caused. Registering twice is never the mechanism.
- **Extra arguments are captured once, at registration**, and copied into
  every construction. Pass reference dependencies as `std::ref` — and keep
  the referent alive until the registration is removed. Subscriptions
  auto-clean by owner cookie; these captures have no such net.
- **`removeCommand<EventT>()` is surgical**: it tears down only that event
  type's factory (see [KernelKey and HostKey](#kernelkey-and-hostkey)).
- **`agentAs<AgentT>` returns nullptr twice over** — nothing registered under
  the name, or the registered agent is not an `AgentT`. No diagnostic
  distinguishes the two.
- **`commandContext()`/`presenterContext()` grant nothing new** — both are
  strictly narrower than the kernel itself. `ViewHost` injects
  `presenterContext()`; `commandContext()` serves callers running
  command-layer helpers outside a dispatch (tests, transaction tooling).

```cpp
ordo::core::Kernel kernel;
kernel.registerAgent(std::make_shared<app::TaskList>());   // registers under "tasks"

// std::ref keeps the output port a reference through the factory's capture;
// the referent must outlive the registration.
kernel.registerCommand<app::events::AddTaskRequested, app::AddTaskCommand>(std::ref(*viewModel));
kernel.registerCommand<app::events::ToggleTaskRequested, app::ToggleTaskCommand>();

kernel.send(app::events::AddTaskRequested{"ship ordo"});

kernel.removeCommand<app::events::AddTaskRequested>();
```

### Typed events

An event is a concept, not a class: any plain struct works. The **struct type
is the event identity** (`typeid(EventT).hash_code()` keys the bus) and its
members are the payload. No base class, no macro, no registration step —
`subscribe<EventT>` and `dispatch<EventT>` name the type directly, so a typo
is a compile error rather than a silently unrouted string.

Optional tracing hook: if the struct has a static `eventName` member, the
dispatcher copies it into `DispatchRecord::eventName`; without it the record
carries an empty name and the type hash only.

```cpp
namespace app::events {

struct AddTaskRequested {                    // intent
    static constexpr std::string_view eventName = "AddTaskRequested";
    std::string title;
};

struct TaskAdded {                           // fact
    static constexpr std::string_view eventName = "TaskAdded";
    std::uint64_t id = 0;
    std::string title;
};

}  // namespace app::events
```

The intent/fact split is a convention, not machinery — see
[Step 1](usage-guide.md#step-1-define-typed-struct-events).

### Dispatcher

The synchronous typed-struct event bus underneath the kernel. `dispatch` runs
handlers on the calling thread, in subscribe order, and returns only after
the last one finishes.

```cpp
using SubscriptionId = std::size_t;

struct DispatchRecord {
    std::size_t typeHash{};
    std::string_view eventName;    // empty when EventT has no eventName member
    std::size_t subscriberCount{};
};

Dispatcher() = default;
template <typename EventT>
SubscriptionId subscribe(const void* owner, std::function<void(const EventT&)> handler);
void unsubscribe(const void* owner);
void setObserver(std::function<void(const DispatchRecord&)> observer);
template <typename EventT> void dispatch(const EventT& event);
```

- **`owner` is an opaque cookie, never dereferenced**, used for bulk removal:
  `unsubscribe(owner)` drops every subscription made with that cookie, across
  all event types. Pass the subscribing component's **own address** — a
  foreign pointer compiles, dispatches fine, then silently detaches someone
  else's handlers.
- **Delivery is subscribe order** within one `EventT`.
- **Reentrancy contract.** `dispatch` snapshots the subscriber list up front.
  A handler that subscribes mid-dispatch joins the live vector and first
  fires on the *next* dispatch; one that unsubscribes mid-dispatch is
  skipped, because each snapshot entry is re-checked by id against the live
  list before it runs.
- **`SubscriptionId` is informational.** Removal is by owner; there is no
  `unsubscribe(id)`.
- **The observer slot is single and trace-only.** `setObserver` replaces
  whatever was there (an empty `std::function` clears it) and fires *before*
  the handler loop on every dispatch, zero-subscriber ones included. The
  contract is honor-system and unguarded: an observer must not subscribe,
  unsubscribe, or dispatch.

**Cost model.** `dispatch` is not allocation-free. Before the handler loop it
copies that event type's whole subscriber vector (`const std::vector<Entry>
snapshot = it->second;`), `std::function` trampolines included — a vector
allocation per dispatch, plus a copy of every subscriber's handler, each of
which allocates in turn unless it fits its `std::function`'s small-object
buffer. It then re-checks each snapshot entry against the live vector with
`std::any_of` before running it: quadratic in that type's subscriber count.
Only an event type nobody has subscribed to is cheap — the map lookup misses
and `dispatch` returns before the copy (the observer, if set, still sees the
record). None of that argues against frequent events at ordo's scale: an event
type typically has a handful of subscribers, and the farm examples stream
per-checkpoint progress through this bus into a live view. Treat it as a
shape, not a budget — no benchmark numbers are published here, so profile your
own hot path, and coalesce on the sending side (batch several updates into one
event) if a hot event has accumulated many subscribers.

```cpp
ordo::core::Dispatcher& bus = kernel.dispatcher();
bus.setObserver([](const ordo::core::DispatchRecord& r) {
    qCDebug(appTrace) << r.eventName << "->" << r.subscriberCount << "subscriber(s)";
});

bus.subscribe<app::events::TaskAdded>(this,                       // owner == own address
                                      [this](const app::events::TaskAdded& e) { append(e.title); });
bus.unsubscribe(this);
```

### Agent

Base class for named domain state holders. A concrete agent owns its data and
its invariants, mutates on request from a command, then publishes a fact.
Agents **send only** — the `AgentContext` they receive has no `subscribe`, so
the "never listen" rule is enforced by the type, not by review.

```cpp
explicit Agent(const std::string& name);
virtual ~Agent();
const std::string& name() const;
virtual void onRegister() {}
virtual void onRemove() {}
void setContext(KernelKey, AgentContext* context) noexcept;

protected:
AgentContext& context() const;
template <typename EventT> void send(const EventT& event);
```

- **`name()` is the registration key** and must be unique within a kernel.
- **`context()` is valid only while registered** — from just before
  `onRegister()` until just after `onRemove()`. Outside that window it throws
  `std::logic_error`. Constructing an agent and calling a mutator before
  `registerAgent` is the usual way to hit this.
- **`onRegister()` already has a context**, so an agent may publish a seed
  fact from it.
- `send(event)` is shorthand for `context().send(event)`;
  `setContext` is kernel-only (see [KernelKey](#kernelkey-and-hostkey)).

```cpp
class TaskList : public ordo::core::Agent {
public:
    static constexpr const char* kName = "tasks";
    TaskList() : Agent(kName) {}

    std::uint64_t add(const std::string& title) {
        const std::uint64_t newId = nextId_++;
        tasks_.push_back(Task{newId, title});
        context().send(events::TaskAdded{.id = newId, .title = title});   // fact, after the mutation
        return newId;
    }

private:
    std::uint64_t nextId_ = 1;
    std::vector<Task> tasks_;
};
```

### AgentContext

The capability view an agent receives at registration: publish events, look
up sibling agents — nothing else. No `subscribe`, no kernel handle.

```cpp
AgentContext(KernelKey, Kernel& kernel, Dispatcher& dispatcher);
template <typename EventT> void send(const EventT& event);
std::shared_ptr<Agent> agent(std::string_view name) const;
template <typename AgentT> std::shared_ptr<AgentT> agentAs(std::string_view name) const;
```

- **Owned by the kernel; lifetime == kernel lifetime**, so the raw pointer a
  registered agent holds to it cannot dangle.
- Constructible only by `Kernel` (the `KernelKey` parameter), so nobody can
  mint a context pointing somewhere else.
- `agent`/`agentAs` return nullptr when the name is unregistered; `agentAs`
  also on a type mismatch. Sibling lookup is for reading a collaborator's
  state mid-mutation; cross-agent *orchestration* belongs in a command.

### Command\<EventT\>

A transaction object created fresh for each dispatched `EventT` and destroyed
when `execute` returns. Pure interface: everything it needs arrives as
parameters — the event and the `CommandContext`. Business policy (validation,
rejection, ordering across agents) belongs here, not in the agent.

```cpp
template <typename EventT>
class Command {
public:
    virtual void execute(const EventT& event, CommandContext& context) = 0;
    virtual ~Command() = default;
};
```

- **Statelessness is by construction, not convention.** A command instance
  never survives its dispatch, so member state cannot accumulate across
  events; persistent dependencies are injected through `registerCommand`'s
  captured arguments.
- `CommandT` must derive from `Command<EventT>` and be constructible from the
  registered arguments — both are `static_assert`s in `registerCommand`.
- A rejected intent should mutate nothing and publish nothing: subscribers
  only hear about state that actually changed.

```cpp
class AddTaskCommand : public ordo::core::Command<events::AddTaskRequested> {
public:
    explicit AddTaskCommand(TaskOutput& output) : output_(output) {}

    void execute(const events::AddTaskRequested& event, ordo::core::CommandContext& context) override {
        const std::string title = trimmed(event.title);
        if (title.empty()) {
            output_.taskRejected("title is empty");   // 1:1 -- nobody else hears this
            return;                                   // no mutation, no fact
        }
        auto tasks = context.agentAs<TaskList>(TaskList::kName);
        if (!tasks) {
            output_.taskRejected("task list unavailable");
            return;
        }
        const auto id = tasks->add(title);            // agent sends TaskAdded (1:N)
        output_.taskAccepted(id, title);              // 1:1 echo to the caller
    }

private:
    TaskOutput& output_;
};
```

`TaskOutput` above is an application pattern, not framework machinery — see
[Clean Architecture Ports](usage-guide.md#clean-architecture-ports-the-ordo-way).

### CommandContext

The capability view handed to a command on each `execute` call: publish
events, look up agents — nothing else. Same shape and guarantees as
`AgentContext`, likewise kernel-owned and kernel-constructed.

```cpp
CommandContext(KernelKey, Kernel& kernel, Dispatcher& dispatcher);
template <typename EventT> void send(const EventT& event);
std::shared_ptr<Agent> agent(std::string_view name) const;
template <typename AgentT> std::shared_ptr<AgentT> agentAs(std::string_view name) const;
```

- One instance per kernel, reused for every dispatch — the `execute`
  parameter is a reference to the kernel's own, not a per-call object.
- `send` from a command dispatches *within* the current dispatch: nested
  handlers run to completion before it returns (see the reentrancy contract
  under [Dispatcher](#dispatcher)).
- Reachable outside a dispatch through `Kernel::commandContext()`, which is
  how tests drive command-layer helpers directly.

### PresenterContext

The capability view for the view side — the only context with `subscribe`. A
view adapter may listen to facts, publish intents, and read agents.

```cpp
PresenterContext(KernelKey, Kernel& kernel, Dispatcher& dispatcher);
template <typename EventT> void subscribe(const void* owner, std::function<void(const EventT&)> handler);
void unsubscribe(const void* owner);
template <typename EventT> void send(const EventT& event);
std::shared_ptr<Agent> agent(std::string_view name) const;
template <typename AgentT> std::shared_ptr<AgentT> agentAs(std::string_view name) const;
```

- `owner` carries the same rule as `Dispatcher::subscribe`: the subscriber's
  own address, and `unsubscribe(owner)` removes every subscription made with
  it. `ViewAdapter` fills this in with `this` automatically, so subclasses
  never pass a cookie by hand.
- Kernel-owned, kernel-constructed, lifetime == kernel lifetime.
- Reading an agent from the view side is for pull-style refresh (repopulating
  a list at registration); ongoing changes still arrive as facts.

### KernelKey and HostKey

Two empty passkey tokens, one per module: `ordo::core::KernelKey` (mintable
only by `Kernel`) and `ordo::qt::HostKey` (mintable only by `ViewHost`).
Both are non-copyable with a private default constructor and a single friend.

```cpp
class KernelKey {
    friend class Kernel;
    KernelKey() = default;

public:
    KernelKey(const KernelKey&) = delete;
    KernelKey& operator=(const KernelKey&) = delete;
};
```

A method taking one — `Agent::setContext`, `ViewAdapter::setContext`, the
three context constructors — is publicly visible but callable only from the
owner, because nobody else can produce the token. That confines the friend
surface to an empty class instead of opening a whole type. **You never write
`KernelKey{}` or `HostKey{}` in application code**; the tokens are listed here
only so the signatures read correctly.

Separately, `Kernel` uses a stable per-`(kernel, EventT)` address as the
dispatcher owner cookie for command factories. That is why
`removeCommand<EventT>()` removes exactly one factory: a plain `this` cookie
would have unsubscribed every command registration on the kernel at once.

### version.h

The version of `ordo::core` you compiled against. `core/CMakeLists.txt`
generates it with `configure_file` from `version.h.in`, so the single source of
truth is `project(ordo VERSION ...)` in the root `CMakeLists.txt`; the
generated header lands in the build tree (`generated/ordo/core/version.h`, on
the target's public include path) and is installed next to the hand-written
ones. Never edit the generated copy.

```cpp
#include <ordo/core/version.h>            // namespace ordo::core

inline constexpr int kVersionMajor = 0;   // PROJECT_VERSION_MAJOR
inline constexpr int kVersionMinor = 1;   // PROJECT_VERSION_MINOR
inline constexpr int kVersionPatch = 0;   // PROJECT_VERSION_PATCH
inline constexpr const char* kVersionString = "0.1.0";   // PROJECT_VERSION

const char* versionString();
```

- The constants are `constexpr`, so a consumer can gate on them at compile
  time; `versionString()` (defined in `core/src/version.cpp`) returns
  `kVersionString`, so it reports the version the linked `ordo::core` was
  built with. The values above are this checkout's.

---

## ordo::qt

### ViewHost

Owns view adapters (`Presenter`s, `ViewModel`s), injects each one's context,
and drives the register/remove lifecycle. Construct one per kernel at
bootstrap and let it own the view-side objects.

```cpp
explicit ViewHost(ordo::core::Kernel& kernel);
~ViewHost();                                    // == clear()
ViewHost(const ViewHost&) = delete;
ViewHost& operator=(const ViewHost&) = delete;
template <typename AdapterT, typename... Args> AdapterT* add(Args&&... args);
void clear();
```

- **`add` constructs in place and takes ownership**, then — in this order —
  injects the kernel's `PresenterContext` and calls `onRegister()`. That
  ordering is the contract behind "subscribe in `onRegister()`, never in the
  constructor": the context does not exist yet during construction.
- The returned pointer is **non-owning**, valid until `clear()` or host
  destruction. Store it for wiring (Qt connects, QML context properties), not
  for ownership.
- `AdapterT` must derive from `ViewAdapter` (`static_assert`).
- **`clear()` is LIFO in full**: it pops adapters from the back one at a
  time, running `onRemove()` then destroying each before moving to the next
  — so both `onRemove()` and destruction go newest-first. Destruction order
  is therefore the exact reverse of registration, the same order in which
  nested local variables unwind. The destructor is exactly `clear()`.

```cpp
ordo::qt::ViewHost host(kernel);
auto* viewModel = host.add<app::TaskListViewModel>();
engine.rootContext()->setContextProperty("taskListViewModel", viewModel);
```

### ViewAdapter

Base of the view-side roles: capability plumbing and registration lifecycle,
nothing view-specific — `Presenter` and `ViewModel` differ only in what they
add on top. Derives from `QObject`, so subclasses can carry signals, slots,
and properties.

```cpp
~ViewAdapter() override;
const QString& name() const;
virtual void onRegister() {}
virtual void onRemove() {}
void setContext(HostKey, ordo::core::PresenterContext* context) noexcept;

protected:
explicit ViewAdapter(const QString& name);
ordo::core::PresenterContext& context() const;
template <typename EventT, typename SelfT> void subscribe(void (SelfT::*memberFn)(const EventT&));
template <typename EventT> void subscribe(std::function<void(const EventT&)> handler);
```

- **The constructor is protected**: a base to derive from, never a component
  to instantiate.
- **`context()` requires registration.** `ViewHost::add` injects it before
  `onRegister()`, and it is valid from there on — never in the constructor.
  Calling it earlier trips a `Q_ASSERT_X` in debug builds.
- **Both `subscribe` overloads use `this` as the owner cookie**, always. The
  member-function overload deduces the most-derived type, so
  `subscribe<events::TaskAdded>(&TaskListViewModel::onTaskAdded)` needs no
  lambda and no cast.
- **The destructor unsubscribes everything** this adapter subscribed, any
  event type, so a destroyed adapter can never leave a dangling handler on the
  bus. This is why view-side objects need no teardown bookkeeping — and why
  command factory captures, which have no equivalent, do.
- `name()` is identity for logging only (category `ordo.view`); it is not a
  registration key and need not be unique.
- `Q_OBJECT` is needed in a subclass only when it declares its own signals,
  slots, or `Q_PROPERTY`s: `subscribe<EventT>(&Self::handler)` is a plain
  template and needs no moc (ordo's own package check runs a `Q_OBJECT`-free
  presenter).
- When a `Q_OBJECT` adapter's header lives in a different directory from its
  `.cpp` (a public `include/` split), CMake AUTOMOC's basename pairing misses
  it silently — list that header in the target's sources, as ordo's own
  `qt/CMakeLists.txt` does.

```cpp
void TaskListViewModel::onRegister() {
    subscribe<events::TaskAdded>(&TaskListViewModel::onTaskAdded);
    subscribe<events::TaskToggled>(&TaskListViewModel::onTaskToggled);
    subscribe<events::TaskRemoved>(&TaskListViewModel::onTaskRemoved);
}
```

### Presenter

View mediator: holds a view component, reacts to facts by updating that
component imperatively, and turns view signals into intents. The non-binding
half of the view side — for widget code and views that cannot express
bindings.

```cpp
explicit Presenter(const QString& name, QObject* viewComponent = nullptr);
QObject* viewComponent() const;
void setViewComponent(QObject* view);
```

- **`viewComponent` is non-owning.** The presenter never deletes it; the
  caller keeps managing its lifetime, in practice Qt's parent/child tree.
  `setViewComponent` swaps the pointer and does nothing else — no reparenting,
  no deletion of the previous one.
- The stored `QObject*` is a convenience root. Presenters that talk to several
  widgets typically hold their own typed pointers as members and leave
  `viewComponent` as the root handle (or null).
- Context, lifecycle hooks, `subscribe`, and auto-unsubscribe all come from
  [`ViewAdapter`](#viewadapter).

```cpp
class TaskListPresenter : public ordo::qt::Presenter {
public:
    TaskListPresenter(QListWidget* list, QObject* root = nullptr)
        : Presenter(QStringLiteral("TaskListPresenter"), root), list_(list) {}

    void onRegister() override { subscribe<events::TaskAdded>(&TaskListPresenter::onTaskAdded); }

    // The controller is a call site, not a class: view signal -> typed intent.
    void addTask(const QString& title) { context().send(events::AddTaskRequested{title.toStdString()}); }

private:
    void onTaskAdded(const events::TaskAdded& e) { list_->addItem(QString::fromStdString(e.title)); }

    QListWidget* list_;
};
```

### ViewModel

View-shaped state holder: exposes `Q_PROPERTY`/bindable state the view binds
to, plus slots and invokables that send intents. It carries **no reference to
any view** — that absence is the whole point of the role.

```cpp
class ViewModel : public ViewAdapter {
    Q_OBJECT

protected:
    explicit ViewModel(const QString& name);
};
```

- **The body is empty on purpose.** The role is defined by what it must *not*
  hold, which C++ cannot enforce; there is no state a base class could
  usefully add. Everything functional is inherited from
  [`ViewAdapter`](#viewadapter).
- **Derive from it; do not instantiate it.** The constructor is protected,
  mirroring `ViewAdapter`'s.
- **Projection state lives in the subclass, by composition.** A view-model
  mirroring a list owns an item model (say a `QStandardItemModel` member
  exposed as a `QAbstractItemModel*` property) rather than inheriting from
  one. The projection is a view-shaped copy, not the source of truth — the
  agent still owns that, and facts are the only writers of the copy.
- **It doubles as a runtime role tag.** `qobject_cast<ViewModel*>(adapter)`
  tells a state holder apart from a `Presenter` — useful to tooling that walks
  a host's adapters.

```cpp
class TaskListViewModel : public ordo::qt::ViewModel {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* items READ items CONSTANT)

public:
    TaskListViewModel() : ViewModel(QStringLiteral("TaskListViewModel")), items_(this) {}

    void onRegister() override { subscribe<events::TaskAdded>(&TaskListViewModel::onTaskAdded); }
    QAbstractItemModel* items() { return &items_; }

    Q_INVOKABLE void addTask(const QString& title) {
        context().send(events::AddTaskRequested{title.toStdString()});
    }

private:
    void onTaskAdded(const events::TaskAdded& e);   // appends a row to items_

    QStandardItemModel items_;   // composition: a projection, not the truth
};
```

Widget and QML bindings for the same view-model are shown in
[The Same Feature, MVVM Style](usage-guide.md#the-same-feature-mvvm-style).

---

## Lifetime & threading

**Contexts never dangle.** All three capability views are members of
`Kernel`, constructed after its dispatcher and living exactly as long as
the kernel, so any raw pointer to one held by a registered agent or adapter is
valid for the kernel's whole life.

| Holder | Context valid from | until |
|:---|:---|:---|
| `Agent` | just before `onRegister()` | just after `onRemove()` (then throws) |
| `ViewAdapter` | `ViewHost::add` injection, just before `onRegister()` | destruction |

**Subscriptions clean themselves up; command captures do not.**
`~ViewAdapter` unsubscribes by owner cookie, so a destroyed adapter leaves
nothing behind. A command factory holds its captured arguments for as long as
the registration lives — `std::ref` dependencies must outlive it.

**Teardown order**, as the example `main`s document it:

1. Whichever side holds a pointer into the other must die first — the rule
   depends on the adapter shape. A `ViewModel` is *read by* the view, so the
   window or QML engine dies first: declare the window after the host, or
   scope the engine to an inner block (`task-list-mvvm`, `task-list-mvvm-qml`).
   A `Presenter` *writes* the widgets, so the adapter dies first: declare the
   widgets before the host, so the host's teardown runs while they still
   exist (`task-list-clean`). Both orderings come from reverse-destruction
   order rather than from discipline.
2. `removeCommand<EventT>()` for every registration whose captured dependency
   is about to die (typically an adapter the host owns). Dropping the
   registration explicitly beats relying on "nothing dispatches after this
   point".
3. The `ViewHost` (`clear()`: LIFO `onRemove()` and destroy, both
   newest-first), then the kernel.

**Threading.** The dispatcher is synchronous and does no locking: `dispatch`
runs every handler on the calling thread before returning, and agents,
commands, and adapters are expected to live on one thread — the UI thread in a
Qt app. Cross-thread work is therefore an *application* seam, not a framework
feature: workers compute, and every result crosses back through one
marshalling point — a *relay* — that re-enters the kernel on the owning
thread. The simplest worked one is `DbRelay` in
[todomvc](../examples/todomvc/README.md) (`infra/db_worker.h` and `.cpp`,
wired in its `main.cpp`); the skeleton is the whole idea:

```cpp
// Lives on the UI thread. A plain QObject with no declared signals:
// invokeMethod with a lambda gives queued delivery without needing moc.
class Relay : public QObject {
public:
    std::function<void(const SomethingHappened&)> onResult;   // wired once, at bootstrap

    // Worker-thread callable: queues the sink onto the UI thread's event loop.
    void postResult(Payload payload) {
        QMetaObject::invokeMethod(
            this,
            [this, payload = std::move(payload)]() mutable {
                if (onResult) {
                    onResult(SomethingHappened{std::move(payload)});
                }
            },
            Qt::QueuedConnection);
    }
};

// Bootstrap, UI thread. The sink re-enters the domain loop as an ordinary event.
Relay relay;
relay.onResult = [&kernel](const SomethingHappened& e) { kernel.send(e); };
```

Nothing there is framework machinery. The relay's thread affinity is the UI
thread, so `Qt::QueuedConnection` defers the lambda to that thread's event
loop; its only state is sinks assigned once at bootstrap; and `kernel.send`
therefore only ever runs on the owning thread — no locks in ordo-facing code.

`BakeRelay` in [mesh-farm](../examples/mesh-farm/README.md) and `GenRelay` in
[galaxy-farm](../examples/galaxy-farm/README.md) are the same pattern scaled
up — a thread pool feeding a GPU-backed view through a single relay.

---

## Error surface

The framework is deliberately quiet: most failure modes are compile errors or
null returns, and only one is an exception.

**Compile time**

| Check | Where |
|:---|:---|
| `CommandT must derive from Command<EventT>` | `Kernel::registerCommand` |
| `CommandT must be constructible from the registered arguments` | `Kernel::registerCommand` |
| `AdapterT must derive from ViewAdapter (Presenter or ViewModel)` | `ViewHost::add` |
| Event type mismatch between `subscribe<EventT>` and its handler | template deduction |

**Run time**

| Situation | Result |
|:---|:---|
| `Agent::context()` while unregistered | throws `std::logic_error` — `"Agent '<name>' is not registered with a kernel"` |
| `ViewAdapter::context()` before injection | `Q_ASSERT_X` in debug builds, undefined in release — treat "no `context()` in the constructor" as a hard rule |
| `agent(name)` with an unregistered name | `nullptr` |
| `agentAs<T>(name)`, unregistered name **or** type mismatch | `nullptr` — the two cases are indistinguishable |
| `removeAgent(name)` with an unregistered name | `false`, no other effect |
| `dispatch<EventT>` with no subscribers | no-op (the observer, if set, still sees the record) |
| `unsubscribe(owner)` with an unknown cookie | no-op |
| `unsubscribe(owner)` with someone else's cookie | **silently removes their handlers** — no diagnostic exists; this is why the own-address rule matters |
| An exception thrown by a handler | propagates out of `dispatch` to the sender; the remaining subscribers for that event do not run |

Nothing in the framework logs errors. The only logging is the debug category
`ordo.view`, which traces adapter creation and subscription.
