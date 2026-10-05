# Post-Implementation Verification

Run this checklist after implementing a feature, before calling it done. Each
item is something to *look for in the code*, not something to take on faith.
On a scaffold-only change (no agents, commands, or adapters exist yet), items
2-5 are legitimately N/A and item 1 may be a placeholder `--smoke` that just
exits 0 — the real assertions arrive with the first domain code. The same
N/A reading covers a probe/harness-only change that adds no new agent,
command, or adapter wiring.

---

## 1. A `--smoke` headless self-check exists and passes

Every worked example ships a `--smoke` flag that drives the feature's whole
loop with no window shown and exits non-zero on failure — see
[`examples/todomvc/smoke.cpp`](../../../examples/todomvc/smoke.cpp)'s
`runSmoke()`/`runScenario()` for the pattern: construct the kernel, send
intents the same way a button press would, assert on what the agent (or a
view-model reading it) ended up holding, `fprintf(stderr, "FAIL: ...")` and
return 1 on mismatch, `printf("PASS: ...")` and return 0 otherwise.

- [ ] Your app (or the feature you added) has an equivalent `--smoke` path.
- [ ] It asserts on **observable state** (agent data, view-model properties),
      not on log output.
- [ ] It runs with no `QApplication` window shown — a `QCoreApplication` or
      `QGuiApplication` is enough unless real widgets/QML are under test (see
      [usage-guide.md, Testing Your Feature](../../usage-guide.md#testing-your-feature)).
      A smoke that grows phases incrementally may keep early phases
      application-free and construct a phase-local `QApplication` only inside
      the first widget-touching phase — declare it first in that phase so it
      outlives every widget local.
- [ ] It actually passes: run it and check the exit code.

## 2. Teardown matches the declaration-order rule

- [ ] Locals are declared in **reverse teardown order** — whatever must die
      last is declared first. Check this by reading top-to-bottom: does
      `kernel` (or `host`) appear before the objects that reference into it?
- [ ] A `Presenter`'s widgets are declared **before** the `ViewHost` that owns
      the presenter (so the adapter is destroyed while the widget it writes to
      still exists). A `ViewModel`'s window/QML engine is declared **after**
      the host (so the window stops reading before the view-model dies).
- [ ] Any relay/worker object (a `DbWorker`-shaped class) is declared **after**
      the kernel/relay it calls back into, so its destructor can drain
      in-flight work while the callback target still exists.

## 3. Every `registerCommand` has a matching `removeCommand` before the kernel goes out of scope

- [ ] For each `kernel.registerCommand<EventT, CommandT>(...)` in bootstrap,
      there is a `kernel.removeCommand<EventT>()` in the teardown block —
      match them one for one, the way
      [`examples/todomvc/wiring.cpp`](../../../examples/todomvc/wiring.cpp) pairs
      every `registerCommand` with a `removeCommand`.
- [ ] This matters most for commands registered with a `std::ref(...)`
      dependency: command factory captures have **no** auto-cleanup (unlike
      subscriptions), so a missing `removeCommand` before the referent's
      destructor runs is a dangling reference the next dispatch will use.
- [ ] The removes happen **before** the referenced objects' destructors run,
      not after.

## 4. Adapters subscribe only through `ViewAdapter::subscribe`

- [ ] Every `Presenter`/`ViewModel` subscription goes through the protected
      `subscribe<EventT>(...)` inherited from
      [`ViewAdapter`](../../../qt/include/ordo/qt/view_adapter.h) — either the
      member-function overload (`subscribe<events::X>(&Self::onX)`) or the
      `std::function` overload. Never a hand-rolled call into
      `PresenterContext::subscribe` or `Dispatcher::subscribe` with a
      different owner cookie.
- [ ] No manual `unsubscribe` bookkeeping was added — `~ViewAdapter` already
      unsubscribes everything the adapter subscribed, by owner cookie, on
      destruction. Adding your own teardown here is redundant and a sign
      something is subscribing outside `ViewAdapter::subscribe`.
- [ ] `subscribe` calls happen inside `onRegister()`, never in a constructor
      (`context()` is unset until `ViewHost::add()` injects it).

## 5. Command dependencies are passed via `std::ref`

- [ ] Any command constructor taking a reference dependency (a worker, an
      output port) is registered as
      `kernel.registerCommand<EventT, CommandT>(std::ref(dependency))` — see
      [`examples/todomvc/wiring.cpp`](../../../examples/todomvc/wiring.cpp)'s
      `std::ref(dbWorker)` calls — never by value (which would copy or fail to
      compile) and never a raw pointer smuggled through a value parameter.
- [ ] The referent (`dbWorker`, a view-model used as an output port) is
      declared with a lifetime that **outlives** the registration — confirmed
      by item 3's removeCommand check.

## 6. Build and smoke are green

- [ ] The app builds clean.
- [ ] `--smoke` (or your project's equivalent) exits 0.
- [ ] Any repo-level unit tests that exercise the changed agents/commands
      pass — a test builds its own `Kernel`, registers only the agents it
      needs, sends an intent, asserts on the agent and on any output port
      recording (see
      [usage-guide.md, Testing Your Feature](../../usage-guide.md#testing-your-feature)).
