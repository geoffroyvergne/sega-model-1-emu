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

## Not yet verified (do not treat as fact)

- Screen resolution/refresh rate and how the 2D tile/sprite layer
  (315-5292) composites with this 3D framebuffer — still open.
- The earlier, unconfirmed "Sega 837-7894 171-6080D VIDEO GPU" single-chip
  claim from a general web search still doesn't match the five-ASIC
  breakdown in source and should still be disregarded.
