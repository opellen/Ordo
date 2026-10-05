# AGENTS.md Integration

Ordo ships this skill as a plain doc tree inside its own repo — there is
nothing to install and no separate package to version-skew against your
`ordo::core`/`ordo::qt`. Point your AI tool's config at the copy that came in
with your build.

---

## Codex / Cursor (and any other `AGENTS.md`-reading tool)

These tools read `AGENTS.md` from anywhere it points — no fixed folder, no
copy needed (zero-copy pointing). Add one of the following sections to your
project's `AGENTS.md`, matching how you bring in ordo:

### FetchContent consumers

If your `CMakeLists.txt` uses `FetchContent_Declare(ordo ...)` (see the
root README's FetchContent section), the source lands under your build
directory at `_deps/ordo-src`:

```markdown
## Ordo Guidelines
When working on Ordo code, read and follow:
build/_deps/ordo-src/docs/skills/ordo-use/SKILL.md
```

Adjust `build/` if you configure into a differently named directory.

### add_subdirectory consumers

If you vendor ordo as a sibling checkout via `add_subdirectory("${ORDO_DIR}"
...)`, point at that checkout directly:

```markdown
## Ordo Guidelines
When working on Ordo code, read and follow:
<path-to-ordo>/docs/skills/ordo-use/SKILL.md
```

Replace `<path-to-ordo>` with the actual relative or absolute path to your
ordo checkout (e.g. `third_party/ordo` or `../ordo`).

### Installed-package consumers

`find_package(ordo CONFIG)` installs the libraries and headers, not the docs
tree — there is no source checkout to point at. Take the skill from the ordo
repo directly (clone it, or browse it on the project's remote) and vendor or
link the same three files from there instead.

---

## Claude Code

Claude Code's skill loader needs `.claude/skills/<name>/` inside your own
project root — it does not read arbitrary `AGENTS.md` paths. Ordo provides an
opt-in CMake target that copies this skill tree into that location for you:

```bash
cmake --build build --target ordo-skill-claude
```

This copies `docs/skills/ordo-use/` (from the vendored ordo source) to
`.claude/skills/ordo-use/` at your project root, plus a small
`ordo-use-skill-version.md` version note. It is:

- **Opt-in only** — never part of `ALL`, never runs unless you invoke it.
- **Silent on your own files** — it never edits `AGENTS.md` or `CLAUDE.md`;
  any guidance it has to give prints to the console for you to paste by hand.
- **Copy semantics** — re-run the target after upgrading your ordo checkout
  to refresh the copy; it does not stay in sync automatically.

If you use both Claude Code and an `AGENTS.md`-reading tool, you can do both:
run the target for Claude Code's native skill loader, and add an `AGENTS.md`
pointer (above) for the other tool — no conflict between the two.
