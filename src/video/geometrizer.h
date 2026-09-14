#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "video/framebuffer.h"

// The Geometrizer/rasterizer pipeline's transform and projection stage --
// "Role B" in docs/hardware-notes/02-tgp-coprocessor.md and
// docs/hardware-notes/05-video.md: object-space vertex transform and
// perspective projection, confirmed against MAME's model1_v.cpp
// (view_t::transform_point/project_point and friends), used as
// documentation only (see docs/planning/06-legal-and-assets.md). This is
// an original implementation, not copied code.
//
// Scope so far: transform + projection, the frustum-clipping plane
// tests/intersection formulas (confirmed against model1_v.cpp's
// fclip_isc_*/fclip_clip_* functions and view_t::recompute_frustum), the
// recursive 4-plane quad-clipping orchestration that drives them
// (confirmed against fclip_push_quad/fclip_push_quad_next -- an original
// non-recursive-on-the-reference's-buffers reimplementation of the same
// case analysis, returning clipped quads/triangles by value instead of
// writing into a preallocated arena), the diffuse/specular lighting +
// view-space backface-cull primitives (confirmed against
// view_t::transform_vector/set_light_direction/set_lightparam,
// model1_state::compute_specular, and model1_state::view_determinant),
// and the back-to-front quad sort (confirmed against quad_t::compare and
// sort_quads). Rasterization is now implemented in full: the per-scanline
// pixel-writing primitives (draw_hline/draw_hline_moired, fill_line,
// fill_slope) plus fill_quad's top-level dual-edge scanline-walking
// orchestration and its embedded wireframe-line special case
// (draw_wireframe_line), confirmed against the reference functions of the
// same name.
//
// Deliberately NOT yet implemented: the full push_object vertex-stream
// traversal that ties everything above to real per-game polygon/texture/
// light-mode/color data from ROM -- see docs/planning/05-roadmap.md
// Phase 6. Every stage that traversal would drive (transform, project,
// clip, light, sort, rasterize) is done; what's missing is the code that
// reads real display-list/polygon-ROM bytes and calls into it with real
// per-game data.
//
// Fixed-point convention: fill_line/fill_slope/fill_quad's x coordinates
// are integers in a 16.16 fixed-point format (kFracShift), matching the
// reference's FRAC_SHIFT -- this lets the polygon edge walker accumulate
// sub-pixel slope error across scanlines without drifting, exactly like
// the reference. y coordinates and the viewport clip bounds are plain
// integer pixels throughout.
namespace model1::video {

// 16.16 fixed-point shift used by fill_line/fill_slope's x coordinates.
// Confirmed against the reference's `enum { FRAC_SHIFT = 16 }`.
constexpr int kFracShift = 16;

// Set in a quad/line color's bit 24 to select the dithered fill variant
// (draw_hline_moired) instead of a solid fill. Confirmed against the
// reference's `enum { MOIRE = 0x01000000 }`; not yet produced by anything
// in this file since it's set per-quad by the still-unimplemented
// push_object traversal (see docs/hardware-notes/05-video.md).
constexpr uint32_t kMoireFlag = 0x01000000;

// A point as it moves through the pipeline: object-space in (x,y,z),
// view-space after transform_point, then xx/yy (post-divide intermediate)
// and sx/sy (final screen coordinates) after project_point. Matches the
// reference's `point_t` fields (renamed for clarity: its `s.x`/`s.y`
// become `sx`/`sy` here).
struct Point {
    float x = 0, y = 0, z = 0;
    float xx = 0, yy = 0;
    float sx = 0, sy = 0;
};

// Four vertices in order around the quad (matches the reference's
// quad_t::p[4]), plus a sort key `z` and a packed color `col` (matches
// quad_t::z/col -- both assigned externally by whatever builds the quad;
// `col` isn't computed by anything in this file yet, since that's the
// still-unimplemented push_object/lighting-to-color pipeline, but
// fill_quad now needs somewhere to read a color from, so it's carried
// here like the reference carries it). A triangle is represented, per the
// reference's own convention, by repeating one vertex twice (see
// clip_quad).
struct Quad {
    std::array<Point, 4> points{};
    float z = 0;
    int col = 0;
};

// A direction (face/vertex normal, or the light direction) -- unlike
// Point, has no position-in-pipeline-specific fields, matching the
// reference's plain glm::vec3 use for these.
struct Vec3 {
    float x = 0, y = 0, z = 0;
};

// One "light mode" slot's shading parameters. Matches the reference's
// lightparam_t; `power` selects how many times the specular term is
// squared (see View::compute_specular) rather than being a literal
// exponent.
struct LightParam {
    float ambient = 0;
    float diffuse = 0;
    float specular = 0;
    int power = 0;
};

// Screen-space viewport clip bounds, as plain integer pixels -- matches
// the reference's view_t::x1/x2/y1/y2 (declared as `int` there; stored as
// float on View like the rest of the viewport state and truncated here,
// mirroring how the reference's own set_viewport calls truncate the
// display list's float values into those int fields).
struct ClipRect {
    int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
};

// Signed volume of the tetrahedron formed by the origin and p1/p2/p3 --
// concretely, p1 . ((p2-p1) x (p3-p1)) -- confirmed against the
// reference's own formula (model1_state::view_determinant; notably NOT a
// view_t method, so it doesn't depend on any View state). Its one
// confirmed use (push_object's quad assembly) treats a positive result as
// "cull this face" for a view-space triangle -- that's the extent of the
// confirmed semantics; no further geometric interpretation is needed to
// implement or use it correctly.
float view_determinant(const Point& p1, const Point& p2, const Point& p3);

// Back-to-front painter's-algorithm sort, in place: farthest `z` first,
// nearest last, ties broken by preserving original relative order.
// Confirmed against quad_t::compare (descending-z comparator) and
// sort_quads/comp_quads: the reference sorts pointers into a flat array
// with qsort (not inherently stable) but its comparator explicitly
// tie-breaks equal-z quads by their pointer's position in that
// insertion-ordered array -- for insertion-ordered storage that is
// exactly a stable sort by original index, which std::stable_sort
// provides directly, so no manual tie-break is needed here. The
// reference's separate unsort_quads (restoring insertion order for the
// unsorted "direct"/already-2D-projected polygon draw pass -- see
// docs/hardware-notes/05-video.md) has no equivalent need here: since
// this function sorts a caller-supplied vector rather than reordering
// indices over shared arena storage, the caller's original vector is
// simply left untouched if they want the unsorted order too.
void sort_quads(std::vector<Quad>& quads);

// Draws a solid horizontal span [x1, x2] (inclusive) at row y. NOT
// clipped or bounds-checked -- confirmed against the reference's
// draw_hline: callers (fill_line/fill_slope) are responsible for having
// already clamped x1/x2/y into range.
void draw_hline(Framebuffer& fb, int x1, int x2, int y, uint32_t color);

// As draw_hline, but only writes pixels where (x^y) is even -- a checkerboard
// dither, confirmed against the reference's draw_hline_moired. Notably
// writes `color` through unmodified, kMoireFlag bit included -- the
// reference does this too (never masks the flag out of the stored pixel
// value before writing it to the bitmap), replicated exactly as a
// harmless-in-practice quirk rather than "fixed."
void draw_hline_moired(Framebuffer& fb, int x1, int x2, int y, uint32_t color);

// Draws a solid (or checkerboard-dithered, per kMoireFlag) line from
// (x1,y1) to (x2,y2) via integer Bresenham, first clipped to `clip` with
// Liang-Barsky (so an unclipped, off-screen endpoint -- which can be
// garbage: a degenerate projection yields inf/NaN, and float->int
// conversion of that turns into INT32_MIN on x86 hosts -- can't walk a
// ~2^31-pixel line). Confirmed against the reference's (file-local
// static, not a class method there) draw_wireframe_line; exposed as a
// public free function here, unlike the reference, since this project
// prefers testing each rasterizer primitive directly rather than only
// indirectly through fill_quad's wireframe-detection wrapper. Also
// bounds-checks each pixel against `fb`'s actual dimensions before
// writing -- confirmed the reference does this too, as a belt-and-
// suspenders guard against floating-point rounding at the clip boundary.
void draw_wireframe_line(Framebuffer& fb, const ClipRect& clip, int x1, int y1, int x2, int y2, uint32_t color,
                          bool moire);

class View;

// Draws one horizontal span of a flat-bottomed/flat-topped scanline
// (x1/x2 in kFracShift fixed-point, y in plain pixels), clipped to
// `view`'s viewport bounds, dispatching to draw_hline or
// draw_hline_moired per kMoireFlag. Confirmed against the reference's
// fill_line -- including a worth-flagging quirk: its "does this span
// overlap the viewport at all" pre-check uses `||` where an actual
// overlap test would need `&&`; since x1 <= x2 always holds, the `||`
// form is a tautology (always true) and never actually skips a line --
// the real work of not drawing out-of-view pixels is done entirely by
// the clamp that follows it. Replicated as-is rather than "fixed" to
// `&&`, matching this project's general policy of preserving confirmed
// reference quirks rather than silently correcting them.
void fill_line(Framebuffer& fb, const View& view, uint32_t color, int32_t y, int32_t x1, int32_t x2);

// Draws consecutive scanlines from y1 up to (but not including) y2 for a
// trapezoid edge pair (x1/sl1 the left-tracking edge, x2/sl2 the right-
// tracking edge, both x's in kFracShift fixed-point, slopes in
// fixed-point-per-scanline), clipped to `view`'s viewport bounds, and
// writes the edges' final x positions to *nx1/*nx2 for the caller to
// resume from on the next segment. Confirmed against the reference's
// fill_slope, including two subtleties worth preserving exactly:
//   - If y1 is already past the bottom of the viewport, returns without
//     touching *nx1/*nx2 at all (left at whatever the caller passed in).
//   - Internally it always walks the smaller-x edge as "left", swapping
//     its x1/x2/sl1/sl2 locals (and, crucially, swapping which of nx1/nx2
//     each output pointer targets right along with them) whenever the
//     caller's x1 > x2. This makes *nx1 always end up tracking the
//     progression of the caller's original (x1, sl1) edge and *nx2 the
//     caller's (x2, sl2) edge, regardless of which one was numerically
//     smaller and therefore walked as "left" internally -- callers can
//     rely on output-to-input correspondence by position, not by which
//     side of the polygon each edge happened to be on for this segment.
void fill_slope(Framebuffer& fb, const View& view, uint32_t color, int32_t x1, int32_t x2, int32_t sl1,
                 int32_t sl2, int32_t y1, int32_t y2, int32_t* nx1, int32_t* nx2);

// Rasterizes `quad`, dispatching to one of two strategies confirmed
// against the reference's fill_quad:
//   - If exactly 2 distinct screen vertices appear among the quad's 4
//     points (the reference's convention for a degenerate wireframe
//     "quad" built from two coincident vertex pairs A,A,B,B -- see
//     clip_quad's own triangle convention, which is exactly this with a
//     3rd repeat), draws it as a line via draw_wireframe_line instead of
//     a filled polygon, so near-horizontal wires keep their full span
//     instead of collapsing to one pixel per row.
//   - Otherwise, decomposes the quad into flat scanline trapezoids via a
//     dual-edge walk (starting at the topmost vertex, walking both
//     directions around the quad toward the bottommost one) and drives
//     fill_slope/fill_line across them.
// `quad.points[i].sx/sy` are truncated to integer pixels here, at the
// point they're read into fixed-point -- the reference truncates earlier
// (project_point assigns its float result directly into point_t::s.x/s.y,
// which are int32_t fields), but nothing observes a projected point's
// fractional screen coordinate between projection and rasterization, so
// truncating at this boundary instead is functionally identical without
// having to touch (and re-verify) project_point/project_point_direct,
// both already confirmed and tested as producing exact (float) screen
// coordinates. `quad.col`'s sign is also handled per the reference: a
// negative value is unwrapped via `-1-col` (the reference's debug-marker
// encoding, which only affects a log line there -- no log-equivalent is
// implemented here, only the color unwrap that survives it).
void fill_quad(Framebuffer& fb, const View& view, const Quad& quad);

// One "camera": the transform matrix, viewport, and zoom currently active
// while processing a display list. Matches the reference's `view_t`,
// minus fields belonging to later pipeline stages (lighting, frustum
// clipping bounds) not implemented yet.
class View {
public:
    // Sets the 3x3 rotation/scale part to identity and the translation
    // part to zero. Confirmed against view_t::init_translation_matrix.
    void init_translation_matrix();

    // `matrix` is 12 floats: a column-major 3x3 rotation/scale (indices
    // 0-8, i.e. columns [0,1,2]/[3,4,5]/[6,7,8]) followed by a 3-component
    // translation (indices 9-11). Confirmed against
    // view_t::set_translation_matrix and how transform_point indexes it.
    void set_translation_matrix(const std::array<float, 12>& matrix);

    // xl/xr/yb/yt are the viewport's clip bounds in screen space, used by
    // recompute_frustum (triggered here and by set_zoom/set_view_translation,
    // confirmed against the reference calling it from all three setters) to
    // derive the 4 frustum-plane slopes used by the is_clipped_*/clip_*
    // methods below.
    void set_viewport(float xcenter, float ycenter, float xl, float xr, float yb, float yt);
    void set_zoom(float x, float y);
    void set_view_translation(float x, float y);

    // The viewport's clip bounds truncated to integer pixels, for the
    // rasterizer stage's per-scanline clipping (fill_line/fill_slope).
    ClipRect clip_rect() const;

    // Object-space -> view-space. Confirmed against view_t::transform_point.
    //
    // The reference's formula also includes a view-offset (vxx/vyy/vzz)
    // and an additional yaw rotation (ayyc/ayys, applied to the transformed
    // X/Z after the main matrix). Confirmed these are DEAD in the current
    // reference: the only code that ever assigns them is wrapped in
    // `#if 0` (a compiled-out developer debug-camera control keyed to
    // keyboard input) -- no real game data path sets them. They therefore
    // always take their default values (vxx=vyy=vzz=0, effectively
    // ayyc=1/ayys=0), making that part of the formula a no-op for every
    // real game on this hardware. This class omits those terms entirely
    // rather than implementing dead code; if a target game is ever found
    // to need them after all, revisit with the exact reference formula
    // documented in docs/hardware-notes/09-geometrizer.md.
    void transform_point(Point& p) const;

    // Rotation-only transform (no translation, no view-offset/yaw terms --
    // those apply only to points, per transform_point) for face/vertex
    // normals. Confirmed against view_t::transform_vector: the same 3x3
    // matrix as transform_point, applied via dot products against its
    // rows in the reference's own (row1/row2/row3-named) formulation,
    // which is the same column-major layout transform_point uses.
    void transform_vector(Vec3& v) const;

    // View-space -> screen-space, with perspective divide. Confirmed
    // against view_t::project_point.
    void project_point(Point& p) const;

    // View-space -> screen-space, WITHOUT perspective divide (used for
    // "direct" / already-2D-projected polygons in the display list, per
    // opcode 0x02 -- see docs/hardware-notes/05-video.md). Confirmed
    // against view_t::project_point_direct.
    void project_point_direct(Point& p) const;

    // Frustum clipping (view-space, pre-projection): each plane test
    // compares y or x against z scaled by that plane's slope, rather than
    // projecting and comparing screen coordinates -- avoids a
    // divide-by-zero/sign flip for points at or behind the eye (z <= 0),
    // which a screen-space test would need to special-case. Confirmed
    // against the reference's fclip_isc_bottom/top/left/right: returns
    // true when the point is OUTSIDE (clipped), matching the reference's
    // own naming and sense exactly (easy to accidentally invert).
    bool is_clipped_bottom(const Point& p) const;
    bool is_clipped_top(const Point& p) const;
    bool is_clipped_left(const Point& p) const;
    bool is_clipped_right(const Point& p) const;

    // Computes the point where segment p1->p2 crosses the named plane,
    // then projects it (confirmed against the reference's fclip_clip_*,
    // which call project_point on the result as their last step -- these
    // intersection points come out of clipping already in screen space,
    // unlike a quad's original vertices which get projected separately).
    // Behavior is only meaningful when p1 and p2 are on opposite sides of
    // the plane; matches the reference's own lack of a guard for this.
    Point clip_bottom(const Point& p1, const Point& p2) const;
    Point clip_top(const Point& p1, const Point& p2) const;
    Point clip_left(const Point& p1, const Point& p2) const;
    Point clip_right(const Point& p1, const Point& p2) const;

    // Clips `quad` against all 4 frustum planes in turn (bottom, top, left,
    // right -- matching the reference's m_clipfn[0..3] order) and returns
    // the resulting quads/triangles (0, 1, or 2 of them). Vertices that
    // survive a plane unclipped are passed through unmodified (not
    // reprojected) -- only newly-created intersection points come back
    // already projected, per clip_bottom/top/left/right's own contract.
    // Confirmed against fclip_push_quad's full case analysis: which of a
    // quad's vertices are outside a given plane determines whether it
    // passes through untouched, is dropped entirely, or is split into one
    // quad, one triangle, one quad, or two triangles before recursing to
    // the next plane.
    std::vector<Quad> clip_quad(const Quad& quad) const;

    // Light direction, normalized on set. Confirmed against
    // view_t::set_light_direction (which uses glm::normalize -- no
    // zero-vector guard, replicated exactly rather than added).
    void set_light_direction(float x, float y, float z);

    // Per-light-mode shading parameters, up to 256 slots -- matches the
    // reference's `lightparams[256]`, indexed by a per-polygon light-mode
    // byte from the display list (see docs/hardware-notes/05-video.md's
    // opcode 0x06). Confirmed against view_t::set_lightparam.
    void set_light_param(int index, float diffuse, float ambient, float specular, int power);

    // Global specular on/off switch. Confirmed against view_t's
    // spec_enable field, set from display-list opcode 0x07 bit 0 (see
    // docs/hardware-notes/05-video.md) -- that display-list dispatch
    // itself is out of scope here, only the flag's effect on
    // compute_specular is implemented.
    void set_specular_enabled(bool enabled);

    // Specular term for a face/vertex normal already transformed into
    // view space, given its diffuse dot product (normal . light) and
    // light-mode index. Confirmed against model1_state::compute_specular:
    // 0 when specular is globally disabled or that light mode has no
    // specular power/scale; otherwise the reflected light vector's Z
    // component (2*diffuse*normal.z - light.z), zero-clamped, then raised
    // to a power of two selected by the power field (squared again at
    // power thresholds 2/4/7 -- so power 1->s^1, 2 or 3->s^2, 4..6->s^4,
    // 7+->s^8, matching the reference's own non-continuous selection
    // exactly rather than a generic pow()), scaled by the specular
    // coefficient and capped at 1.
    float compute_specular(const Vec3& normal, float diffuse, int light_mode) const;

    // Full diffuse+specular luminance term for a face: ambient +
    // diffuse_coefficient*max(0, N.L) + specular, confirmed against the
    // reference's exact combination at its one call site (`ln` in
    // push_object). Deliberately NOT clamped to [0,1] here -- that clamp,
    // and the conversion to an 8-bit value via the color-translation
    // table, belong to the still-unimplemented rasterization/color stage.
    float compute_lighting(const Vec3& normal, int light_mode) const;

private:
    void recompute_frustum();
    bool is_clipped(int level, const Point& p) const;
    Point clip(int level, const Point& p1, const Point& p2) const;
    void clip_quad_level(int level, const Quad& quad, std::vector<Quad>& out) const;

    std::array<float, 12> translation_{};
    float xc_ = 0, yc_ = 0;
    float x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
    float zoomx_ = 0, zoomy_ = 0;
    float viewx_ = 0, viewy_ = 0;
    float a_left_ = 0, a_right_ = 0, a_bottom_ = 0, a_top_ = 0;
    Vec3 light_{};
    std::array<LightParam, 256> light_params_{};
    bool spec_enabled_ = false;
};

} // namespace model1::video
