---
name: ordo-use
description: Build an application on the ordo C++20 framework — domain naming, bootstrap/teardown order, threading, and multi-kernel conventions, with pointers into the core docs for anything deeper.
---

# Using Ordo

This skill is for an agent **building an application on top of ordo** —
writing events, agents, commands, and view adapters for your own domain. It
is not for changing ordo itself (`ordo::core`, `ordo::qt`, or the build).

Read this file, then [conventions.md](conventions.md) before writing any
code, and [post-verify.md](post-verify.md) after implementing a feature and
before calling it done.

---

## Mental model

An ordo app is a set of **typed C++ structs (events)** flowing through one
**`Kernel`**, which is the composition seam, not a god object: it owns a
synchronous dispatcher, named **agents** (domain state, send-only), and
per-event-type **commands** (transient, policy-holding, constructed fresh on
each dispatch). A view **adapter** (`Presenter` or `ViewModel`) subscribes to
facts the agents publish and turns UI input into intents it sends back
through the kernel. Nothing here is a singleton and nothing polls — one
`send` runs its whole reaction chain synchronously before returning.

---

## Router

| Need | Go to |
|:---|:---|
| Domain file naming, directory layout, bootstrap/teardown idiom, threading, multi-kernel rules | [conventions.md](conventions.md) |
| Checklist to run after implementing a feature | [post-verify.md](post-verify.md) |
| Paste blocks for `AGENTS.md` (Codex/Cursor) or the Claude Code skill copy target | [integration/AGENTS-snippet.md](integration/AGENTS-snippet.md) |
| Step-by-step tutorial (events → command → agent → presenter → bootstrap) | [usage-guide.md](../../usage-guide.md) |
| Type-by-type API reference (signatures, lifetime rules, error surface) | [api.md](../../api.md) |
| Which example teaches which concept, and the recommended reading order | [examples.md](../../examples.md) |

---

## The four roles, in one line each

- **Event** — a plain struct; the type itself is the identity. `*Requested`
  is an intent, `*Added`/`*Changed`/`*Removed` is a fact. See
  [usage-guide.md, Step 1](../../usage-guide.md#step-1-define-typed-struct-events).
- **Agent** (`ordo::core::Agent`) — named owner of one slice of domain state;
  mutates, then publishes a fact via `AgentContext`. Never subscribes. See
  [api.md, Agent](../../api.md#agent).
- **Command** (`ordo::core::Command<EventT>`) — one intent type, one command
  type, bound with `kernel.registerCommand<EventT, CommandT>(...)`; business
  policy (validation, rejection) lives here, not in the agent. See the
  `Command<EventT>` entry in [api.md](../../api.md), under `ordo::core`.
- **View adapter** (`ordo::qt::Presenter` or `ordo::qt::ViewModel`) — reacts
  to facts, sends intents; owned and context-injected by a `ViewHost`. See
  [api.md, ViewHost](../../api.md#viewhost).

---

## When you're unsure

- Don't invent a new lifecycle hook or startup mechanism — there isn't one;
  see [conventions.md](conventions.md)'s bootstrap section.
- Don't guess a class or method name — every one used in this skill exists in
  the Ordo codebase in exactly the shape shown; cross-check against
  [api.md](../../api.md) or the [`examples/todomvc/`](../../../examples/todomvc/)
  source before using an unfamiliar one.
- If a convention here and the official docs ever disagree, the official docs
  (`usage-guide.md`, `api.md`) win — this skill is a compression of them, not
  a replacement.
