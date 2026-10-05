# galaxy-farm — Rendering & Simulation Algorithms

Code lives under `examples/galaxy-farm/`: the generator DLL in `galaxylib/`,
the renderer in `view/galaxy_gl_widget.{h,cpp}`.

## Provenance & licenses

| System | Source | License |
|---|---|---|
| Galaxy generation + point rendering | [beltoforion/Galaxy-Renderer](https://github.com/beltoforion/Galaxy-Renderer) (Ingo Berg) | BSD-2-Clause, attribution kept in `galaxylib.cpp` and shader comments |
| Blackbody color LUT | Galaxy-Renderer `Helper.hpp` (200 entries, 1000–10000 K) | BSD-2-Clause |
| Sky cubemap bake | [wwwtyro/space-3d](https://github.com/wwwtyro/space-3d), via [matusnovak's OpenGL port](https://github.com/matusnovak/glfw-space-3d); 4D classic noise by Stefan Gustavson | MIT-family, attribution kept in the shader source |

## 1. Galaxy generation — galaxylib ABI v3

One C ABI call: `galaxylib_generate_galaxy(seed, type, starCount, progress, user, out)`.
The DLL emits **orbit parameters, not positions** — final placement happens in
the vertex shader every frame (§2), which is what makes live differential
rotation free.

**Output per point** (all arrays `starCount_total` long, kind-contiguous):
`orbitA`, `orbitB` (ellipse semi-axes, normalized: galaxy radius = 1),
`theta0` (deg), `velTheta` (deg/year), `tiltAngle` (rad), `colors` (rgb,
blackbody), `mags` (brightness/size magnitude), `kinds`. Plus one
`GalaxylibGalaxyParams` struct per galaxy — the shader's uniforms
(radCoreN, radFarFieldN=2, exInner/exOuter, angleOffsetN, barRadiusN/barEx,
pertN/pertAmp, dustRenderSize, h2SizeMax, h2Threshold, baseTemp).

**Density-wave model** (ported from Galaxy-Renderer's `Galaxy.cpp`, computed
in the preset's native parsec scale, lengths normalized at the write seam
`CloudWriter::emit`):
- Radius sampling: CDF built from bulge `exp(-k·R^0.25)` + disc `exp(-R/a)`
  (Simpson-integrated, inverse-sampled).
- Orbit ellipse: `a = r`, `b = r·e(r)` with the piecewise eccentricity
  profile (round core → exInner at core edge → exOuter at disc edge → round
  far field; bar segment when barRadius > 0). Tilt `= r·angleOffset` — the
  per-radius tilt drift IS the spiral arm (density-wave theory).
- `velTheta` from the ported orbital-velocity curve **including dark
  matter** (mass disc + halo), so inner orbits visibly outrun outer ones.

**Populations** (kind-contiguous blocks, prefix-complete progress callbacks):
1. STAR = `starCount` — CDF radii, temp 4000–8000 K, mag 0.1–0.5 (1/60
   brightened up to 1.0).
2. DUST = `starCount` (ratio varies per type) — half CDF radii, half
   uniform-square (fills the inter-arm field); temp = `baseTemp +
   r-gradient` (inner yellow → outer blue, "no science, looks right");
   mag 0.02–0.17.
3. FILAMENT = `dust/100` chain seeds × uniform[0,99] links — radius ±200 pc
   jitter along a shared theta, temp −1000 K.
4. H2 + H2_CORE = `starCount/100` **pairs** (same orbit params twice; the
   halo and its white pinpoint core — ignition happens in the shader, §2).
- HAZE (kind 0) is never emitted in v3 — bulge glow emerges from CDF density.

**Presets & types**: the reference's nine preset parameter sets are baked as
a table (radius, coreRadius, angularOffset, ex1/ex2, counts, pert,
dustRenderSize, baseTemp, h2SizeMax/threshold); a seed hash picks one. The
caller-facing 6-type axis is an override layer on top — spiral (as-is),
barred (+bar 0.4·coreRadius, barEx 0.5), elliptical (ex≡1, no dust/H2,
+800 K), irregular (pertN 3, pertAmp 15 — smaller amp = stronger
distortion, it divides), starburst (+1500 K, 3× H2), dust-belt lenticular
(0.3× dust, ex→0.95). Domain-side weighted draw: 26/22/14/8/14/16 %
(`star_map.cpp::galaxyType`). Add new looks by adding presets, not code
paths.

**Contracts**: byte-identical determinism from (seed, type, starCount) —
every draw comes from a splitmix64 stream chain, one independent stream per
point (thread-safety by construction); validation zeroes everything on bad
input; `galaxylib_free_cloud` is idempotent; a calibrated busy-loop keeps a
60k-star bake at ~1–2 s so the demo's worker lanes stay visibly busy.

## 2. Galaxy rendering — orbit evaluation in the vertex shader

One shader pair, one draw per kind range (`kindMode` uniform 1–5; the
ABI's kind-contiguity means ranges, not per-vertex branches). Blend
`GL_SRC_ALPHA, GL_ONE` — **everything is additive light**; "dark lanes"
are density-wave gaps, never absorption. (Subtractive dust models were
tried and rejected: over a bright nebula background they let a galaxy
render darker than the sky behind it, which no photograph shows.)

- `calcPos`: rotated-ellipse evaluation of `theta0 + velTheta·time` with
  the tilt, plus the m-armed perturbation
  `ps += (a/pertAmp)·(sin, cos)(2·pertN·α)`.
- Per-kind size/color (the reference's formulas, verbatim):
  star `mag·4 px`; dust `mag·5·dustRenderSize px` (huge faint particles —
  their overlap IS the arm haze, alpha 0.05 cone); filament
  `mag·2·dustRenderSize`, alpha 0.07; H2 halo `h2SizeMax·ignite` tinted
  `(2.0, 0.5, 0.5)·ignite`, core `size/10`, white.
- **H2 ignition in-shader**: orbit-crowding measure — distance to the
  neighbouring density waves at `a ± Δ` (Δ ≈ 0.077 normalized ≈ 1000 pc),
  each with its own eccentricity/tilt; `rho = ½(ΔI/dI + Δ/dO)`,
  `ignite = smoothstep(th, 1.5·th, rho) · barFactor(a)` (bars are old and
  gas-poor — suppression to 1.05× bar radius).
- **Time**: one global clock, `+= dt·6e6 yr/s` (the reference's own
  100 000 yr/frame at 60 fps) → live differential rotation.

**Scale-invariance law** (why no global gain knobs exist): above the 1 px
floor, sprite area ∝ s² exactly cancels screen density ∝ 1/s², so
per-pixel brightness is zoom/distance-invariant by itself. Two corrections
only:
- `sizeFactor = min(projectedGalaxyRadiusPx / (0.383·1080), 1.0)` — maps
  the reference's fixed pixel sizes (tuned for one fullscreen galaxy) to
  this galaxy's projected size, **frozen at 1.0 past reference framing**
  so deeper zoom becomes pure magnification (crisp fine grain, like the
  reference's own fov zoom).
- Energy-conserving floor: sub-pixel sprites are drawn at 1 px with
  `vEnergy = wanted²/floored²` scaling their color — without it small
  galaxies blow out into white blobs (the floored sprites stop shrinking
  while density keeps growing).
- Count invariance: intensity × `min(1, 40000/starCount)` (40 000 = the
  reference's numStars; more points add smoothness, not brightness).

**Depth cue** (ours, not the reference's): per-galaxy
`depth01 = clamp((dist − nearest)/span)` — anchored on the frame-global
nearest galaxy across both jump boards, span = 1.2× sector radius (world
constant, so the cue survives any zoom). Applied as an artistic intensity
curve `max(1 − 0.55·depth01, 0.15)` and a 70 %-at-far desaturation.

## 3. Bloom post-pass

The scene renders into an RGBA16F FBO (values above 1.0 must survive),
then: bright-pass extract at **quarter res** (soft-knee threshold
0.55 ± 0.20 on the MAX channel — a saturated pure-blue spark reads "maxed"
to the eye even when luminance-weighting would miss it) → 9-tap separable
gaussian ping/pong → composite `scene + strength·blur` onto
`defaultFramebufferObject()`. HUD (QPainter) draws after, outside the
bloom. `GF_BLOOM_STRENGTH` env var overrides the baked 0.9 (0 = off). All
chain textures GL_LINEAR + CLAMP_TO_EDGE (Qt FBOs default to GL_NEAREST —
the quarter-res upsample otherwise stamps a visible 4 px grid).
Note: `glClear(GL_DEPTH_BUFFER_BIT)` is gated by the
current `glDepthMask` — a freshly allocated FBO depth buffer needs the
mask on for the clear to reach it.

## 4. Sky — baked cubemap

Port of space-3d's `generate()`: the sky is **baked once per sector** into
an RGB8 1024³ cubemap (~4 ms), then rendered as one cubemap sample per
pixel. Two cubemaps stay resident (current + destination sector).

Bake (per cube face, 6 standard 90° capture views, additive
`GL_SRC_ALPHA, GL_ONE` over black):
1. Stars: ~20 000 tiny + ~100 large point sprites at random unit
   directions; near-white (0.9–1.0) subtly tinted toward the sector
   palette; `pow(1 − d, 0.5)` cone falloff.
2. Nebulae: 3–5 fullscreen layers of wwwtyro's fragment shader — 4D
   classic noise (cnoise) fed through a 6-step domain-displacement fBm;
   per layer uScale 0.25–0.75, uIntensity 0.9–1.1, uFalloff 3–6, uOffset
   ±1000³. **uColor is a hashed mix of the sector's curated palette pair**
   (six hand-picked pairs — violet/ember, teal/amber, … — never random
   RGB): the app's sky identity.
3. Distant-galaxy smudges: ~6 soft anisotropic sprites (axis ratio ~3:1,
   a few degrees across, peak ~0.15, palette-tinted).

Runtime `drawSky()`: reconstruct the view ray from the camera basis + FOV,
apply the **jump swirl** (Rodrigues rotation scaled by the speed bell) to
the lookup direction, `mix(current, dest, skyBlend)` for the sector
cross-fade, `× (1 − 0.18·jumpMix)` dimming. Dirty/defer flow: setSectorSky
marks a slot dirty on coordinate change; paintGL bakes at most one cubemap
per frame — the destination's bake lands right after jump start, before
the cross-fade window opens at 25 % travel. Determinism: all bake draws
from a splitmix chain on the sector coords.

## 5. Jump journey

The camera never moves; the **sectors ride a jump axis** frozen from the
camera's forward at launch. The destination board starts
`kJumpDistance = 500` ahead and lands exactly at the origin at arrival
(board promotion swaps domain state, not geometry — no frame jump); the
departing board is retained and recedes behind.

- Travel = smootherstep(t01); its normalized derivative
  `30t²(1−t)²/1.875` is the 0→1→0 **speed bell** driving every
  speed-scaled effect: streak stretch/brightness, sky swirl, the
  cross-fade window (0.25–0.80 of travel), and the FOV speed-kick
  `45° + 18°·speed` (shared by projection AND sky, or the background
  slides).
- **Transit clock is responsive-time**: t01 advances by per-tick clamped
  deltas, so a UI-thread stall (first upload + a burst of generation jobs
  at launch) PAUSES the journey instead of skipping the departure beat.
- **Launch pose is sacred**: jump launches skip the camera reframe (an
  epoch sentinel identifies the app-open first jump, which does reframe),
  the smooth-zoom ease targets are snapped to the live pose at the
  Idle→Jumping transition (an in-flight ease must not glide the view
  during the locked transit), and layout keys are per-board (§7) so the
  departing sector never re-shuffles. The Jumping visual flips in the same
  event-handler run that moves the board — waiting for the next animation
  tick would leave 1–2 board-less frames (a visible flicker).
- **Curved journey**: each jump bows sideways along a random perpendicular
  — path `P(t) = axis·travel(t) + latDir·kJumpCurve·h(t)` with
  `h = 256·t⁴(1−t)⁴` (peak 1.0 mid-transit, kJumpCurve = 80 ≈ 18° swing).
  Both boards shift by the same lateral term, so launch and arrival
  geometry are exact (h and h′ are zero at both ends). The bump's ends must
  be one order FLATTER than the axial smootherstep: axial speed grows as
  t², so a t²-ended bump makes the tangent's lateral/axial ratio diverge
  at launch (the flow leaned sideways in the first frames); quartic ends
  drive the ratio → 0 — depart straight along the view, bow mid-flight,
  straighten for arrival. latDir is a Knuth-hashed counter (deterministic
  per jump, probe-reproducible), deliberately NOT the streak field's seed.
  The streak layer's eased axis chases the path TANGENT
  `axis·s′(t) + latDir·kJumpCurve·h′(t)`, so the whole flow (and the lens
  cone, §6) leans into the curve.

## 6. Jump streak layer (local star field)

A permanent world-space field of 1600 "local stars" near the camera
(cross ±14 × axial 40 units, camera at 15 % from the back), drawn as
**elliptical-distance-field** quads: the field is normalized by an ellipse
whose axial semi-axis grows with the stretch
(`e² = (x/(segHalf+2.4))² + (y/2.4)²`, gaussians on `e·2.4`, zero-rim
window at e = 1), so a beam is thickest amidships and its tips taper
smoothly to points. At rest both semi-axes are equal and a star
degenerates to the same round soft dot. A stretch-gated head bias
(tail floor 0.6) adds the comet cue: the leading end burns hotter, the
tail fades long. Stretch itself comes from the speed bell — anisotropic
scale on real scene objects, so the beams read as the universe flying
past rather than a screen-space overlay.
Position along the axis is driven by accumulated TRAVEL (wrapping); an
ever-running 0.02 field/s drift keeps the field alive at rest; recycled
stars re-enter dim at the far end and brighten as they approach — never
reseeded, never group-faded. The drawn axis eases (~0.5 s) toward a newly
frozen jump axis (the curved journey's path tangent, §5) so the field
wheels instead of teleporting.

**Spacetime lens**: each beam endpoint is pushed hyperbolically away from the LENS
axis — `r′ = sqrt(r² + R²)` with `R = max(axialDist, 0)·lensTan`,
computed in world space around the **camera → destination-board-center
ray** (not the flow axis: the cleared void must sit exactly where the
destination appears on screen, launch through arrival; the ray uses the
same curved-path math as the board offsets). Head and tail deflect by
different amounts, so beams bow around the void. `lensTan` is the
destination board's REAL angular footprint per frame
(`1.15 × sectorRadius / boardDistance`, clamped 1.2 for deep-zoom
launches): as the board approaches, the instantaneous footprint converges
to exactly the landing sector's screen area — no end-state blend needed.
The gate opens 2.5× faster than the speed bell so the void stays open
through deceleration while the beams still glow, closing only as they
settle back into dots.

## 7. Sector layout

Fermat spiral (`r = 5·√(i+1)`, golden angle) as the collision-free BASE,
with per-sector organic irregularity — all hashes mix a **layout seed**
derived from the sector coordinates (each sector gets its own arrangement,
reproducibly):
- angle jitter ±0.45 rad, radius jitter 0.8–1.3×, height ±7;
- pull 35 % toward the nearest of `slotCount/10` seeded cluster anchors
  (clumps form, voids open);
- ~11 % of slots become close companions of their predecessor
  (interacting pairs);
- per-galaxy world radius 0.7–1.5× (`slotScale`), tilts ±35°, yaw hashed.
Layout keys are kept **per board** (active + departing) so a jump launch
never re-evaluates the receding sector under the new sector's seed.

## 8. Camera

Orbit camera (azimuth/elevation/distance around a movable `orbitTarget_`)
with a smooth-zoom **end state** (`targetDistance_/targetOrbit_`): input
edits only the end state; paintGL eases the live pose toward it with
`1 − exp(−dt·8)` — approach speed proportional to the remaining gap.
- **Cursor-anchored zoom**: the wheel dollies toward the point under the
  cursor. Anchor = nearest galaxy bounding-sphere hit along the cursor ray
  (zoom-to-object); empty space falls back to the target plane.
- **Flight mode**: when the dolly floors (kMinDistance 0.02), zoom-in
  becomes translation of the whole rig along the cursor ray — stride from
  the anchor's remaining distance with a scene-scale floor (the nearest
  galaxy's distance serves as the effective scene scale for both flight
  stride and orbit sensitivity once the orbit distance stops meaning
  anything at deep zoom).
- Unlimited zoom-out (max 50 000) with distance-scaled near
  (`clamp(d·0.05, 0.002, 0.05)`) and far (`max(1500, d·4)`) planes.
- Middle-drag pan in the view plane (world-per-pixel at target depth);
  zoom-adaptive orbit sensitivity (`0.4°/px` scaled by
  distance/default-framing, floored).
- Bindings: double-click = glide target back to sector center (keeps zoom
  and angles), Space = full smooth reset to default framing, Enter = Jump
  (window-wide shortcut). A toolbar readout polls eye/target/distance at
  200 ms (the canvas widget deliberately has no Q_OBJECT/signals).

## 9. Verification harness

- `galaxy-farm-harness`: ABI contracts — determinism ×2, kind-contiguity,
  per-kind count formulas per type, params sanity, validation zeroing,
  free idempotence, per-type bake timing.
- `--smoke`: headless domain loop (streaming jump, arrival, parallel bake,
  stale-generation discard).
- `--render-probe`: widget-only, 6 galaxies (one per type), 3 PNGs.
- `--gui-probe`: full window/presenter path, 2 PNGs.
