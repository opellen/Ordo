---
name: ordo-use
description: Build an application on the ordo C++20 framework — domain naming, bootstrap/teardown order, threading, and multi-kernel conventions, with code patterns for every role and a map of the headers to check signatures against.
---

# Using Ordo

This skill is for an agent **building an application on top of ordo** —
writing events, agents, commands, and view adapters for your own domain. It
is not for changing ordo itself (`ordo::core`, `ordo::qt`, or the build).

Read this file, then [conventions.md](conventions.md) before writing any
code, and [post-verify.md](post-verify.md) after implementing a feature and
before calling it done.

The skill is self-contained: it needs nothing from the ordo tree except the
headers your build already compiles against. A vendored copy of ordo that
carries only `core/`, `qt/` and the build files is enough, and so is a copy
of this directory installed elsewhere.

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
| Code for each role, wiring, bootstrap, thread relay, `--smoke`, unit test | [patterns.md](patterns.md) |
| Exact signatures — which header declares what | [patterns.md, header table](patterns.md#code-patterns) |
| Checklist to run after implementing a feature | [post-verify.md](post-verify.md) |
| Paste blocks for `AGENTS.md` (Codex/Cursor) or the Claude Code skill copy target | [integration/AGENTS-snippet.md](integration/AGENTS-snippet.md) |

The longer tutorial (`usage-guide.md`), the type-by-type API reference
(`api.md`) and the worked examples live in the full ordo repository
(`github.com/opellen/Ordo`, under `docs/` and `examples/`). They are optional
further reading; nothing in this skill requires them.

---

## The four roles, in one line each

- **Event** — a plain struct; the type itself is the identity. `*Requested`
  is an intent, `*Added`/`*Changed`/`*Removed` is a fact. See
  [patterns.md, Events](patterns.md#events).
- **Agent** (`ordo::core::Agent`) — named owner of one slice of domain state;
  mutates, then publishes a fact via `AgentContext`. Never subscribes. See
  [patterns.md, Agent](patterns.md#agent).
- **Command** (`ordo::core::Command<EventT>`) — one intent type, one command
  type, bound with `kernel.registerCommand<EventT, CommandT>(...)`; business
  policy (validation, rejection) lives here, not in the agent. See
  [patterns.md, Command](patterns.md#command).
- **View adapter** (`ordo::qt::Presenter` or `ordo::qt::ViewModel`) — reacts
  to facts, sends intents; owned and context-injected by a `ViewHost`. See
  [patterns.md, ViewModel shape](patterns.md#view-adapter--viewmodel-shape).

---

## When you're unsure

- Don't invent a new lifecycle hook or startup mechanism — there isn't one;
  see [conventions.md](conventions.md)'s bootstrap section.
- Don't guess a class or method name — every ordo name used in this skill
  exists in exactly the shape shown; cross-check an unfamiliar one against the
  headers listed in [patterns.md](patterns.md#code-patterns) before using it.
- If this skill and the headers ever disagree, the headers win — they are what
  your build compiles.
