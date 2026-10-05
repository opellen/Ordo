# task-list-clean

The same task-list feature as [`../task-list-mvvm`](../task-list-mvvm), organized
Clean-Architecture-first: directories named by Clean's vocabulary, an explicit
request/response pair around the use case, and a standalone imperative
Presenter as the output-port implementation. It's a worked example of how you
might *lay out* an ordo app for readers who want Clean's vocabulary on disk.
This is one way to organize an ordo app; `task-list-mvvm` is another.

## Layout

```
entities/
  task.h                 -- the Task entity
  task_added.h           -- TaskAdded fact event (1:N broadcast)
  task_list.h            -- TaskList Agent: owns the tasks, applies the add rule
use_cases/
  add_task_request.h     -- AddTaskRequest: request model == intent event
  add_task_response.h    -- AddTaskResponse: response model for the port
  task_output.h          -- TaskOutput: the output port interface
  add_task_command.h     -- AddTaskCommand: the use-case interactor
interface_adapters/
  task_list_presenter.h / .cpp  -- TaskListPresenter: imperative port implementation
main.cpp                 -- bootstrap, wiring, --smoke
```

## Mapping table

Every piece, back to the ordo role it actually is:

| Clean Architecture  | this example                               | ordo role / notes |
|---------------------|--------------------------------------------|-------------------|
| Entity              | `entities/task.h` + `entities/task_list.h` | `Agent` -- never sees a port, stays send-only |
| Input port          | *(no file)*                                | the request event **type**; `registerCommand` is the binding -- the sender never learns a handler exists |
| Request model       | `use_cases/add_task_request.h`             | the same struct as the intent event -- see "One struct, two names" below |
| Use case interactor | `use_cases/add_task_command.h`             | `Command<AddTaskRequest>` -- transient, stateless, policy lives here |
| Output port         | `use_cases/task_output.h`                  | abstract interface the Command receives at registration |
| Response model      | `use_cases/add_task_response.h`            | plain struct carried through the port |
| Presenter           | `interface_adapters/task_list_presenter.h` | `ordo::qt::Presenter` subclass implementing the port |
| Controller          | `TaskListPresenter::addTask` + the signal connections in `main.cpp` | a `send()` call site, not a class |
| Dependency rule     | --                                         | satisfied: ports and models live use-case-side, adapters implement them |

## One struct, two names

Clean Architecture calls for a request model at the use case's boundary. ordo
calls for a typed intent event at the dispatcher's boundary. Here those turn
out to be the same struct: `AddTaskRequest` has one field (`title`) and an
`eventName`, and both roles fall out of it -- `context().send(request)` *is*
the input port, and `Kernel::registerCommand` is the binding that turns a
dispatched `AddTaskRequest` into a call to `AddTaskCommand::execute()`. No
separate input-port interface exists, or needs to: the typed event already
gives the use case a stable, checked boundary. Wrapping it in a second,
textually different "request" struct would add ceremony without adding a
boundary that isn't already there, so this example ships one struct and
documents the equivalence instead of wrapping a POD in a POD.

## Two result channels

`TaskOutput` carries outcomes that matter only to whoever asked: rejection
before any mutation, and an acceptance echo after one. Both are 1:1 --
delivered to the caller specifically, through the port. `TaskAdded` is a
different kind of result: a fact any interested view may want to observe,
independent of who triggered it, so it stays a 1:N broadcast through the
dispatcher instead of being folded into the port. A port is a complement to
the event bus here, never a replacement for it.

## Contrast with the MVVM twin

The domain loop is identical, file for file: `entities/task_list.h` here is
`model/task_list.h` there; `use_cases/add_task_command.h` here is
`model/task_commands.h` there, down to the trimming rule and the two
rejection messages. Only the port's channel names differ cosmetically -- `presentRejected`
/ `presentAdded` here, `taskRejected` / `taskAccepted` there.

What actually differs is the adapter layer. `task-list-mvvm`'s
`TaskListViewModel` is a `ViewModel`: it exposes `Q_PROPERTY`s and signals,
and the view binds to them declaratively -- the view-model never touches a
widget. This example's `TaskListPresenter` is a `Presenter`: it holds raw
widget pointers and writes them directly, in `presentRejected`,
`presentAdded`, and the `TaskAdded` handler alike -- imperative, the way a
classic MVP presenter works. Same domain, same kernel, two legitimate ways to
close the loop to a Qt view; ordo ships both roles because neither is "more
correct" than the other.

## The lifetime rule

`Kernel::registerCommand` stores the output port reference for as long as
the registration lives -- the object it points at must outlive it. In
`main.cpp`, the presenter lives inside the `ViewHost`, which is declared
*after* `window` and its widgets so that, on the way out, it's destroyed
*before* them (reverse construction order -- see the comment at the top of
`main.cpp`). But the command registration on `kernel` would otherwise outlive
the `ViewHost` entirely, so `main` calls
`kernel.removeCommand<AddTaskRequest>()` right before returning: drop the
registration first, rather than relying on nothing dispatching after that
point.

## Build & run

```
cmake -S examples/task-list-clean -B build-clean -G Ninja -DCMAKE_PREFIX_PATH=<Qt6>
cmake --build build-clean
./build-clean/task-list-clean            # the app
./build-clean/task-list-clean --smoke    # headless self-check, exit 0
```
