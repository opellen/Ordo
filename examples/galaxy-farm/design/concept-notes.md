# Concept notes — galaxy-farm

Source: `ordo-galaxy-farm.png` (generated 2026-09-01 by San via ChatGPT;
original kept at `docs/ref/ordo-galaxy-farm.png`). One image, a full window
mockup captured mid-jump — it pins the UI layout, the transit visual, and
the defaults in one shot.

**Theme decision (San, 2026-09-02): the whole window is DARK-THEMED** —
near-black panels, light text, blue accent — unlike mesh-farm's default
(light) Qt look. Title bar in the mockup reads "Ordo Galaxy Farm".

## What the image shows, region by region

### Top toolbar
- `Seed` spinbox — a large (64-bit-scale) value: `8842112273027`.
- `Galaxies / sector: 32` spinbox.
- `Stars / galaxy: 60k` spinbox (abbreviated display).
- A prominent **Jump** button, cyan/blue outline — the one user intent.
- Settings gear + overflow-menu icons (decorative; no pinned behavior).

### Left panel — Workers
Header "Workers", then `Max threads: 6` spinbox, then one lane card per
worker thread (mesh-farm's worker panel, restyled dark):
- colored dot: green = busy, gray = idle;
- thread id rendered in hex (`0x51b0`) — mesh-farm shows decimal;
- right-aligned running completed-count (23, 22, 21, 18, 0, 0);
- second line: `galaxy #87 · sector (3,-1)` while busy, `idle` otherwise.
  Galaxy ids look globally cumulative across jumps, not per-sector.

### Viewport — jump transit
- Radial star-streak tunnel converging toward the screen center: gold /
  blue / purple / white streaks over black.
- The tunnel's center is a dark circular void — and INSIDE it the
  destination sector is visible: spiral and elliptical galaxies scattered
  on black, some fully lit, some faint (reads as "still building").
  This is the "destination visibly under construction through the jump
  tunnel" beat, literally framed: tunnel = periphery, destination = center.
- Top-right HUD overlay in cyan, corner-bracketed, monospace feel:
  `jumping to sector (3,-1)` / `building 32 galaxies`.
  Sectors are keyed by **2D integer coordinates**.

### Bottom status bar
- `Progress` label + blue bar: `11 / 32 galaxies` (destination build-up).
- Dot-labeled counters: `running 6` (green), `queued 15` (blue),
  `generation 3` (purple) — the jump epoch surfaced by name.

## Decisions this image pins (feeding DESIGN.md's Open Points)

1. **Dark theme** for the whole window (San, 2026-09-02).
2. **Defaults**: 32 galaxies/sector, 60k stars/galaxy, 6 worker threads.
3. **Sector identity**: 2D integer coordinates `(x,y)`, shown in the HUD
   and worker lanes.
4. **Transit look**: streak tunnel around a central void; destination
   renders inside the void and fills in galaxy by galaxy; cyan bracketed
   HUD text names the target sector and the build count.
5. **Status vocabulary**: running / queued / generation counters on the
   status bar; per-galaxy progress aggregated as `N / total galaxies`.

## Resolved 2026-09-02 (San)

- Naming: example/executable `galaxy-farm`, window title "Ordo Galaxy Farm".
- Camera locked forward during jump; transit ~4 s (view-side constant).
- Idle = free orbit camera over the current sector (mesh-farm's orbit
  carried over).

## Still open

- Whether the DLL's `sizes` buffer stays in the ABI or point size derives
  from color luminance — decide when the shader takes shape.
