# Conventions

Idioms an ordo app is expected to follow. None of this is enforced by the
compiler except where noted — it is the shape that keeps a directory listing
self-explanatory and a bootstrap function correct by inspection.

---

## Domain file naming

**Never a bare `events.h` or `commands.h`.** Domain-qualify every file name so
a directory listing alone reveals what domain it belongs to and what's in it:

- `todo_events.h` — one feature's whole event vocabulary: its intents and the
  fact(s) they produce, together in one file (see
  [`examples/todomvc/model/todo_events.h`](../../../examples/todomvc/model/todo_events.h)).
- `persistence_events.h` — a second feature lane gets its own event file, not
  a shared dumping ground (see
  [`examples/todomvc/model/persistence_events.h`](../../../examples/todomvc/model/persistence_events.h)).
- `todo_commands.h` / `persistence_commands.h` — commands grouped **by
  cohesive family**, not one file per class. todomvc's seven per-intent
  commands split into exactly two files along the same feature-lane boundary
  as the events (see
  [`examples/todomvc/controller/todo_commands.h`](../../../examples/todomvc/controller/todo_commands.h),
  [`controller/persistence_commands.h`](../../../examples/todomvc/controller/persistence_commands.h)).

Rule of thumb: **the file name names the domain concept, not the ordo role.**
`todo_events.h`, never `events.h`; `task_commands.h`, never `commands.h`.

## Directory layout

Mirror `examples/todomvc/`'s split:

```
model/        events, agents, plain domain types (todo.h, todo_events.h, todo_list.h)
controller/   commands, grouped by feature lane (todo_commands.h/.cpp, persistence_commands.h/.cpp)
view/         Presenters/ViewModels and the widgets/QML they drive
infra/        anything crossing a thread or process boundary (db_worker.h/.cpp)
```

A reader should be able to tell the domain's intents, facts, and command
families apart from the file listing alone, with no need to open a file first.

Composition-root glue — a per-document session type owning a kernel plus its
registrations, a multi-pane shell, a project loader — is not a framework role
and gets no directory of its own: it lives in `view/` beside the shell that
uses it (a `DocumentSession` next to the main window), or in `main.cpp` while
it is still small.

---

## Bootstrap idiom

The composition root is **plain code, run once, in order** — there is no
startup-command framework, no lifecycle hook to implement. See
[`examples/todomvc/main.cpp`](../../../examples/todomvc/main.cpp)'s non-smoke
branch for the canonical shape:

1. Construct `ordo::core::Kernel kernel;`.
2. `kernel.registerAgent(...)` for every agent, before anything looks one up.
3. Construct the `ordo::qt::ViewHost host(kernel);`, then `host.add<...>(...)`
   each adapter.
4. Construct any relay/worker infra (todomvc's `DbRelay` + `DbWorker`) and
   wire its sinks to `kernel.send(...)`.
5. `kernel.registerCommand<EventT, CommandT>(...)` for every intent.
6. **One final intent send kicks off the pipeline** — todomvc ends its
   registrations with `kernel.send(app::events::LoadTodosRequested{})`. There
   is no other way to "start" an ordo app: the first event is just an event,
   sent the same way a button press would send one.

One ordering caveat inside this sequence: an adapter that *sends* from its
own `onRegister()` (a snapshot pull, say) only reaches commands already
registered before the `host.add<...>()` call — with the add-before-commands
order above, that first send is a silent no-op. Harmless when the initial
state is empty; register the answering command before the add (or accept the
no-op) when it is not.

**Declaration order is reverse teardown order.** Locals are destroyed in
reverse declaration order, so declare things in the order they must *die*
last. `kernel` always comes first. The view side depends on the adapter
shape (see Teardown order below): a `Presenter` writes into widgets, so the
widgets are declared *before* the `ViewHost` that owns it; a `ViewModel` is
read *by* the view, so the window/QML engine is declared *after* the host —
todomvc declares `window` last for exactly this reason. A relay/worker
(todomvc's `dbWorker`, declared after `kernel`/`dbRelay`) is destroyed
*first*, so its drain-then-quit destructor finishes queued writes while the
kernel it calls back into still exists.

---

## Teardown order

Three steps, always in this order (`main.cpp`'s post-`application.exec()`
block, or a `ViewModel` shape's destructor block):

1. **View layer first** — whichever side holds a pointer into the other dies
   first. A `Presenter` writes widgets, so destroy the adapter (host `clear()`
   or scope exit) while the widgets still exist; a `ViewModel` is read *by*
   the view, so the window/QML engine dies first instead. See
   [api.md, Lifetime & threading](../../api.md#lifetime--threading).
2. **`removeCommand<EventT>()` for every registered command** whose captured
   dependency (a `std::ref` to a worker, a port, an adapter) is about to die.
   Command factory captures have no auto-cleanup, unlike subscriptions —
   drop the registration explicitly rather than relying on "nothing
   dispatches after this point."
3. **The kernel** goes out of scope last (or `ViewHost`'s `clear()` runs,
   which is LIFO `onRemove()`-then-destroy, then the kernel).

## Threading

The dispatcher is **synchronous** and does no locking — one thread only.
Cross-thread work is an application-level seam, not a framework feature: a
worker computes off-thread, and every result crosses back through exactly one
**relay** object whose queued delivery (`QMetaObject::invokeMethod(...,
Qt::QueuedConnection)`) re-enters `kernel.send(...)` on the UI thread. The
model is `DbRelay` in
[`examples/todomvc/infra/db_worker.h`](../../../examples/todomvc/infra/db_worker.h):
its `postLoaded`/`postPersistCompleted` are worker-thread-callable, and the
`onLoaded`/`onPersistCompleted` sinks wired in `main.cpp` call `kernel.send`
only once already back on the UI thread. Never call `kernel.send` (or touch
any agent/command/adapter) from a worker thread directly.

## Multi-kernel operating model

**One kernel per document.** `Kernel` is instantiable, never a singleton —
several kernels coexist sharing no dispatcher, no agents, no command
registrations (see
[usage-guide.md, Multi-Document Applications](../../usage-guide.md#multi-document-applications)).

- Switching the active document means **destroying** that document's
  `ViewHost` and every adapter under it, then **building new ones** against
  the other kernel — never rebinding existing adapters to a different kernel.
- This is a compile-time guarantee, not a discipline to remember:
  `ViewHost` holds its kernel's `PresenterContext` as a reference member and
  is non-copyable/non-assignable, and `ViewAdapter::setContext` is gated by a
  `HostKey` passkey only `ViewHost::add()` can mint (see
  [`qt/include/ordo/qt/view_adapter.h`](../../../qt/include/ordo/qt/view_adapter.h),
  [`qt/include/ordo/qt/view_host.h`](../../../qt/include/ordo/qt/view_host.h)).
  There is no expressible way to point an adapter at a second kernel.
- Anything that spans documents (a document list, "close all") lives above
  both kernels in the composition root — it reads one kernel and sends an
  intent into the other; neither kernel gains a reference to its sibling.
