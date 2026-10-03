# galaxy-farm

A pool of worker threads builds procedural star fields through a plain C-API
DLL, and a `QOpenGLWidget` viewport shows the destination sector filling in
galaxy by galaxy while the toolbar, the orbit camera, and the worker panel
all stay fully responsive -- including mid-jump, and including while a
heavy jump is still generating when a second one supersedes it. Built from
ordo's ordinary vocabulary -- one `Agent`, one `Command` per intent, one
imperative `Presenter` -- this is a worked example of an "infinite universe"
loop over mesh-farm's single-batch shape, not a claim that ordo ships some
new threading or space-partitioning feature.

> Jump START commissions the destination sector's generation. The
> destination visibly builds through the jump tunnel while the transit
> animation plays. Re-jumping mid-transit is a legal, designed action -- the
> stale path this example exists to prove.

Code running inside a `QRunnable` may touch exactly two things: the
galaxylib C API and `GenRelay`'s `post*` methods. There is not one lock
anywhere in this example -- that absence is the design, not an oversight.

## Layout

```
galaxylib/
  galaxylib.h                -- the C ABI: GalaxylibCloud, GalaxylibProgressFn, galaxylib_generate_galaxy, galaxylib_free_cloud
  galaxylib.cpp               -- the domain team's "black box": deterministic per-star placement + prefix-complete progress
domain/
  cloud_buffers.h             -- CloudBuffers: the baked-galaxy payload (galaxylib ABI v3 shape)
  sector_coord.h              -- SectorCoord: the universe's 2D integer addressing
  jump_events.h               -- JumpRequested / JumpArrivalReached: the view's two intents
  galaxy_job_events.h         -- GalaxyJobStarted / GalaxyJobProgress / GalaxyJobFinished: the relay's re-entry trio
  star_map_changed.h          -- StarMapChanged fact + its vocabulary (GalaxyState, Board, JumpPhase, GalaxyStatus, WorkerInfo)
  star_map.h / .cpp           -- StarMap Agent: owns TWO boards (current_/destination_) + workers, epochs, the stale-arrival guard
  jump_commands.h / .cpp      -- JumpCommand + ArrivalCommand: the two commands a user/view action sends
  galaxy_job_commands.h / .cpp -- GalaxyJobStartedCommand / GalaxyJobProgressCommand / GalaxyJobFinishedCommand
infra/
  job_runner.h / .cpp         -- GenRelay (the marshalling seam) + JobRunner (the QThreadPool wrapper + progress thunk)
view/
  galaxy_gl_widget.h / .cpp   -- the orbit-camera renderer: point-cloud shader, jump streak tunnel, deferred GL upload/release in paintGL
  jump_presenter.h / .cpp     -- JumpPresenter: worker lanes, the status row, the progressive cloud pull, the ~4s transit clock
  farm_window.h / .cpp        -- FarmWindow: the toolbar/worker-panel widgets, two-phase wiring to the presenter
design/
  ordo-galaxy-farm.png        -- the mockup this example was built from (dark theme, jump tunnel, worker lanes)
  concept-notes.md            -- what the mockup pins: layout, defaults, sector addressing, status vocabulary
main.cpp                      -- bootstrap, wiring, --smoke / --render-probe / --gui-probe
CMakeLists.txt                -- builds galaxylib SHARED, a console harness, plus the galaxy-farm executable
```

## The loop, end to end

1. A toolbar click on Jump (or the one jump fired before the window is
   shown) sends `JumpRequested{universeSeed, galaxiesPerSector,
   starsPerGalaxy}`.
2. `JumpCommand::execute` clamps every dial -- `galaxiesPerSector` to `[1,
   128]`, `starsPerGalaxy` to `[1, 1000000]` (galaxylib's own hard cap) --
   the same shape as mesh-farm's `RegenerateCommand`. Policy lives here, in
   the command, not in the agent or the runner.
3. `StarMap::beginJump` bumps `epoch_`, drops the previous destination_
   wholesale (`destination_.galaxies.clear()`), derives the destination
   sector as a deterministic step from `(universeSeed, epoch_)` off the
   *current* sector -- never `(0,0)`-stuck, and always a genuine
   neighbor -- chains every galaxy's `(seed, type)` from that sector's own
   seed, and returns this destination's jobs. `JobRunner::enqueue` stamps
   every job with the new epoch and submits one `QRunnable` per job to its
   own `QThreadPool`.
4. Once a `QRunnable` actually reaches a worker thread: a stale-START check
   against the runner's own live epoch -> `galaxylib_generate_galaxy`,
   which calls back into the runnable's own `progressThunk` at every
   prefix-complete checkpoint along the way -> copy the DLL's buffers (a
   checkpoint's already-final prefix, or the finished cloud once the call
   returns) into an owned `CloudBuffers` -> `galaxylib_free_cloud` once
   generation is done.
5. `GenRelay::postStarted` / `postProgress` (one call per checkpoint) /
   `postFinished` marshal every one of those back with
   `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` -- the one and
   only crossing from a worker thread to the UI thread.
6. Back on the UI thread, the queued lambda calls
   `onStarted`/`onProgress`/`onFinished`, which `kernel.send()`s
   `GalaxyJobStarted`/`GalaxyJobProgress`/`GalaxyJobFinished` into the
   dispatcher.
7. `GalaxyJobStartedCommand`/`GalaxyJobProgressCommand`/
   `GalaxyJobFinishedCommand` call
   `StarMap::markStarted`/`markProgress`/`storeGenerated`, each
   epoch-checked against the star map's current jump (`markProgress` alone
   drops a stale epoch silently -- see "Epochs and stale sectors" below).
8. Every mutation ends in `publishSnapshot()`, broadcasting one
   `StarMapChanged` fact carrying BOTH boards (current and destination,
   each entry tagged `Board::Current`/`Board::Destination`), the worker
   list, and counts scoped to whichever board is "of interest" --
   destination while Jumping, current once Idle.
9. `JumpPresenter::onStarMapChanged` updates the status row and worker
   lanes, and -- for every galaxy on the active board whose on-screen
   preview is behind what the star map can now show, Generating or Ready
   alike -- pulls its latest `CloudBuffers` straight off the live agent and
   hands it to the canvas.
10. `GalaxyGlWidget::setGalaxyCloud` copies that data into a
    pending-upload queue, replacing any GPU buffers already uploaded for
    that galaxy; `paintGL` drains it (and any deferred GL releases) once
    per frame -- the one place this widget's GL context is guaranteed
    current. Meanwhile the jump streak tunnel and central void render
    around the destination's slots, driven by the presenter's own transit
    clock (see below), until arrival snaps the visual back to Idle.

## Epochs and stale sectors

Every `JumpRequested` bumps `StarMap::epoch_` and wipes `destination_` in
the same call -- the previous destination's entries and clouds leave
together, not one at a time, which keeps the destination board's `total`
scoped to "this jump's galaxy count" instead of growing across every
re-jump. Two guards, each owning a different failure window, keep late
results from resurrecting a superseded destination:

- **Stale-START**, inside the `QRunnable` itself: by the time a submitted
  job actually reaches a worker thread, a newer jump may already have
  superseded it. The check compares the job's stamped epoch against
  `JobRunner`'s own live epoch and, if it lost the race, returns before
  calling `galaxylib_generate_galaxy` or the relay at all -- skipped
  silently, because nobody is owed a notification for a galaxy that's
  already off the board. (`StarMap::markStarted` re-checks the same epoch
  a second time, belt-and-suspenders, for the narrow window where a start
  and a `beginJump` interleave right at the boundary -- it owes no fact
  either.)
- **Stale-ARRIVAL**, inside `StarMap::storeGenerated`: a job that passed
  the stale-start check can still finish after the star map has moved on
  again. This one can't stay silent -- the worker's "no longer busy" state
  still needs recording -- so it's counted in `staleArrivals` and shown as
  "stale N" in the status row.

Neither guard can do the other's job, for the same reason mesh-farm's
twin pair can't: the runnable's check saves a worker thread from a wasted
generation, which the star map alone can't prevent once a job is already
running; the star map's check is the backstop for whatever slips past it,
which the runnable alone can't provide since it has no view of the star
map's current boards.

One asymmetry worth calling out on its own: `markProgress`'s stale-epoch
branch is dropped SILENTLY, unlike `storeGenerated`'s stale-arrival branch,
which still counts into `staleArrivals`. `staleArrivals` means "completed
work discarded," and a superseded generation's in-flight checkpoints are
not completed work -- they are progress nobody asked to see anymore, on a
galaxy that has already left the board. Counting every checkpoint of every
superseded generation still running to completion would inflate that
counter into something that no longer means what its name says -- the same
rationale mesh-farm's `PartShelf::markProgress` documents for its own AO
checkpoints.

One thing arrival deliberately does NOT do: `StarMap::arrive()` promotes
`destination_` to `current_` wholesale but does **not** bump `epoch_`. A
galaxy still generating for that sector keeps streaming into what is now
the *current* board, and its eventual `GalaxyJobFinished` is not stale --
that continuity is sector streaming, working as designed. Only a fresh
`JumpRequested` -- a new epoch -- invalidates a destination's in-flight
results.

The `--smoke` check exercises exactly this: jump A streams to completion
and arrives, jump B (a heavy six-galaxy jump) is superseded mid-flight by
jump C, and the run asserts `staleArrivals >= 1` and an exact
`sumCompleted` across every worker -- the two guards' behavior pinned down
as a test, not just a comment.

## The transit clock lives in the view

`StarMap` is deliberately timer-free -- nothing in `domain/` reads a clock,
and `JumpArrivalReached` carries no payload, because the domain has no way
to measure "the animation is over," only to be told. The ~4s jump transit
instead lives in `JumpPresenter`: a `QTimer` ticking every ~33ms drives
`GalaxyGlWidget`'s streak-tunnel visual and a `QElapsedTimer` that resets
to zero on every new epoch (including a re-jump fired mid-transit, which
legitimately restarts the animation rather than queuing behind the one
already playing). The instant the elapsed time crosses `kTransitMs`, the
presenter sends `JumpArrivalReached` itself.

This is the one mechanism mesh-farm's `ShelfPresenter` never needed, and
it is why `--smoke` sends `JumpArrivalReached` directly after jump A
reaches `ready == galaxiesPerSector`, with no view, no timer, and no
presenter involved at all: the domain loop's own arrival handling
(`StarMap::arrive()`, the board promotion, the "no epoch bump" rule above)
proves out completely headless.

## The DLL slot is where your solver goes

`galaxylib.h` is the entire contract for what a domain team's black-box
solver looks like from the app's side: deterministic given `(seed, type,
starCount)` -- every random choice threads through a `splitmix64` chain
rooted at the seed, so calling `progress` (or not) never changes the
answer -- thread-safe purely by holding no mutable state of its own (any
number of threads may call `galaxylib_generate_galaxy` concurrently, each
with its own `out`, no locking), and DLL-allocated -- `out`'s buffers are
malloc'd inside galaxylib, so the caller passes them to
`galaxylib_free_cloud` instead of `free()`/`delete` directly, because
allocation crosses a module boundary and the allocator that handed out the
memory has to be the one that reclaims it. Nothing above `job_runner.h`
assumes anything about galaxylib's internals, only its ABI -- swap the
`.cpp` for a real vendor solver and nothing else would need to change.

Progress here differs from mesh-farm's worklib in one deliberate way:
`worklib_bake_part`'s AO sweep reports a mix of finished-and-still-`-1.0`
values (a sentinel worth checking for), while galaxylib's snapshots are
**prefix-complete** -- entries `[0, snapshot->starCount)` are always FINAL,
byte-identical to the same prefix of the eventual `out`, and there is no
sentinel value anywhere in a `GalaxylibCloud`. A point cloud has no
topology phase the way a mesh does, so there's no `fraction == 0.0` call
either: the first checkpoint already reports a positive prefix.

`starsPerGalaxy` is this example's one weight knob: galaxy generation
places each star independently through its own `starSeed(seed, index)` PRNG
draw, with no shared acceleration structure across stars, and it stays that
way on purpose -- the point is CPU work that scales linearly and visibly
with the dial, not a demonstration of how fast a real solver could go.

## Rendering

The visuals went through a full upgrade pass (goal
`galaxy-farm-rendering-upgrade`, 2026-09-02/03) and are documented as a
living reference in
[`docs/examples/galaxy-farm/algorithms.md`](../../docs/examples/galaxy-farm/algorithms.md)
-- galaxy generation and per-frame orbit evaluation ported faithfully from
beltoforion's Galaxy-Renderer (BSD-2, attribution kept), an all-additive
density-wave model with live differential rotation, a quarter-res bloom
chain, a once-per-sector baked sky cubemap (space-3d port, MIT), and the
jump journey: travelling sector boards on a frozen axis, a curved flight
path, an elliptical-taper streak field, and a spacetime lens that parts
the beams around exactly the screen area where the destination sector
lands. That file is the single source of truth for every rendering
algorithm here; this README stays about the ordo loop.

## Dark theme and dev probes

The whole window runs Fusion + a near-black `QPalette` (main.cpp, before
`FarmWindow` is constructed) -- design/concept-notes.md's pinned "whole
window is DARK-THEMED" decision, unlike mesh-farm's default light Qt look.

Two headless dev modes sit alongside `--smoke`: `--render-probe` drives
`GalaxyGlWidget` directly (no domain/StarMap/JobRunner involved) and writes
three PNGs to the current working directory; `--gui-probe` builds the real
window+presenter path with small, fast dials and grabs two whole-window
screenshots on timers. `--gui-probe`'s screenshots land under `C:/tmp/`
rather than anywhere under Documents: Windows Controlled Folder Access
blocks this unrecognized exe from creating files there, so an unprotected
path is required for the probe to actually write anything.

## Rejected alternatives

| Alternative | Why not |
|---|---|
| A cancellable generation callback (abort at the next checkpoint once superseded) | Would save real CPU time on a superseded jump, but it would also remove the one thing `--smoke` relies on to exercise `StarMap::storeGenerated`'s stale-arrival guard deterministically: jump B's heavy bake has to actually finish, late, for its `GalaxyJobFinished` to prove the guard drops it -- same reasoning mesh-farm documents for its own rejected cancellable bake. |
| Carry `CloudBuffers` inside the `StarMapChanged` fact | Facts stay light on purpose -- big data lives on the agent, a view pulls only what it needs. |
| A domain-owned transit timer | The domain has no way to measure "the animation is over," only to be told (`JumpArrivalReached`) -- owning a clock is a view concern, not a state-owning one. |
| One board instead of two (mesh-farm's single-batch shape) | An "infinite universe" needs the camera's current sector to keep existing while a destination builds ahead of it -- a single board can't represent "here" and "where I'm going" at once. |

## Build & run

```
cmake -S examples/galaxy-farm -B build-galaxy-farm -G Ninja -DCMAKE_PREFIX_PATH=<Qt6>
cmake --build build-galaxy-farm
./build-galaxy-farm/galaxy-farm                 # the app
./build-galaxy-farm/galaxy-farm --smoke         # headless self-check, exit 0
./build-galaxy-farm/galaxy-farm --render-probe  # widget-only PNG proof (run from build-galaxy-farm)
./build-galaxy-farm/galaxy-farm --gui-probe     # full window PNG proof, writes to C:/tmp/galaxy-farm-probe
```

galaxylib's DLL builds into the same output directory as the executable, so
no install step is needed; Qt's and MinGW's runtime DLLs still need to be
on `PATH` to run any of these.
