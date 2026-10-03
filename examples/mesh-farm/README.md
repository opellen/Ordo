# mesh-farm

A pool of worker threads bakes procedural 3D parts through a plain C-API DLL,
each part's geometry and status marshal back to the UI thread one message at
a time, and a `QOpenGLWidget` shelf assembles the results live while the
toolbar, the orbit camera, and the worker panel all stay fully responsive the
entire time -- including while a heavy batch is still baking. Built from
ordo's ordinary vocabulary -- one `Agent`, one `Command` per intent, one
imperative `Presenter` -- this is a worked example of thread discipline
around a GPU-backed view and a thread pool, not a claim that ordo ships some
new threading feature.

> The GL context and the ordo dispatcher are both owned by the UI thread.
> Worker threads only ever compute; every result crosses back through one
> marshalling seam.

Code running inside a `QRunnable` may touch exactly two things: the worklib C
API and `BakeRelay`'s `post*` methods. There is not one lock anywhere in this
example -- that absence is the design, not an oversight.

## Layout

```
worklib/
  worklib.h                 -- the C ABI: WorklibMesh, WorklibProgressFn, worklib_bake_part, worklib_free_mesh
  worklib.cpp               -- the domain team's "black box": part generation, smoothing, AO bake + checkpoints
domain/
  mesh_buffers.h            -- MeshBuffers: the baked-part payload both events and the shelf share
  regenerate_requested.h    -- RegenerateRequested: the one user intent
  bake_job_events.h         -- BakeJobStarted / BakeJobProgress / BakeJobFinished: the relay's re-entry trio
  shelf_changed.h           -- ShelfChanged fact + its vocabulary (PartState, PartStatus, WorkerInfo)
  part_shelf.h / .cpp       -- PartShelf Agent: owns parts + workers, generations, the stale-arrival guard, progress
  regenerate_command.h / .cpp -- RegenerateCommand: clamps the dials, records the batch, submits the work
  bake_job_commands.h / .cpp  -- BakeJobStartedCommand / BakeJobProgressCommand / BakeJobFinishedCommand
infra/
  job_runner.h / .cpp       -- BakeRelay (the marshalling seam) + JobRunner (the QThreadPool wrapper + progress thunk)
view/
  shelf_gl_widget.h / .cpp  -- the orbit-camera renderer: shaders (incl. the ghost-gray sentinel branch), deferred GL upload/release in paintGL
  shelf_presenter.h / .cpp  -- ShelfPresenter: worker lanes, the status row, the mesh pull (now progressive)
  farm_window.h / .cpp      -- FarmWindow: the toolbar/worker-panel widgets, two-phase wiring to the presenter
main.cpp                    -- bootstrap, wiring, --smoke
CMakeLists.txt              -- builds worklib SHARED plus the mesh-farm executable
```

## The loop, end to end

1. A toolbar click (or the one batch fired before the window is shown) sends
   `RegenerateRequested{seed, partCount, smoothIters, aoRaysPerVertex}`.
2. `RegenerateCommand::execute` clamps every dial -- `partCount` to `[1,
   64]`, `smoothIters` to `[0, 10000]`, `aoRaysPerVertex` to `[1, 4096]` --
   the same ranges worklib itself enforces. Policy lives here, in the
   command, not in the agent or the runner.
3. `PartShelf::beginBatch` bumps `generation_`, drops the previous board
   wholesale (`parts_.clear()`), and returns this batch's jobs.
   `JobRunner::enqueue` stamps every job with the new generation and submits
   one `QRunnable` per job to its own `QThreadPool`.
4. Once a `QRunnable` actually reaches a worker thread: a stale-start check
   against `currentGeneration_` -> `worklib_bake_part`, which calls back
   into the runnable's own `progressThunk` at every AO checkpoint along the
   way -> copy the DLL's buffers (a checkpoint's still-mid-sweep snapshot,
   or the finished mesh once the call returns) into an owned `MeshBuffers`
   -> `worklib_free_mesh` once the bake itself is done.
5. `BakeRelay::postStarted` / `postProgress` (one call per checkpoint) /
   `postFinished` marshal every one of those back with
   `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` -- the one and
   only crossing from a worker thread to the UI thread.
6. Back on the UI thread, the queued lambda calls
   `onStarted`/`onProgress`/`onFinished`, which `kernel.send()`s
   `BakeJobStarted`/`BakeJobProgress`/`BakeJobFinished` into the dispatcher.
7. `BakeJobStartedCommand`/`BakeJobProgressCommand`/`BakeJobFinishedCommand`
   call `PartShelf::markStarted`/`markProgress`/`storeBaked`, each
   generation-checked against the shelf's current batch (`markProgress`
   alone drops a stale generation silently -- see "Watching the
   computation" below).
8. Every mutation ends in `publishSnapshot()`, broadcasting one
   `ShelfChanged` fact with the full part list (each part's own `progress`
   included), worker list, and counts.
9. `ShelfPresenter::onShelfChanged` updates the status row and worker lanes,
   and -- for every part whose on-screen preview is behind what the shelf
   can now show, Running or Baked alike -- pulls its latest `MeshBuffers`
   straight off the live agent and hands it to the canvas.
10. `ShelfGlWidget::setPartMesh` copies that data into a pending-upload
    queue, replacing any GPU buffers already uploaded for that part;
    `paintGL` drains it (and any deferred GL releases) once per frame -- the
    one place this widget's GL context is guaranteed current. The fragment
    shader renders each vertex ghost-gray until its own AO value stops
    being worklib's -1.0 sentinel.

## Mapping to the reference pattern

This example's threading shape follows
`docs/ref/opengl-context-multi-threads.md` (in the sibling `planura` repo,
not this one) -- the article that keeps the GL context on the main thread
and passes commands to it through a thread-safe "GLJob queue" the owner
thread drains, with workers waiting on a finish-mutex so anything they
passed by reference stays alive until their jobs run.

| The article | This example |
|---|---|
| A hand-rolled `GLJobQ` (mutex + `std::queue`) | Qt's own event loop, reached via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` -- no custom queue needed |
| "Execute one GLJob per frame" drain loop | `paintGL`'s pending-upload drain, literally: `releaseDeferred()` then `drainPendingUploads()` at the top of every frame |
| A finish-mutex the worker locks so its by-reference locals outlive the queued job | Not needed here -- data crosses threads by ownership, not by reference: copied into `MeshBuffers`, then moved into a `shared_ptr` that rides the queued lambda itself |
| "Never share the context across threads" | Worker threads never see GL at all; a `QRunnable` only ever calls `worklib_bake_part` and `BakeRelay::post*` |

## The DLL slot is where your solver goes

`worklib.h` is the entire contract for what a domain team's black-box solver
looks like from the app's side: deterministic given `(seed, smoothIters,
aoRaysPerVertex)`, thread-safe purely by holding no mutable state of its own
(any number of threads may call `worklib_bake_part` concurrently, no
locking), and DLL-allocated -- `out`'s buffers are malloc'd inside worklib,
so the caller passes them to `worklib_free_mesh` instead of `free()`/`delete`
directly, because allocation crosses a module boundary and the allocator
that handed out the memory has to be the one that reclaims it. A nonzero
return doesn't crash anything upstream; `JobRunner`'s runnable just treats
`ok = false`, and that surfaces as a `Failed` part on the board. Nothing
above `job_runner.h` assumes anything about worklib's internals, only its
ABI -- swap the `.cpp` for a real vendor solver and nothing else would need
to change.

`aoRaysPerVertex` is this example's one weight knob: the ambient-occlusion
bake is a deliberately brute-force O(vertexCount * rays * triangleCount)
any-hit ray cast with no BVH, and it stays that way on purpose -- a
GPU-side bake would be faster, but it would also erase the one thing this
example exists to show: real CPU work happening on a worker thread while the
UI thread stays free.

## Generations and stale work

Every `RegenerateRequested` bumps `PartShelf::generation_` and wipes
`parts_` in the same call -- the previous board's entries and buffers leave
together, not one at a time, which keeps `total` scoped to "this batch's
size" instead of growing across every regenerate. Two guards, each owning a
different failure window, keep late results from resurrecting a superseded
batch:

- **Stale-START**, inside the `QRunnable` itself: by the time a submitted
  job actually reaches a worker thread, a newer regenerate may already have
  superseded it. The check compares the job's stamped generation against
  `currentGeneration_` and, if it lost the race, returns before calling
  `worklib_bake_part` or the relay at all -- skipped silently, because
  nobody is owed a notification for a part that's already off the board.
- **Stale-ARRIVAL**, inside `PartShelf::storeBaked`: a job that passed the
  stale-start check can still finish after the shelf has moved on again. This
  one can't stay silent -- the worker's "no longer busy" state still needs
  recording -- so it's counted in `discardedArrivals` and shown as "stale N"
  in the status row.

Neither guard can do the other's job: the runnable's check saves a worker
thread from a wasted bake, which the shelf alone can't prevent once a job is
already running; the shelf's check is the backstop for whatever slips past
it, which the runnable alone can't provide since it has no view of the
shelf's current board. (`markStarted` re-checks the same generation a third
time, belt-and-suspenders, for the narrow window where a start and a
`beginBatch` interleave right at the boundary -- it owes no fact either.)

The `--smoke` check exercises exactly this: batch A bakes to completion,
batch B (a heavy 256-ray batch) is superseded mid-flight by batch C, and the
run asserts `discardedArrivals >= 1` and an exact `sumCompleted` -- the two
guards' behavior pinned down as a test, not just a comment.

## The worker panel

`ShelfPresenter` keys each lane card by the OS thread id it first observes
in a `BakeJobStarted` -- `QThreadPool`'s own worker threads are anonymous
and created lazily, so a new lane card appearing on screen *is* thread
creation made visible, not a UI event standing in for one. Each card shows a
colored dot (green while busy, gray while idle) next to the thread id, a
second line naming the current part and its AO-sweep progress -- "part 7 ·
40%", falling back to just "part 7" if that part can't be found in the
latest fact, or "idle" -- and a running completed-count. The "Max threads" spinbox in the left panel is
wired straight to `JobRunner::setMaxThreads`, not through an event --
deliberately: pool size is view-side infrastructure, not domain state, and
nothing else in the domain loop would ever care to hear about it. Bumping it
mid-batch is the easiest way to watch a new lane appear live. Thread
retirement has no symmetrical signal -- Qt never tells anyone a pooled
thread went away -- so a lane that goes quiet just sits there showing "idle"
forever. That is a documented limitation, not a bug.

## Watching the computation

Every part now appears the instant its geometry exists -- as a flat
ghost-gray mesh -- and visibly shades in as the AO bake sweeps its vertices,
instead of popping in fully baked once the whole part is done. This rides on
one addition to worklib's own ABI:

```c
typedef void (*WorklibProgressFn)(const WorklibMesh* snapshot, float fraction, void* user);
```

`worklib_bake_part` takes this callback (nullable -- NULL reproduces the
exact old, silent behavior) and calls it once with `fraction == 0.0` the
moment geometry and topology are final, then again at roughly every tenth of
the vertex count as the AO loop completes them, never at the very end (the
function's own return is the final delivery; there is no `1.0` callback).
`snapshot` is only valid for the duration of the call -- it points at
worklib's own in-progress output buffers, not a copy -- so anything that
wants to keep it has to copy it before returning. Every `aoValues` entry
worklib hands out is either a finished value or the `-1.0` "not yet
computed" sentinel; a snapshot is never seen with a vertex half-cast. This is
deliberately written as the real shape a solver-progress callback takes in
practice, not a toy simplified for the example.

The ghost-gray-to-shaded sweep is not an animation layered on top of the
mesh -- it is worklib's own per-vertex iteration order (ring-major; see
`triangulateRinged`) made visible. The fragment shader checks each vertex's
own `vAo` and renders flat gray for a still-negative one, so the frontier
between gray and lit tracks exactly which vertices the AO loop has reached
so far, nothing more.

The callback reaches the UI thread through the same marshalling seam as
everything else in this example: `job_runner.h`'s `progressThunk` (passed as
worklib's `progress` argument, so it runs mid-bake on the worker thread) is
handed a small stack context of `{ relay, partId, generation, threadId }`;
it copies the snapshot's four arrays into a `MeshBuffers` and calls
`BakeRelay::postProgress`, which queues onto the UI thread exactly like
`postStarted`/`postFinished`. `BakeJobProgress` is the third event on that
seam, and `BakeJobProgressCommand` is the third thin forward into
`PartShelf::markProgress` -- the routing table in `bake_job_commands.h` grew
by one entry, not by a new mechanism.

A stale-generation `BakeJobProgress` -- one whose batch has since been
superseded -- is dropped SILENTLY by `markProgress`, unlike
`storeBaked`'s stale-arrival branch, which still counts into
`discardedArrivals`. That asymmetry is deliberate: `discardedArrivals`
means "completed work discarded," and a superseded bake's in-flight
checkpoints are not completed work -- they are progress nobody asked to
see anymore, on a part that has already left the board. Counting every
checkpoint of every superseded bake still running to completion (see
below) would inflate that counter into something that no longer means
what its name says.

One natural variation is deliberately left out: a cancellable callback,
where `progress` returns an abort flag and worklib bails out of the AO loop
at its next checkpoint instead of running a stale bake to completion. That
would save real CPU time on a superseded batch, but it would also remove
the one thing `--smoke` currently relies on to exercise
`PartShelf::storeBaked`'s stale-arrival guard deterministically: batch B's
heavy bake has to actually finish, late, for its `BakeJobFinished` to prove
the guard drops it.

## Interaction model

The toolbar exposes Seed (0..2,000,000,000), Parts (1..64, default 16), and
AO rays (1..1024), plus the Regenerate button; `smoothIters` is fixed at 8 in
the GUI -- one less knob on screen -- even though the domain command still
accepts it as a parameter. Orbit drag (left-mouse) and wheel zoom stay live
at every moment, including mid-batch, because the camera only ever touches
`ShelfGlWidget`'s own state and never waits on a bake. Each part gets a
distinct pastel color from a golden-ratio hue rotation over its `partId`, so
neighboring parts on the shelf read as visually separate; per-vertex ambient
occlusion darkens crevices through the fragment shader's `aoTerm = mix(0.35,
1.0, ao)`.

## Rejected alternatives

| Alternative | Why not |
|---|---|
| Share the GL context across threads / `MakeCurrent` handoffs | The reference article's own conclusion: race-prone and unnecessary once commands can be marshaled instead |
| Bake ambient occlusion on the GPU | Erases the worker-thread lesson this example exists to teach |
| Carry `MeshBuffers` inside the `ShelfChanged` fact | Facts stay light on purpose -- big data lives on the agent, a view pulls only what it needs |
| A hand-rolled thread-safe job queue | Qt's event loop, reached through `invokeMethod`, already is one |

## Build & run

```
cmake -S examples/mesh-farm -B build-mesh-farm -G Ninja -DCMAKE_PREFIX_PATH=<Qt6>
cmake --build build-mesh-farm
./build-mesh-farm/mesh-farm            # the app
./build-mesh-farm/mesh-farm --smoke    # headless self-check, exit 0
./build-mesh-farm/mesh-farm --gui-probe  # full window PNG proof, writes to C:/tmp/mesh-farm-probe
```

worklib's DLL builds into the same output directory as the executable, so no
install step is needed; Qt's and MinGW's runtime DLLs still need to be on
`PATH` to run either binary.
