# todomvc

The classic [TodoMVC](https://todomvc.com/) feature set -- add / toggle /
destroy / edit (with edit-to-empty destroying the todo) / All-Active-Completed
filters / an items-left counter / clear-completed / toggle-all -- as a Qt
Widgets app on ordo, translated file-for-role from the PureMVC JS demo:
[`puremvc-js-demo-todomvc`](https://github.com/PureMVC/puremvc-js-demo-todomvc).

Todos now survive a restart. Every mutation still lands on `TodoListAgent` first, exactly
as before, but each one also mirrors into a SQLite table, and every touch of that table --
load, insert, update, delete -- runs on one dedicated worker thread, never the UI thread:
the Android main-thread-I/O ban, desktop edition. This is a worked example of a
write-through persistence lane built from ordo's ordinary vocabulary, not a claim that
ordo ships some new persistence feature.

## Layout

```
model/
  todo.h                     -- Filter, Todo: the vocabulary both feature lanes share
  todo_events.h              -- the seven user intents and the TodosFiltered fact: the
                                 TodoMVC feature's own vocabulary
  persistence_events.h       -- LoadTodosRequested, TodosLoaded, PersistCompleted: the
                                 write-through persistence lane's own vocabulary
  todo_list.h / .cpp         -- TodoListAgent: owns todos + filter + persistence status
                                 (loaded, pendingWrites), publishes the fact
controller/
  todo_commands.h / .cpp     -- seven intent Commands (the demo's switch, unrolled)
  persistence_commands.h / .cpp -- LoadTodosCommand/TodosLoadedCommand/
                                 PersistCompletedCommand
infra/
  db_worker.h / .cpp         -- DbRelay + DbWorker: the SQLite worker-thread lane (see
                                 "The serial I/O lane" below)
view/
  todo_view_model.h / .cpp   -- TodoViewModel: view-shaped state (now incl. loaded/
                                 pendingWrites/persistError) + intent-sending slots
  todo_window.h / .cpp       -- TodoWindow: plain widgets bound to the view-model, incl.
                                 the loading overlay
main.cpp                     -- bootstrap, composition root, --slow-io/--db, --smoke
```

Events and commands are grouped by feature lane, never by kind -- an intent and the fact
it produces stay in the same file -- and todomvc is the only example laid out this way
because it alone has two real feature lanes, the TodoMVC feature itself and the
write-through persistence lane.

## Translation map

The folders above now mirror the JS demo's own `js/src/{model,view,controller}` layout
folder-for-folder, with `infra/` as the one addition the demo never needed -- its
persistence was a synchronous `localStorage` call.

| PureMVC JS demo | this example | Notes |
|---|---|---|
| `Facade` (multiton core) | `Kernel` | instantiable, never a singleton -- two kernels are fully isolated |
| `StartupCommand` (MacroCommand: PrepController -> PrepModel -> PrepView) | plain bootstrap code in `main()` | ordo needs no startup commands; the three `Prep*` steps are just `registerAgent`, `host.add<TodoViewModel>()`, and the seven `registerCommand` calls, run in order and commented as such |
| `TodoProxy` | `TodoListAgent` | owns todos AND the current filter, same as the demo's one proxy; the demo's `localStorage` round trip is now a real SQLite table on a dedicated worker thread (see "The serial I/O lane" below) -- but the filter isn't part of that trip: the schema stays one `todos` table, so the active filter resets to All on every launch |
| `TodoCommand` (one `SimpleCommand`, switches on notification name) | seven typed per-intent `Command`s in `controller/todo_commands.h` | typed intent events give each notification its own C++ type, so the name-switch has nothing left to switch on |
| `TodoFormMediator` + `TodoForm` + `AppEvents` DOM plumbing | `TodoViewModel` + Qt signal/slot wiring in `main()` | the mediator's `handleEvent()` switch becomes the view-model's slots; its `handleNotification()` switch becomes `onTodosFiltered()` |
| `RoutesMediator` (URL router, `/`, `/active`, `/completed`) | three filter buttons sending `SetFilterRequested` | a desktop app has no URL to route through, so the router disappears and the buttons call the intent directly |
| `TODOS_FILTERED` notification (`{ todos, stats, filter }`) | `events::TodosFiltered` fact | a typed struct instead of an untyped body; `stats` is unpacked into `totalCount`/`activeCount`/`completedCount` |
| per-row toggle via `UPDATE_TODO` (`updateTodo` also handles title + completed together) | its own `ToggleTodoRequested` intent, separate from `EditTodoRequested` | one event per meaning instead of one event overloaded for two different edits |
| per-row `✕` destroy button, one per row | a single "Delete" button acting on the selected row | simplest robust Widgets translation of a per-row destroy control; see "Simplifications" below |

## Where policy lives

Trimming, "an empty add is ignored", and "editing a title down to empty
destroys the todo" are all business rules, so they live in `controller/todo_commands.h` --
`AddTodoCommand` and `EditTodoCommand` -- not in `TodoListAgent`. The agent
stays a plain state owner: it applies whatever mutation it's told to apply,
recomputes stats and the filtered view, and broadcasts the result. This is
the same split the `task-list-mvvm` twin uses (`task_commands.h` trims and
rejects, `task_list.h` never does).

The same split holds for the new persistence calls: `db_.add(...)`, `db_.setTitle(...)`,
and the rest are issued from `controller/todo_commands.h` too, right after each mutation --
`TodoListAgent` never touches `DbWorker` at all, so it stays exactly as ignorant of
persistence as it is of trimming.

## The serial I/O lane

One dedicated `QThread`, owned by `DbWorker`, owns the one `QSQLITE` connection for the
whole app's lifetime. That is not a stylistic choice: Qt's SQL module only lets a
connection be used from the thread that created it, so `DbExecutor::open()` -- which
calls `QSqlDatabase::addDatabase` -- has to run on that thread too, and it does, as the
first op ever queued onto the executor. Every later op (`load`, `add`, `setTitle`,
`setCompleted`, `remove`, `removeCompleted`, `setAllCompleted`) queues onto that same
`DbExecutor` via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`, and Qt delivers
queued calls to one receiver in the order they were posted -- so mutation order survives
the trip to disk for free, no sequence numbers or reordering logic needed anywhere above
this file. (`DbWorker` does stamp each write with an `opSeq`, but only so
`PersistCompleted` can be traced back to its write for diagnostics; nothing reorders on
it.)

This is the deliberate contrast with `mesh-farm`'s worker pool: that example spreads bakes
across a `QThreadPool` of several threads, on purpose, because the work is CPU-bound and
parallel, unordered completion is the whole point -- which is exactly why it needs
generation stamps to detect and discard stale results. This example's I/O is the opposite
shape: one thread, one connection, one FIFO lane, because a todo list's mutation order is
part of its correctness, not incidental to it. Two worker-thread shapes for two different
jobs, both built from ordo's ordinary vocabulary.

Code running on the worker thread, inside `DbExecutor`, obeys the same iron rule
`mesh-farm`'s runnables do: it may touch exactly two things, its own `QSqlDatabase`
connection and `DbRelay`'s `post*` methods -- never the ordo dispatcher, never
`TodoListAgent`, never a widget.

`DbWorker`'s destructor has one trap worth naming: a bare `thread_.quit()` would race every
write still sitting in the queue, since quitting stops the event loop without saying which
already-posted op runs last -- a pending `add()` or `remove()` could be dropped on the
floor. Instead the destructor queues its own closing lambda (`close()` then
`QThread::currentThread()->quit()`) through the same `invokeMethod` path as every other op,
so the FIFO lane guarantees it: the queued quit only runs after every earlier queued write
has actually drained.

## Write-through, memory-authoritative persistence

`TodoListAgent`'s mutators -- `add`/`toggle`/`destroy`/`edit`/`clearCompleted`/`toggleAll`
-- are exactly the functions they were before persistence existed: each mutates `todos_`
synchronously and calls `publishSnapshot()` immediately, so the view always reflects the
agent's in-memory state right away. The agent stays the one source of truth; persistence
rides along afterward, not instead of that. Each of the six mutating commands in
`controller/todo_commands.h` calls its agent mutator first, then enqueues the mirroring `DbWorker` op, then
calls `todos->notePersistQueued()` -- the UI thread never waits on a write to land.

The only visible trace that a write is in flight is `TodoListAgent`'s `pendingWrites_`
counter: `notePersistQueued()` increments it, `notePersistCompleted()` (called once
`PersistCompletedCommand` relays `DbRelay`'s reply back in) decrements it.
`TodoViewModel::pendingWrites()` drives a label next to the items-left count -- "saving…"
while it's above zero, "saved ✓" once it drops back to zero -- and a separate red
(`#c62828`) label appears only when the most recent write failed, showing
`lastPersistError`'s text.

Rejected alternative: a DB-authoritative round trip, where a mutation waits on its write
before updating the view, was left out on purpose -- it would mean a laggy UI and a busy
state on every keystroke and checkbox, the wrong lesson for an example about keeping I/O
off the UI thread.

## The loading overlay

A semi-transparent overlay covers the whole window from construction until the first
`TodosLoaded` arrives, success or failure alike -- `TodoListAgent::adoptLoaded` flips
`loaded_` to `true` either way, so the overlay doesn't wait around for a load error to
resolve; the error surfaces separately through the persist-error label above.

"Modal" here names an input-blocking UI *state*, not a blocked thread: the indeterminate
busy bar underneath keeps animating, and the window keeps repainting, precisely because the
load never touches the UI thread's event loop -- that work happens entirely on the worker
thread described above. The overlay's blocking is mechanical, not systemic: it is simply
the top-most opaque widget in the stack, so it intercepts every mouse event meant for the
widgets underneath and never forwards them -- that intercept is the entire mechanism, not
disabled widgets and not a blocked thread. One Qt trap is worth flagging: a plain `QWidget`
paints no background of its own, so without `Qt::WA_StyledBackground` set explicitly, the
overlay's `background: rgba(...)` stylesheet is silently ignored and the whole thing
renders invisible.

A genuinely modal overlay -- one that should block interaction because partial input would
be meaningless, the way a migration or an export-class operation might -- would need more
than an opaque intercept. No such operation exists in this example; the loading overlay's
one job is bridging the transient window between startup and the first `TodosLoaded`.

## The schema

The persisted schema is one table:

```sql
CREATE TABLE IF NOT EXISTS todos (
    id INTEGER PRIMARY KEY,
    title TEXT NOT NULL,
    completed INTEGER NOT NULL
)
```

There is no position/order column. `TodoListAgent::add` mints ids from a monotonically
increasing counter and always appends to the end of `todos_`, so id order and insertion
order are the same sequence -- the same order the rest of the app already relies on for its
vector. `DbExecutor::load`'s `SELECT ... ORDER BY id` reproduces that order on reload, so
the UI-thread agent can zip the query's rows straight into `todos_` with no separate sort
and no id-based reconciliation.

Each row's `id` is the agent's own id, not a value SQLite assigned -- `add()`'s `INSERT`
supplies it explicitly. That is what makes id continuity possible: on reload,
`TodoListAgent::adoptLoaded` scans the loaded rows for the highest id and sets `nextId_` to
one past it, so the next `add()` after a restart can never collide with a row already on
disk.

## Command-line flags

Two flags configure persistence for the real app (`--smoke` ignores both -- it builds its
own temp-file path per run, see below):

- **`--slow-io <ms>`** sleeps for `<ms>` milliseconds at the top of every `DbExecutor`
  operation, load included, before it touches the database. The sleep runs on the worker
  thread, not the UI thread -- the same idea as a devtools network throttle: the honest way
  to make the loading overlay and the saving indicator actually observable, since a real
  SQLite file on a fast local disk is usually too quick to see either state change. Try
  `--slow-io 500` to watch the overlay linger.
- **`--db <path>`** overrides the default database file. Without it, the app writes to
  `<AppDataLocation>/todomvc.db`, creating the directory first if it doesn't exist.

## The smoke check

`--smoke` runs three phases over one on-disk file (a temp path keyed by process id, removed
before phase 1 so a stale file from a killed prior run can't leak rows in):

1. **Spec drive + persisted writes + drain.** A fresh session waits for `loaded()`, then
   runs the full in-memory TodoMVC spec -- the same feature set as before persistence: two
   adds, the blank-add-ignored rule, toggle, all three filters, edit,
   edit-to-empty-destroys, toggle-all, clear-completed. It then adds and toggles one more
   todo, asserts the final board, and waits for `pendingWrites() == 0` before the session --
   and with it, `DbWorker` -- goes out of scope, draining every queued write through the
   destructor described above.
2. **A fresh session over the same file**, asserting the exact reloaded board
   (`[a (active), persist me (completed)]`) and then adding one more todo to prove id
   continuity: if `nextId_` had restarted at 1 instead of continuing past the highest id on
   disk, the insert would collide with an existing row and `totalCount` would never reach 3.
3. **A slow-io session** (`slowIoMs = 80`) that records whether `loaded()` was ever observed
   `false` before it flipped `true` -- proving the loading state is a real transition the
   app passes through, not a value that merely starts `false` by coincidence.

A passing run prints:

```
PASS: todomvc smoke (spec + sqlite round trip, worker-thread io)
```

## Simplifications from the demo

- **No filter persistence.** The demo's `TodoProxy` round-tripped through `localStorage` on
  every mutation, filter included, so a reload reopened on whichever route was last active.
  This example's schema stays one `todos` table (see "The schema" above) -- todos persist,
  the active filter does not, so every launch starts back on All.
- **No URL routing.** `RoutesMediator` exists because the demo runs in a
  browser and wants `/active` and `/completed` to be bookmarkable URLs. A
  desktop app has no address bar, so the three filter buttons just send
  `SetFilterRequested` directly -- no router needed.
- **One Delete button instead of a per-row `✕`.** The demo renders a destroy
  button inside every `<li>`. A `QListWidget` doesn't offer an easy per-row
  custom control without a delegate, so this example uses one "Delete" button
  that acts on the currently-selected row. Double-click-to-edit still comes
  for free from Qt's `Qt::ItemIsEditable` flag, matching the demo's
  dblclick-to-edit behavior.

## Build & run

```
cmake -S examples/todomvc -B build-todomvc -G Ninja -DCMAKE_PREFIX_PATH=<Qt6>
cmake --build build-todomvc
./build-todomvc/todomvc            # the app
./build-todomvc/todomvc --smoke    # headless self-check, exit 0
```

`CMakeLists.txt` now links `Qt6::Sql` alongside `Qt6::Widgets` -- the `QSQLITE` driver
ships as a plugin inside Qt itself, so there is nothing extra to vendor or install. Qt's
and MinGW's runtime DLLs still need to be on `PATH` to run the binary, exactly as before.
