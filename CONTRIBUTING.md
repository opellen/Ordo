# Contributing

Issues are welcome — bugs, questions, ideas, disagreements. A few lines of code
that reproduce a problem are the most useful thing you can send.

Pull requests are not accepted. Ordo is maintained by one person in the time
left over from a day job, and reviewing external code costs more than writing
it. Report the bug, describe the change you have in mind, or attach a patch to
the issue; it is read like any other report, and the change lands from the
maintainer's tree. SQLite works the same way.

## Reporting a bug

Include:

1. The Ordo version — the release tag or commit you built, or
   `ordo::core::versionString()`.
2. How you consume it: `add_subdirectory`, `FetchContent`, or the installed
   package.
3. Your compiler, OS and Qt kit (for example GCC 13 MinGW, Qt 6.11 on
   Windows 11). Say if you build the core alone (`-DORDO_BUILD_QT=OFF`).
4. The smallest program that shows the problem — the events, the agent or
   command involved, and the `send` that goes wrong — or, if it shows up in
   one of the examples, which one and the steps you took.
5. What you expected and what happened instead.

For a problem in the framework build, paste the failing lines of
`ctest --test-dir build --output-on-failure`. For an example, run it with
`--smoke` and paste its last lines; the smoke run stops at the first failing
check and names it.

## Ideas and questions

Open an issue. Say what you were trying to do, not only what you want added.
Ordo has one model — typed-struct events on one dispatcher per kernel, and
roles (Agent, Command, Presenter, ViewModel) that each get a capability context
limited to what the role may do. A request that fits that model is easier to
take than one that needs a second one.

Ordo is pre-1.0: minor versions may still change the API, so a proposal that
breaks existing code is fine to raise. Say what it breaks.

## Licensing

Ordo is licensed under the MIT License ([`LICENSE`](LICENSE)). No external code
is merged, so there is no contributor license agreement.

## Development

The build needs CMake 3.24 or newer and a C++20 compiler. `ordo_qt` and the
examples also need Qt 6.5 or newer. See the README's *Build / Install /
Consume* section for the consumption modes and the target names.

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=<Qt6 prefix>
cmake --build build
ctest --test-dir build --output-on-failure
```

`-DORDO_BUILD_QT=OFF` builds and tests the core alone, without Qt.

Each example under `examples/` is its own CMake project that pulls Ordo in
with `add_subdirectory`. After a change to `core/` or `qt/`, build the examples
it touches and run them with `--smoke`:

```bash
cmake -S examples/todomvc -B build-todomvc -G Ninja -DCMAKE_PREFIX_PATH=<Qt6 prefix>
cmake --build build-todomvc
./build-todomvc/todomvc --smoke
```
