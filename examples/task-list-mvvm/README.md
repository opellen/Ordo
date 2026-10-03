# task-list-mvvm

The usage guide's task-list feature, MVVM-shaped: the domain loop is
identical to the Presenter version -- only the view adapter differs. A
`TaskListViewModel` exposes observable state instead of a Presenter poking
widgets directly, and doubles as the add-task use case's output port, so
`TaskListWindow` binds to it declaratively and never reaches into the domain
loop itself.

## Layout

```
model/
  task_events.h                  -- AddTaskRequested intent, TaskAdded fact
  task_output.h                  -- TaskOutput: the output port interface
  task_list.h / .cpp             -- TaskList Agent: owns the tasks, applies the add rule
  task_commands.h / .cpp         -- AddTaskCommand: trims, rejects, or mutates + echoes
viewmodel/
  task_list_view_model.h / .cpp  -- TaskListViewModel: observable state + the output-port impl
view/
  task_list_window.h / .cpp      -- TaskListWindow: plain widgets bound to the view-model
main.cpp                         -- bootstrap, wiring, --smoke
```

## Role map

- `model/` -- the domain: the `TaskList` agent, the `AddTaskRequested` /
  `TaskAdded` events, the `TaskOutput` port, and `AddTaskCommand`, the use
  case that uses both.
- `viewmodel/` -- `TaskListViewModel`, which both subscribes to the 1:N
  `TaskAdded` fact and implements the 1:1 `TaskOutput` port, so both result
  channels land as observable state.
- `view/` -- `TaskListWindow`, the plain-widget window that binds to the
  view-model.
- `main.cpp` -- the composition root: wires the agent, the command, the
  view-model, and the window together, and doubles as the `--smoke` entry
  point.

See the ordo usage guide's ["The Same Feature, MVVM
Style"](../../docs/usage-guide.md#the-same-feature-mvvm-style) and ["Clean
Architecture Ports, the Ordo
Way"](../../docs/usage-guide.md#clean-architecture-ports-the-ordo-way)
sections for the concepts behind the ViewModel and the output port.

[`../task-list-clean`](../task-list-clean) is the same feature's
Presenter-first twin: same domain loop, an imperative `Presenter` instead of
a `ViewModel` for the adapter layer.

## Build & run

```
cmake -S examples/task-list-mvvm -B build-mvvm -G Ninja -DCMAKE_PREFIX_PATH=<Qt6>
cmake --build build-mvvm
./build-mvvm/task-list-mvvm            # the app
./build-mvvm/task-list-mvvm --smoke    # headless self-check, exit 0
```
