# 05 — Video: Geometrizer Simulation & Rasterizer [verified against MAME source]

*Replaces the earlier placeholder version of this file, which hadn't yet
read the actual video source. See `02-tgp-coprocessor.md` for why the
"Geometrizer" role documented here is a separate thing from the
"Coprocessor" TGP role.*

## Custom Sega video ASICs (part numbers + stated storage, from `model1.cpp` board header comment)

| Part | Role (as labeled in source) | Storage |
|---|---|---|
| 315-5422 | Z-sorting | 8K×16bit + 128K×16bit |
| 315-5423 | Renderer 1 | — |
| 315-5424 | Renderer 2 (provides "OPR-14748" addresses) | — |
| 315-5425 | Renderer 3 / framebuffer management | 32K×8bit + 2×256K×16bit |
| 315-5292 | Tilemaps | 128K×32bit + 32K×16bit |

## MAME's approach: HLE simulation of the Geometrizer + rasterizer pipeline

`model1_v.cpp`'s own top-of-file comment: *"Model 1 geometrizer TGP and
rasterizer simulation."* This is a hand-written C++ reimplementation of what
the real 315-5422/5423/5424/5425 video ASICs and the 315-5571/5572
Geometrizer chips do — not a replay of real microcode/gate-level logic.
Given how well-developed this simulation is (it's what makes Virtua Racing
fully playable in MAME today — see `06-mame-oracle-status.md`), **this is
the right v1 target for our own project too**: a behavioral HLE
reimplementation of the same pipeline, not a doomed attempt to reverse
engineer five undocumented video ASICs from scratch.

### Pipeline stages, as implemented

1. **Transform** (`view_t::transform_point`, `transform_vector`) — object
   points go through a 3×3 rotation/scale matrix (`translation[0..8]`) plus
   a translation (`translation[9..11]`) and a view offset (`vxx/vyy/vzz`),
   then an additional Y-axis rotation (`ayyc`/`ayys` — cos/sin of some
   "world yaw" angle) is applied on top. This two-stage transform (object
   matrix, then a separate yaw term) is a concrete detail worth preserving
   exactly, not simplifying away.
2. **Projection** (`view_t::project_point`, `project_point_direct`) —
   perspective projection from camera space to screen space.
3. **Clipping** (`fclip_clip_top/bottom/left/right`, `fclip_push_quad*`) —
   frustum clipping against all four screen edges, implemented per-edge
   (not a generic Sutherland-Hodgman polygon clipper) with a "level"
   parameter suggesting recursive/staged clipping. `view_t::recompute_frustum`
   sets up the frustum planes from the viewport (`set_viewport`).
4. **Lighting** (`compute_specular`) — a specular lighting term computed
   per-face/vertex from a light direction and per-view light parameters
   (`set_light_direction`, `set_lightparam`) — this is where Gouraud-style
   shading values come from before rasterization.
5. **Sorting** (`sort_quads`, `unsort_quads`, `quad_t::compare`) — confirms
   the challenges doc's assumption of draw-order/painter's-algorithm-style
   sorting rather than a Z-buffer. This is a real, separate sort pass over
   quads before rasterization, not sorting-as-you-go.
6. **Rasterization** (`fill_quad`, `fill_slope`, `fill_line`, `draw_hline`
   and a `draw_hline_moired` variant) — scanline-based quad fill. The
   "moired" variant (`MOIRE` flag, `0x01000000`) suggests a dithering or
   moiré-pattern fill mode used for some effect (fog/distance fade in
   Virtua Racing's horizon, plausibly) — worth investigating concretely
   during Phase 6 rather than guessing further here.
7. **Command dispatch** (`tgp_render`) — a "display list" of commands (built
   from `m_display_list0`/`m_display_list1`, double-buffered and selected
   via `set_current_render_list`/`model1_listctl_w`) drives which of the
   above stages runs for each list entry. **Opcode table, fully transcribed
   from `tgp_render`'s switch statement (word offsets are relative to each
   entry's start, in 16-bit list words):**

   | Opcode | Words | Meaning |
   |---|---|---|
   | `0x00` | 2 | No-op/skip |
   | `0x01` | 8 | Draw object *below* the HUD tilemaps — `push_object(tex_adr, poly_adr, size, old_z)` from list offsets +2/+4/+6 |
   | `0x41` | 8 | Draw object *above* the HUD (e.g. Star Wars Arcade's radar blips) — same `push_object` call, different compositing pass |
   | `0x02` | variable | Direct (already 2D-projected) polygons — `push_direct`/`skip_direct`; z-sorted alongside `0x01` objects in the same pass |
   | `0x03` | 16 | Set viewport (`set_viewport`) — also the point where buffered objects actually get flushed to the framebuffer via `draw_objects` (i.e. this is the per-viewport z-sort/flush boundary, not just a state-set command) |
   | `0x04` | 6 + len×2 | "ZVIDEO" color table write into `m_tgp_ram` at `adr - 0x40000`, `len+1` 16-bit entries |
   | `0x05` | 6 + len×2 | Upload data into `m_poly_ram` at `adr - 0x800000`, `len` 32-bit entries |
   | `0x06` | 6 + len×2 | Upload lighting params: each entry packs diffuse/ambient/specular (8-bit each) + power (8-bit) into `set_lightparam` |
   | `0x07` | 4 | Mode word — bit 0 enables the specular lighting term, bit 1 tracks display-list double-buffer parity. **MAME's own comment cross-references this as analogous to Sega Model 2's "GEO command 7"** — i.e. Model 1's display-list format is a documented predecessor to Model 2's better-known GEO command language, a potentially useful analogy source for filling gaps later |
   | `0x08` | 4 | "Select mode" — value logged but effect not yet identified in source; open item |
   | `0x09` | 6 | Zoom — `set_zoom(x*4, y*4)` |
   | `0x0a` | 8 | Light direction vector (x, y, z floats) |
   | `0x0b` | 26 | Load object transform matrix — 12 floats (rotation/scale + translation, matches the `translation[0..11]` fields used by `transform_point`) |
   | `0x0c` | 6 | View translation (x, y floats) |
   | `0x0f` | — | End of list |
   | *(other)* | — | Unknown opcode — MAME logs and treats as end-of-list |

   This table is complete enough to implement our own display-list
   interpreter directly from it — no further reverse engineering needed for
   the opcode dispatch itself.

   `push_object`'s polygon data (partially read): a flat array of 32-bit
   floats, reinterpreted directly from `m_poly_ram` or a ROM region
   depending on a high address bit, read as a **vertex strip** — each
   object starts with two seed points (6 floats: two X/Y/Z triples), then
   presumably walks forward adding points per quad (consistent with the
   quad-based hardware confirmed in the challenges doc). Exact stride/count
   past the seed points not yet fully transcribed — remaining open item
   before Phase 6 coding, along with opcode `0x08`'s effect. (Notable in
   passing: a defensive guard at the top of `push_object` carries the
   comment *"Protect against bad data when attacking a super destroyer,"*
   i.e. a real Star Wars Arcade gameplay bug MAME had to work around.)

### Notable implementation dependency

`model1_v.cpp` uses the **GLM** (OpenGL Mathematics) header-only C++ library
for some vector math (`glm::vec3`, `glm::dot`). This is a MAME implementation
detail, not a hardware fact — we don't need to use GLM ourselves, but it
tells us the vector math involved is standard 3D graphics math (dot
products, cross products — `cross_product` is also implemented directly),
not some exotic fixed-point scheme. Worth noting for our own math library
choice in `src/tgp/` (or wherever we land this code — see architecture doc
update).

## Revises the project's plan

- **The "Geometrizer" 3D pipeline should be v1-scoped as HLE**, following
  MAME's proven approach (transform → project → clip → light → sort →
  rasterize, driven by a display-list opcode dispatch), not real 315-5571/
  5572 microcode LLE. This directly updates `docs/planning/04-architecture.md`
  and `05-roadmap.md`, which — after the *previous* research pass — had
  swung to assuming LLE for "the TGP" as a whole. Only the separate
  Coprocessor role (see `02-tgp-coprocessor.md`) is LLE; this rasterizer
  pipeline is HLE.
- True LLE of the real Geometrizer microcode (315-5571/5572) is a
  legitimate stretch goal that would make this project *more* accurate than
  MAME's current implementation in this one area — interesting, but
  explicitly not v1 scope; don't let it block Phase 6.
- ~~Building an accurate opcode table~~ — done, see the table above. Phase 6
  can now be scoped directly against it.

## Transform/projection implemented — and a dead code path caught along the way

Phase 6 work: `src/video/geometrizer.{h,cpp}` implements the transform and
projection pipeline stages, confirmed against `model1_v.cpp`'s
`view_t::transform_point`/`project_point`/`project_point_direct`. Pure
math, no ROM dependency — fully testable with synthetic data, including a
90-degree-about-Y rotation test whose expected output was worked out by
hand from the standard rotation formula independently of the
implementation, not just re-deriving the same formula (`geometrizer_test.cpp`).

**A real, worth-flagging finding**: `transform_point`'s real formula also
includes a view-offset (`vxx`/`vyy`/`vzz`) and an extra yaw rotation
(`ayyc`/`ayys`, applied to the transformed X/Z after the main matrix) —
originally assumed in earlier research to be real per-game camera data.
Tracing every assignment to these fields shows the **only** code that ever
sets them is a developer debug-camera control (mapped to keyboard input:
F/G/H/J/K/L for position, U/I for yaw) — and that entire block is wrapped
in `#if 0`, i.e. **compiled out of the reference build entirely**. No real
game data path touches these fields; they permanently hold their
zero-initialized defaults (`vxx=vyy=vzz=0`, `ayy=0` so `ayyc=1`/`ayys=0`)
for every frame of every game. This confirms the *operative* transform is
just the plain 3×4 affine matrix (`translation[12]`) — `geometrizer.h`
documents this and deliberately omits the always-inert terms rather than
implementing dead code, with the exact reference formula recorded here in
case a target game is ever found to need them after all:

```
xx = t[0]*qx + t[3]*qy + t[6]*qz + t[9]  + vxx
p->y = t[1]*qx + t[4]*qy + t[7]*qz + t[10] + vyy
zz = t[2]*qx + t[5]*qy + t[8]*qz + t[11] + vzz
p->x = ayyc*xx - ayys*zz
p->z = ayys*xx + ayyc*zz
```

Also confirmed: `translation[12]`'s layout is **column-major** — indices
`[0,1,2]` are the column of coefficients multiplying the input's X
component (contributing to the outputs' x/y/z respectively), `[3,4,5]`
multiply Y, `[6,7,8]` multiply Z, and `[9,10,11]` is the translation. This
was confirmed by deriving a concrete rotation matrix by hand and checking
it against the formula's indexing, not assumed from convention (row-major
would have been an equally plausible guess without checking).

## Frustum-clipping plane tests/intersections implemented

Phase 6 work continues: `src/video/geometrizer.{h,cpp}` now also implements
the 4 frustum-plane test/intersection primitives, confirmed against
`model1_v.cpp`'s `view_t::recompute_frustum` and
`fclip_isc_bottom/top/left/right` / `fclip_clip_bottom/top/left/right`:

- `recompute_frustum()` derives 4 slopes (`a_left`/`a_right`/`a_bottom`/
  `a_top`) from the viewport's clip bounds, center, zoom, and view
  translation — triggered from `set_viewport`, `set_zoom`, and
  `set_view_translation` alike (confirmed all three call it in the
  reference).
- `is_clipped_*(p)` tests a view-space point against `p.z` scaled by the
  relevant slope rather than projecting to screen space first — this
  avoids a divide-by-zero/sign-flip for points at or behind the eye
  (`z <= 0`) that a screen-space test would need to special-case
  separately.
- `clip_*(p1, p2)` computes the segment/plane intersection via a `t`
  parameter and then projects the result — confirmed the reference calls
  `project_point` as the last step of each `fclip_clip_*`, i.e. the
  intersection point comes out already in screen space, unlike a quad's
  original vertices which get projected independently.

Verified with hand-derived test cases in `geometrizer_test.cpp`: a
symmetric "unit-slope" frustum (viewport bounds ±100 at zoom 100 yields
slopes of exactly ±1, i.e. the region `-z <= x,y <= z`) makes both the
`is_clipped_*` true/false cases and the `clip_*` intersection points
verifiable by hand (e.g. two points sharing `z=5` crossing the `x=z` plane
at `x=5` by similar triangles), independent of the implementation.

**Explicitly deferred** (see `geometrizer.h`'s scope comment): the
recursive quad-clipping *orchestration* (`fclip_push_quad`, ~90 lines of
hardcoded case analysis walking a quad through all 4 planes and splitting
it into however many sub-quads/triangles result) is real, substantial
logic that deserves its own increment rather than being rushed in
alongside the primitives it builds on. Lighting, sorting, and
rasterization remain separate, also-not-yet-implemented stages.

## Quad-clipping orchestration implemented

Phase 6 work continues: `View::clip_quad` (plus private helpers
`clip_quad_level`/`is_clipped`/`clip`) now implements the deferred
orchestration above, confirmed line-by-line against `fclip_push_quad`/
`fclip_push_quad_next`. Design notes:

- Recurses through the 4 planes in the reference's own order (bottom, top,
  left, right — confirmed from `m_clipfn[0..3]`'s initialization). A quad
  that's fully inside a plane passes straight to the next one; fully
  outside, it's dropped; otherwise the vertices are rotated so the first
  clipped-out vertex is at index 0 and the guaranteed-inside vertex is at
  index 3, then one of 4 hardcoded cases (matching the reference exactly)
  produces 0, 1, or 2 new quads/triangles (a triangle represented, per the
  reference's own convention, by repeating one vertex twice) which recurse
  into the next plane.
- Returns clipped quads/triangles by value (`std::vector<Quad>`) instead
  of writing into a preallocated arena with raw pointer bookkeeping
  (`m_pointpt`/`m_quadpt`) — a deliberate implementation-detail departure
  from the reference (MAME-specific memory management, not a hardware
  fact), while the actual clipping math and case analysis are transcribed
  exactly.
- Vertices that survive a plane unclipped are passed through **unmodified,
  not reprojected** — only freshly-created intersection points come back
  already in screen space, since `clip_bottom/top/left/right` project as
  their last step. This matches the reference's own behavior and is a
  detail worth getting right: naively reprojecting every vertex at every
  level would silently diverge from it.

Verified with 3 more hand-derived unit tests in `geometrizer_test.cpp`
(134 unit tests total project-wide, zero warnings under
`-Wall -Wextra -Wpedantic`): a fully-inside quad passes through unchanged,
a fully-outside quad is dropped entirely, and a quad with exactly one
vertex outside the right plane splits into a quad and a triangle whose
two new intersection points were computed by hand (chosen so both
crossings land at the arithmetic mean of their segment, avoiding
repeating-fraction arithmetic) and matched exactly by the implementation.

## Lighting and view-space backface-cull primitives implemented

Phase 6 work continues: `View::transform_vector`, `set_light_direction`,
`set_light_param`, `set_specular_enabled`, `compute_specular`,
`compute_lighting`, and the free function `view_determinant` are now
implemented, confirmed against `view_t::transform_vector`/
`set_light_direction`/`set_lightparam`, `model1_state::compute_specular`,
and `model1_state::view_determinant`. Notes:

- `transform_vector` reuses the same 3×3 rotation/scale matrix as
  `transform_point` but drops the translation and the always-dead
  view-offset/yaw terms entirely — confirmed the reference's own
  `transform_vector` never references them either (unlike
  `transform_point`, where they exist but are dead code — see above).
- `compute_specular`'s power selection is a real, worth-preserving
  hardware/software quirk: the `power` field doesn't feed a continuous
  `pow()`, it gates up to 3 successive squarings at thresholds 2, 4, and
  7 (so power 1 → s¹, 2–3 → s², 4–6 → s⁴, 7+ → s⁸). Implemented as the
  literal `if (p>=2) s*=s; if (p>=4) s*=s; if (p>=7) s*=s;` chain rather
  than a generalized formula, matching the reference exactly.
- `compute_lighting` intentionally stops at the same unclamped `ln` value
  the reference computes right before converting it to an 8-bit lumval
  via the color-translation table — that conversion (and the palette/tile
  color lookup around it) belongs to the still-unimplemented
  rasterization/color stage, not this pure-math primitive.
- `view_determinant` is confirmed to live on `model1_state` in the
  reference, not `view_t` — it doesn't depend on any per-view state, just
  three points, so it's a free function here rather than a `View` method.
  Its confirmed semantics extend only as far as its one call site (a
  positive result culls the face being assembled in `push_object`); no
  further geometric interpretation was assumed.

Verified with 7 new unit tests in `geometrizer_test.cpp` (140 unit tests
total project-wide, zero warnings under `-Wall -Wextra -Wpedantic`),
including a case that specifically proves `set_light_direction` actually
normalizes (an un-normalized (3,4,0) direction and its normalized
(0.6,0.8,0) equivalent would give a diffuse dot product of 5.0 vs. 1.0
against the same normal — the test pins the normalized value) and a case
that walks all 4 of `compute_specular`'s power thresholds against a
hand-computed base value.

**Explicitly out of scope for this increment** (bigger than a single
primitive, deferred like the quad-clipping orchestration before it): the
full `push_object` vertex-stream traversal that reads real per-game
polygon/texture/normal data from ROM or `poly_ram`, assembles quads from
it, and calls into everything above with real data. That's blocked on
real ROM data anyway (see the open items below and the bus/ROM-loading
note), unlike the pure-math primitives implemented so far.

## Back-to-front quad sort implemented

Phase 6 work continues: the free function `sort_quads(std::vector<Quad>&)`
implements the reference's `sort_quads`/`quad_t::compare`, and `Quad` now
carries a `z` sort key (propagated through `clip_quad`'s sub-quads,
matching `fclip_push_quad_next`'s `quad_t cquad(q.col, q.z, ...)`).

**A real, worth-flagging finding**: the reference's comparator
(`quad_t::compare`) doesn't just compare `z` — for equal-z quads it
tie-breaks by comparing the quads' own pointer addresses within a flat,
insertion-ordered array (`this - other < 0`). `qsort` (used via
`sort_quads`) is not inherently stable, so this pointer tiebreak is a
deliberate, manual way of making an unstable sort behave like a stable
one: for insertion-ordered storage, "lower address first" is exactly
"earlier-inserted first." Since our design sorts an explicit
`std::vector<Quad>` rather than reordering pointers into a shared arena,
`std::stable_sort` with a plain descending-`z` comparator reproduces the
exact same observable ordering with no manual tiebreak needed — same
behavior, simpler implementation, confirmed equivalent rather than
assumed.

Also confirmed (from `draw_objects`/`draw_direct`'s call sites): the
reference draws z-sorted 3D quads first, then flushes them, then draws
"direct" (already-2D-projected) polygons in their *original* insertion
order via `unsort_quads` — i.e. direct/HUD-style polygons are explicitly
never z-sorted. That call-site detail doesn't need an equivalent function
in our design (no shared arena to reset), but is worth keeping in mind
once the display-list dispatch (opcode `0x02` vs `0x01`/`0x41`) is
implemented.

Verified with 3 more unit tests in `geometrizer_test.cpp` (142 unit tests
total project-wide, zero warnings under `-Wall -Wextra -Wpedantic`):
descending-z ordering, stable tie-breaking on equal z, and a new
assertion on the existing single-vertex-clip test confirming both
resulting sub-quads inherit the parent quad's `z`.

## Rasterizer scanline primitives implemented (fill_quad's orchestration still deferred)

Phase 6 work continues: a new `src/video/framebuffer.{h,cpp}` adds a
minimal `Framebuffer` (a stand-in for the reference's `bitmap_rgb32` — a
MAME display-surface type, not a hardware fact), and
`src/video/geometrizer.{h,cpp}` adds the per-scanline pixel-writing
primitives `draw_hline`, `draw_hline_moired`, `fill_line`, and
`fill_slope`, confirmed against the reference functions of the same name.

Notable findings, all replicated exactly rather than "fixed":

- **`draw_hline_moired` writes `color` through unmodified, `kMoireFlag`
  bit included.** The reference never masks that bit out of the value
  before storing it in the bitmap. Harmless in practice (the flag lives
  in the pixel format's otherwise-unused top byte), but a real quirk
  worth having on record rather than silently "cleaning up."
- **`fill_line`'s (and `fill_slope`'s inner-loop) "does this span overlap
  the viewport" pre-check uses `||` where an overlap test would need
  `&&`.** Since `x1 <= x2` always holds for these spans, `xx1 <= view.x2
  || xx2 >= view.x1` is a tautology — always true, never actually skips a
  line. The real out-of-view exclusion happens entirely via the clamp
  that follows (and, in the totally-off-screen case, via the resulting
  `x1 > x2` making `draw_hline`'s own loop a no-op). Functionally inert,
  confirmed by tracing through concrete out-of-range cases, not assumed.
- **`fill_slope`'s early return (`y1 > view.y2`) leaves its `*nx1`/`*nx2`
  outputs completely untouched** — not zeroed, not extrapolated, just
  whatever the caller passed in. A caller of the not-yet-implemented
  `fill_quad` orchestration has to know not to trust those outputs after
  such a call.
- **`fill_slope`'s left/right normalization swaps the *output pointers*
  along with the working x/slope values.** It always walks the
  numerically-smaller-x edge as "left" internally, but swapping `nx1`/
  `nx2` right along with `x1`/`x2`/`sl1`/`sl2` guarantees `*nx1` always
  ends up tracking the caller's *original* `(x1, sl1)` edge and `*nx2`
  the original `(x2, sl2)` edge — regardless of which one was walked as
  "left" for this particular segment. Verified with a test that
  deliberately triggers the swap (`x1=500 > x2=100`) and checks the
  outputs land on the *un-swapped* edge identities.

Fixed-point convention: `fill_line`/`fill_slope`'s x coordinates are
16.16 fixed-point integers (`kFracShift`, confirmed against the
reference's `FRAC_SHIFT` enum), letting the (still-unimplemented) polygon
edge walker accumulate sub-pixel slope error across scanlines without
drift. `View::clip_rect()` exposes the existing viewport-bounds fields
(already stored for frustum-slope computation — see above) truncated to
integer pixels, matching the reference's `view_t::x1/x2/y1/y2` (declared
`int` there) for this second, unrelated use as pixel-clip bounds.

**Explicitly still deferred**, same reasoning as `clip_quad`'s
orchestration before it: `fill_quad`'s top-level dual-edge scanline
walker (the `goto`-based state machine that drives `fill_slope`/
`fill_line` across a whole quad's 4 vertices) and its embedded
wireframe-line special case (a degenerate two-distinct-point "quad"
rasterized as a Liang-Barsky-clipped Bresenham line instead — used for
wireframe primitives like Star Wars Arcade's target box). Both are
substantial enough for their own increment. The full `push_object`
vertex-stream traversal remains blocked on real ROM data regardless.

Verified with 3 new `Framebuffer` unit tests (`framebuffer_test.cpp`) and
7 new rasterizer unit tests in `geometrizer_test.cpp`, including the two
quirk-confirming cases above (152 unit tests total project-wide, 827
assertions, zero warnings under `-Wall -Wextra -Wpedantic`).

## fill_quad's scanline-walking orchestration and wireframe special case implemented

Phase 6 work continues, and completes the pure-math/pure-pixel side of
the rasterizer: `fill_quad` (the dual-edge scanline walker deferred last
increment) and `draw_wireframe_line` (its embedded degenerate-quad
special case) are now implemented, confirmed against the reference
functions of the same name. `Quad` gained a `col` field (matching
`quad_t::col`, propagated through `clip_quad`'s sub-quads alongside `z`,
which turned out to need the same treatment on closer reading of
`fclip_push_quad_next`'s `quad_t cquad(q.col, q.z, ...)` — missed when
`z` was added, since `col` didn't exist on `Quad` yet at that point).

Design notes and confirmed findings:

- The reference decides between two strategies per quad: if exactly 2
  distinct screen vertices appear among its 4 points (two coincident
  vertex pairs A,A,B,B — the same convention `clip_quad` already uses to
  represent a triangle, just with a third repeat), it's a degenerate
  "wireframe" primitive drawn as a Bresenham line (`draw_wireframe_line`)
  instead of a filled polygon — this keeps near-horizontal wires (e.g.
  Star Wars Arcade's target box) from collapsing to one pixel per row
  under the scanline filler. Otherwise, it decomposes the quad into flat
  scanline trapezoids via a dual-edge walk from the topmost vertex to the
  bottommost, driving `fill_slope`/`fill_line` across them.
- `draw_wireframe_line` first clips its endpoints against the viewport
  with Liang-Barsky (confirmed necessary by the reference's own comment:
  an unclipped, off-screen endpoint from a degenerate projection can be
  `inf`/`NaN`, which truncates to `INT32_MIN` on x86 — wingwar hung
  during boot walking a ~2^31-pixel line before this clip was added),
  then rasterizes with integer Bresenham, with a belt-and-suspenders
  per-pixel bounds check against the actual framebuffer dimensions on top
  of the clip (guarding against floating-point rounding right at the clip
  boundary). Unlike the reference (a file-local `static` function, not a
  class method), this project exposes it as a public free function, kept
  consistent with the general preference here for testing each rasterizer
  primitive directly rather than only indirectly through `fill_quad`.
- `fill_quad`'s pmin/pmax-then-dual-edge-walk needed one real, confirmed
  translation decision: the reference's screen coordinates
  (`point_t::s.x`/`s.y`) are `int32_t` fields that `project_point`
  assigns its float result into directly, so they're truncated to
  integer pixels *at projection time*, before `fill_quad` ever sees them
  and left-shifts them into `kFracShift` fixed-point. This project's
  `Point::sx`/`sy` stayed `float` (already tested and confirmed exact),
  so `fill_quad` truncates at its own boundary instead of inside
  `project_point`/`project_point_direct` — functionally identical, since
  nothing observes a projected point's fractional screen coordinate in
  between, but without touching (and having to re-verify) two
  already-solid functions.
- `fill_quad` also replicates the reference's negative-`col` handling:
  `color = -1-color` when `col < 0`. In the reference this is paired with
  a debug log line (not implemented here, since it's a log statement with
  no rendering effect); the color-unwrap arithmetic itself, which *does*
  affect the rendered pixels, is replicated and tested.
- The reference's `goto startup` (used to share initialization logic
  between the loop's first iteration and every subsequent vertex
  transition) is implemented here as the same block written out twice
  instead — identical control flow, no `goto`.

Verified with 9 new unit tests: 4 for `draw_wireframe_line` (an exact
diagonal, a segment entirely clipped away, a segment Liang-Barsky-clips
partially, and moiré dithering) and 5 for `fill_quad` (a rectangle
exercising the colinear-top-edge skip and the equal-y loop branch, the
degenerate flat-quad single-`fill_line` path, wireframe dispatch with
`kMoireFlag` masking, the negative-`col` unwrap, and a viewport-cull early
return). 161 unit tests total project-wide, 1185 assertions, zero
warnings under `-Wall -Wextra -Wpedantic`.

**What remains in Phase 6**: only the `push_object` vertex-stream
traversal that reads real per-game polygon/texture/light-mode/color data
from ROM and calls into everything implemented so far — every pipeline
stage itself (transform, project, clip, light, sort, rasterize) is now
done as pure, ROM-independent, unit-tested math. That traversal is
blocked on real ROM data regardless of implementation order.

## Not yet verified (do not treat as fact)

- Screen resolution/refresh rate and how the 2D tile/sprite layer
  (315-5292) composites with this 3D framebuffer — still open.
- The earlier, unconfirmed "Sega 837-7894 171-6080D VIDEO GPU" single-chip
  claim from a general web search still doesn't match the five-ASIC
  breakdown in source and should still be disregarded.
