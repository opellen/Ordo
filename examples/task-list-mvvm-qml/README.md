# task-list-mvvm-qml

The usage guide's task-list feature with a QML view: add a task, toggle it
done, remove it. The add path is the same domain loop as the widgets sibling
[`../task-list-mvvm`](../task-list-mvvm) -- same events, same agent, same
command, same output port -- with toggle and remove added so the list has
update and erase paths too. What actually differs is the view adapter:
`TaskListViewModel` is a `QObject` facade that owns a row-shaped projection
of the list, and `Main.qml` binds to it and calls its invokables, never
touching the domain loop itself.

## Where did the Controller go?

Developers arriving from other MVC frameworks keep asking the same question
about Qt: there is a Model, there is a View, so where is the Controller?
Qt's own [Model/View
Programming](https://doc.qt.io/qt-6/model-view-programming.html) document
answers it plainly. Its introduction: *"If the view and the controller
objects are combined, the result is the model/view architecture."* And from
the Delegate Classes section: *"Unlike the Model-View-Controller pattern,
the model/view design does not include a completely separate component for
managing interaction with the user."*

So the C is not missing by accident, and a delegate is not a stand-in for
it. Qt deliberately folded the controller into the view and left the seat
open for the application to fill. This example fills it with ordo: commands
carry the policy, agents own the state, and typed events connect the two.
Everything above the event bus stays a view adapter.

## Layout

```
model/
  task_events.h                  -- Add/Toggle/RemoveTaskRequested intents,
                                    TaskAdded/TaskToggled/TaskRemoved facts
  task_output.h                  -- TaskOutput: the add use case's output port
  task_list.h / .cpp             -- TaskList Agent: owns the tasks, mutates + publishes
  task_commands.h / .cpp         -- AddTaskCommand (trims, rejects, or mutates + echoes),
                                    ToggleTaskCommand / RemoveTaskCommand (broadcast-only)
viewmodel/
  task_list_view_model.h / .cpp  -- TaskListViewModel: the projection, the invokable
                                    intents, and the output-port implementation
view/
  Main.qml                       -- ApplicationWindow: bindings and invokable calls only
main.cpp                         -- bootstrap, wiring, --smoke / --qml-smoke
```

## Role map

- **M** -- `model/`. `TaskList` is the agent that owns `std::vector<Task>`
  and is the only thing that mutates it; the three commands carry the
  policy. `AddTaskCommand` trims the title and rejects an empty one before
  anything is written. `ToggleTaskCommand` and `RemoveTaskCommand` look the
  agent up and delegate. No command holds view state, no agent knows a view
  exists.
- **VM** -- `viewmodel/TaskListViewModel`, an `ordo::qt::ViewModel` (so a
  `QObject`) that exposes `items` (a `QAbstractItemModel*`, CONSTANT) and
  `lastError` (NOTIFY), three `Q_INVOKABLE` intents, and an `inputAccepted`
  signal. It also implements `TaskOutput`. Behind `items` sits a
  `QStandardItemModel` whose role names are set once in the constructor:
  `TitleRole -> "title"`, `DoneRole -> "done"`, `IdRole -> "taskId"`.
- **V** -- `view/Main.qml`. A `TextField`, an error `Label`, and a
  `ListView` whose delegate reads `model.title`, `model.done`,
  `model.taskId`. Input handlers do exactly one thing: call
  `taskListViewModel.addTask/toggleTask/removeTask`.
- `main.cpp` -- the composition root and the only place `Kernel` appears.
  It registers the agent, creates the view-model through an
  `ordo::qt::ViewHost`, registers the three commands (the add command gets
  the view-model as its port via `registerCommand`'s argument forwarding),
  and exposes the view-model to QML as the context property
  `taskListViewModel`. The `QQmlApplicationEngine` lives in an inner block
  so it is destroyed before the `ViewHost` it reads from, and the command
  registrations are dropped explicitly at the end.

## Event flow

```
  Main.qml            onClicked: taskListViewModel.addTask(input.text)
     |
     |  Q_INVOKABLE
     v
  TaskListViewModel --send--> AddTaskRequested            (intent)
                                    |
                                    v
                              AddTaskCommand              (policy: trim, reject empty)
                                |         |
               tasks->add(title)|         | output_.taskRejected / taskAccepted
                                v         v
                        TaskList agent    TaskListViewModel        (1:1, caller only)
                        mutates tasks_    lastErrorChanged / inputAccepted
                                |                    |
                --send--> TaskAdded (fact, 1:N)      |
                                |                    |
                                v                    v
              TaskListViewModel::onTaskAdded    error Label shows / input
              appends a row to the projection   clears and refocuses
                                |
                                v
              ListView rebinds on title / done / taskId
```

Two result channels leave the add command, and they carry different things.
The output port is 1:1 and caller-directed: "your title was empty" belongs
next to the input field that produced it, not on every open view.
`TaskAdded` is 1:N and factual: the task now exists, and anyone who cares
may react.

Toggle and remove are broadcast-only on purpose. Their ids can only have
come from a `TaskAdded` fact the view already received, so an unknown id
means a stale view, not a user mistake -- the commands return silently
rather than invent an error channel. Add is the one that validates, because
add is the one that takes free text from a human.

## The QStandardItemModel objection

Experienced Qt developers will point out that `QStandardItemModel` usually
leads to bad design that breaks MVC, and they are right about the case they
have in mind: reaching for it as *the* model, so the source of truth ends up
living inside a class built around view concerns -- items, columns, display
roles, editability.

That is not what happens here. The truth is `TaskList::tasks_`, a plain
`std::vector<Task>` in the domain layer. The item model is a disposable
view-side projection of it, and the flow is strictly one-way:

- The only writers of `items_` are the three fact handlers
  (`onTaskAdded` appends, `onTaskToggled` sets `DoneRole`, `onTaskRemoved`
  erases the row). Nothing else in the codebase touches it.
- QML never writes a row. Items are created with `setEditable(false)`, and
  the checkbox uses `Binding on checked { value: model.done }` rather than
  `checked: model.done`, so a click cannot sever the binding and leave the
  row showing a state the domain never agreed to. The click's only effect is
  `onToggled: taskListViewModel.toggleTask(model.taskId)`; the checkmark
  moves when `TaskToggled` comes back.
- Delete the projection and rebuild it from the facts and nothing is lost.

Used that way the class does the one job it is genuinely good at: being a
role-named row container that QML already knows how to display, for free.

## Variation: a hand-rolled QAbstractListModel

The canonical alternative is a custom `QAbstractListModel` over the
view-model's own row structs, emitting `dataChanged` / `beginInsertRows` /
`beginRemoveRows` by hand. It costs more code and gives tighter control over
roles, signal granularity, and batching -- worth it once the list grows
large or the rows carry more than three fields. The architecture does not
change at all: it is still a projection written only by fact handlers, and
`Main.qml` would not need a single edit.

## Build & run

```
cmake -S examples/task-list-mvvm-qml -B build-mvvm-qml -G Ninja -DCMAKE_PREFIX_PATH=<Qt6>
cmake --build build-mvvm-qml
./build-mvvm-qml/task-list-mvvm-qml               # the app
./build-mvvm-qml/task-list-mvvm-qml --smoke       # headless self-check, exit 0
./build-mvvm-qml/task-list-mvvm-qml --qml-smoke   # offscreen QML load check, exit 0
```

`--smoke` drives the kernel directly with no QML engine and checks both
channels: a blank title must surface through the port, a valid one must
appear in the projection with the right three roles, and toggle then remove
must update and erase the row. `--qml-smoke` forces the offscreen platform
and verifies that `Main.qml` loads.

## Relation to the sibling

[`../task-list-mvvm`](../task-list-mvvm) is the same feature with a plain
widgets view and the same add-path domain files; put the two view adapters
side by side to see what MVVM buys once the view goes declarative. The ordo
usage guide's ["The Same Feature,
MVVM Style"](../../docs/usage-guide.md#the-same-feature-mvvm-style) and
["Clean Architecture Ports, the Ordo
Way"](../../docs/usage-guide.md#clean-architecture-ports-the-ordo-way)
sections cover the concepts behind the view-model and the output port.
