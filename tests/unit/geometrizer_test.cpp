#include "doctest.h"
#include "video/geometrizer.h"

using namespace model1::video;

namespace {
bool approx(float a, float b, float epsilon = 1e-4f) { return (a > b ? a - b : b - a) < epsilon; }
} // namespace

TEST_CASE("init_translation_matrix produces an identity transform") {
    View view;
    view.init_translation_matrix();

    Point p{1, 2, 3};
    view.transform_point(p);

    CHECK(approx(p.x, 1));
    CHECK(approx(p.y, 2));
    CHECK(approx(p.z, 3));
}

TEST_CASE("A translation-only matrix offsets the point without rotating it") {
    View view;
    // Identity 3x3 (columns [1,0,0]/[0,1,0]/[0,0,1]) plus a (10,20,30) offset.
    view.set_translation_matrix({1, 0, 0, 0, 1, 0, 0, 0, 1, 10, 20, 30});

    Point p{1, 2, 3};
    view.transform_point(p);

    CHECK(approx(p.x, 11));
    CHECK(approx(p.y, 22));
    CHECK(approx(p.z, 33));
}

TEST_CASE("A 90-degree rotation about Y maps (x,y,z) to (z,y,-x)") {
    // Standard right-handed Y-up rotation-by-90-about-Y: (x,y,z) -> (z,y,-x).
    // Confirmed by working out transform_point's column-major indexing by
    // hand (see geometrizer.h's comment on set_translation_matrix) rather
    // than guessing the layout.
    View view;
    view.set_translation_matrix({0, 0, -1, 0, 1, 0, 1, 0, 0, 0, 0, 0});

    Point p1{1, 0, 0};
    view.transform_point(p1);
    CHECK(approx(p1.x, 0));
    CHECK(approx(p1.y, 0));
    CHECK(approx(p1.z, -1));

    Point p2{0, 0, 1};
    view.transform_point(p2);
    CHECK(approx(p2.x, 1));
    CHECK(approx(p2.y, 0));
    CHECK(approx(p2.z, 0));

    Point p3{0, 5, 0}; // Y is unaffected by a pure Y-axis rotation
    view.transform_point(p3);
    CHECK(approx(p3.y, 5));
}

TEST_CASE("project_point applies perspective divide, viewport center, zoom, and view translation") {
    View view;
    view.set_viewport(160, 120, 0, 0, 0, 0); // only xc/yc matter at this pipeline stage
    view.set_zoom(100, 100);
    view.set_view_translation(5, -5);

    Point p{20, 10, 2}; // xx = 20/2 = 10, yy = 10/2 = 5

    view.project_point(p);

    // sx = xc + (xx*zoomx + viewx) = 160 + (10*100 + 5) = 1165
    // sy = yc - (yy*zoomy + viewy) = 120 - (5*100 + (-5)) = 120 - 495 = -375
    CHECK(approx(p.xx, 10));
    CHECK(approx(p.yy, 5));
    CHECK(approx(p.sx, 1165));
    CHECK(approx(p.sy, -375));
}

TEST_CASE("project_point_direct skips the perspective divide entirely") {
    View view;
    view.set_viewport(160, 120, 0, 0, 0, 0);

    Point p{20, 10, 2}; // z would matter for project_point, but not here

    view.project_point_direct(p);

    CHECK(approx(p.xx, 20)); // == p.x, no division by z
    CHECK(approx(p.yy, 10));
    CHECK(approx(p.sx, 180)); // xc + xx = 160 + 20
    CHECK(approx(p.sy, 110)); // yc - yy = 120 - 10
}

namespace {
// A symmetric frustum with unit slopes: viewport clip bounds [-100,100] at
// zoom 100 give a_left=-1, a_right=1, a_bottom=1, a_top=-1, i.e. the frustum
// is exactly -z <= x <= z, -z <= y <= z for z > 0 -- worked out by hand from
// recompute_frustum's formula, not just re-running the implementation.
View make_unit_slope_view() {
    View view;
    view.set_viewport(0, 0, -100, 100, -100, 100);
    view.set_zoom(100, 100);
    view.set_view_translation(0, 0);
    return view;
}
} // namespace

TEST_CASE("is_clipped_right/left are true outside the unit-slope frustum, false inside") {
    View view = make_unit_slope_view();

    CHECK(view.is_clipped_right(Point{10, 0, 5})); // x(10) > z(5)
    CHECK_FALSE(view.is_clipped_right(Point{3, 0, 5})); // x(3) <= z(5)

    CHECK(view.is_clipped_left(Point{-10, 0, 5})); // x(-10) < -z(-5)
    CHECK_FALSE(view.is_clipped_left(Point{-3, 0, 5})); // x(-3) >= -z(-5)
}

TEST_CASE("is_clipped_bottom/top are true outside the unit-slope frustum, false inside") {
    View view = make_unit_slope_view();

    CHECK(view.is_clipped_bottom(Point{0, 10, 5})); // y(10) > z(5)
    CHECK_FALSE(view.is_clipped_bottom(Point{0, 3, 5})); // y(3) <= z(5)

    CHECK(view.is_clipped_top(Point{0, -10, 5})); // y(-10) < -z(-5)
    CHECK_FALSE(view.is_clipped_top(Point{0, -3, 5})); // y(-3) >= -z(-5)
}

TEST_CASE("clip_right finds the segment/plane intersection and projects it") {
    View view = make_unit_slope_view();
    // p1 inside (x=3,z=5), p2 outside (x=10,z=5); the x=z=5 plane crossing
    // is at x=5 by similar triangles since both points share z=5.
    Point p1{3, 0, 5};
    Point p2{10, 0, 5};

    Point hit = view.clip_right(p1, p2);

    CHECK(approx(hit.x, 5));
    CHECK(approx(hit.y, 0));
    CHECK(approx(hit.z, 5));
    // project_point: xx=x/z=1, yy=0, sx=xc+(xx*zoomx+viewx)=0+100=100, sy=0
    CHECK(approx(hit.sx, 100));
    CHECK(approx(hit.sy, 0));
}

TEST_CASE("clip_bottom finds the segment/plane intersection and projects it") {
    View view = make_unit_slope_view();
    // p1 inside (y=3,z=5), p2 outside (y=10,z=5); crossing at y=z=5.
    Point p1{0, 3, 5};
    Point p2{0, 10, 5};

    Point hit = view.clip_bottom(p1, p2);

    CHECK(approx(hit.x, 0));
    CHECK(approx(hit.y, 5));
    CHECK(approx(hit.z, 5));
    // project_point: xx=0, yy=y/z=1, sx=0, sy=yc-(yy*zoomy+viewy)=0-100=-100
    CHECK(approx(hit.sx, 0));
    CHECK(approx(hit.sy, -100));
}

TEST_CASE("clip_quad passes a fully-inside quad through all 4 planes unchanged") {
    View view = make_unit_slope_view();
    Quad quad{{Point{1, 1, 5}, Point{-1, 1, 5}, Point{-1, -1, 5}, Point{1, -1, 5}}};

    std::vector<Quad> result = view.clip_quad(quad);

    REQUIRE(result.size() == 1);
    for (int i = 0; i < 4; ++i) {
        CHECK(approx(result[0].points[i].x, quad.points[i].x));
        CHECK(approx(result[0].points[i].y, quad.points[i].y));
        CHECK(approx(result[0].points[i].z, quad.points[i].z));
    }
}

TEST_CASE("clip_quad drops a quad that is fully outside a single plane") {
    View view = make_unit_slope_view();
    // All 4 vertices have x(100) > z(5): fully outside the right plane,
    // while staying inside bottom/top/left (y=0, x=100 > -z).
    Quad quad{{Point{100, 0, 5}, Point{100, 1, 5}, Point{100, -1, 5}, Point{100, 2, 5}}};

    std::vector<Quad> result = view.clip_quad(quad);

    CHECK(result.empty());
}

TEST_CASE("clip_quad splits a quad with one vertex outside into a quad and a triangle") {
    View view = make_unit_slope_view();
    // p0 sticks out past the right plane (x=10 > z=5); p1/p2/p3 are inside
    // every plane. Expected intersection points worked out by hand: since
    // p0/p1 and p3/p0 each share z=5 with their partner, the crossing of
    // the x=z=5 plane falls at the arithmetic-mean point in both cases.
    Point p0{10, 0, 5};
    Point p1{0, -3, 5};
    Point p2{-3, 0, 5};
    Point p3{0, 3, 5};
    Quad quad{{p0, p1, p2, p3}, /*z*/ 42.0f, /*col*/ 7};

    std::vector<Quad> result = view.clip_quad(quad);

    REQUIRE(result.size() == 2);
    // Both sub-quads must carry forward the parent's sort key and color.
    CHECK(approx(result[0].z, 42));
    CHECK(approx(result[1].z, 42));
    CHECK(result[0].col == 7);
    CHECK(result[1].col == 7);

    // First result: (pi1, p1, p2, p3) where pi1 = crossing of p0->p1.
    const Quad& quad1 = result[0];
    CHECK(approx(quad1.points[0].x, 5));
    CHECK(approx(quad1.points[0].y, -1.5f));
    CHECK(approx(quad1.points[0].z, 5));
    CHECK(approx(quad1.points[0].sx, 100));
    CHECK(approx(quad1.points[0].sy, 30));
    CHECK(approx(quad1.points[1].x, p1.x));
    CHECK(approx(quad1.points[1].y, p1.y));
    CHECK(approx(quad1.points[2].x, p2.x));
    CHECK(approx(quad1.points[2].y, p2.y));
    CHECK(approx(quad1.points[3].x, p3.x));
    CHECK(approx(quad1.points[3].y, p3.y));

    // Second result: (p3, pi2, pi1, pi1) where pi2 = crossing of p3->p0.
    const Quad& quad2 = result[1];
    CHECK(approx(quad2.points[0].x, p3.x));
    CHECK(approx(quad2.points[0].y, p3.y));
    CHECK(approx(quad2.points[1].x, 5));
    CHECK(approx(quad2.points[1].y, 1.5f));
    CHECK(approx(quad2.points[1].sx, 100));
    CHECK(approx(quad2.points[1].sy, -30));
    CHECK(approx(quad2.points[2].x, 5));
    CHECK(approx(quad2.points[2].y, -1.5f));
    CHECK(approx(quad2.points[3].x, 5));
    CHECK(approx(quad2.points[3].y, -1.5f));
}

TEST_CASE("transform_vector applies only the rotation/scale part, ignoring translation") {
    View view;
    view.set_translation_matrix({1, 0, 0, 0, 1, 0, 0, 0, 1, 10, 20, 30});

    Vec3 v{1, 2, 3};
    view.transform_vector(v);

    CHECK(approx(v.x, 1));
    CHECK(approx(v.y, 2));
    CHECK(approx(v.z, 3));
}

TEST_CASE("transform_vector applies the same rotation as transform_point") {
    View view;
    // Same 90-degree-about-Y matrix as the transform_point test.
    view.set_translation_matrix({0, 0, -1, 0, 1, 0, 1, 0, 0, 0, 0, 0});

    Vec3 v{1, 0, 0};
    view.transform_vector(v);

    CHECK(approx(v.x, 0));
    CHECK(approx(v.y, 0));
    CHECK(approx(v.z, -1));
}

TEST_CASE("view_determinant matches the reference's scalar-triple-product formula") {
    // Hand-computed: e1=p2-p1=(-1,1,0), e2=p3-p1=(-1,0,1);
    // p1.(e1 x e2) = 1*(1*1-0*0) + 0*(0*-1-1*-1) + 0*(-1*0-(-1)*1) = 1.
    Point p1{1, 0, 0};
    Point p2{0, 1, 0};
    Point p3{0, 0, 1};

    CHECK(approx(view_determinant(p1, p2, p3), 1));
}

TEST_CASE("compute_lighting combines ambient, normalized-direction diffuse dot, and specular") {
    View view;
    view.set_light_direction(3, 4, 0); // normalizes to (0.6, 0.8, 0)
    view.set_light_param(0, /*diffuse*/ 0.5f, /*ambient*/ 0.1f, /*specular*/ 0.0f, /*power*/ 0);

    Vec3 normal{0.6f, 0.8f, 0.0f}; // already unit, parallel to the light

    // dot(normal, light) = 0.6*0.6 + 0.8*0.8 = 1.0 -- would be 5.0 if
    // set_light_direction had not normalized (3,4,0) first.
    // ln = ambient(0.1) + diffuse(0.5)*max(0,1.0) + specular(0) = 0.6
    CHECK(approx(view.compute_lighting(normal, 0), 0.6f));
}

TEST_CASE("compute_specular follows the reference's non-continuous power-of-two selection") {
    View view;
    view.set_light_direction(0, 0, -1); // light.z = -1
    view.set_specular_enabled(true);

    Vec3 normal{0, 0, 1}; // normal.z = 1
    const float diffuse = 1.0f;
    // s = 2*diffuse*normal.z - light.z = 2*1*1 - (-1) = 3

    view.set_light_param(0, 0, 0, 0.001f, 1); // power 1 -> s^1
    CHECK(approx(view.compute_specular(normal, diffuse, 0), 3.0f * 0.001f));

    view.set_light_param(0, 0, 0, 0.001f, 2); // power 2 or 3 -> s^2
    CHECK(approx(view.compute_specular(normal, diffuse, 0), 9.0f * 0.001f));

    view.set_light_param(0, 0, 0, 0.001f, 4); // power 4..6 -> s^4
    CHECK(approx(view.compute_specular(normal, diffuse, 0), 81.0f * 0.001f));

    view.set_light_param(0, 0, 0, 0.001f, 7); // power 7+ -> s^8, capped at 1
    CHECK(approx(view.compute_specular(normal, diffuse, 0), 1.0f));
}

TEST_CASE("compute_specular is zero when disabled, unset, or facing away") {
    View view;
    view.set_light_direction(0, 0, -1);
    Vec3 normal{0, 0, 1};

    view.set_light_param(0, 0, 0, 0.5f, 2);
    CHECK(approx(view.compute_specular(normal, 1.0f, 0), 0.0f)); // spec disabled by default

    view.set_specular_enabled(true);
    CHECK(approx(view.compute_specular(normal, -1.0f, 0), 0.0f)); // s <= 0 (facing away)

    view.set_light_param(1, 0, 0, 0.0f, 2); // specular scale 0
    CHECK(approx(view.compute_specular(normal, 1.0f, 1), 0.0f));

    view.set_light_param(2, 0, 0, 0.5f, 0); // power 0
    CHECK(approx(view.compute_specular(normal, 1.0f, 2), 0.0f));
}

TEST_CASE("Zoom of zero collapses the projected offset to the view translation alone") {
    View view;
    view.set_viewport(0, 0, 0, 0, 0, 0);
    view.set_zoom(0, 0);
    view.set_view_translation(7, 3);

    Point p{100, 100, 1};
    view.project_point(p);

    CHECK(approx(p.sx, 7));  // xc(0) + (xx*0 + 7)
    CHECK(approx(p.sy, -3)); // yc(0) - (yy*0 + 3)
}

TEST_CASE("sort_quads orders quads back-to-front (descending z)") {
    std::vector<Quad> quads = {
        Quad{{}, 10.0f},
        Quad{{}, 50.0f},
        Quad{{}, 30.0f},
    };

    sort_quads(quads);

    CHECK(approx(quads[0].z, 50));
    CHECK(approx(quads[1].z, 30));
    CHECK(approx(quads[2].z, 10));
}

TEST_CASE("sort_quads breaks ties by preserving original relative order") {
    // Tag each quad's identity via a vertex field (x), since z alone
    // can't distinguish otherwise-equal quads -- this is exactly what the
    // reference's pointer-position tiebreak is for.
    std::vector<Quad> quads = {
        Quad{{Point{1, 0, 0}}, 5.0f},
        Quad{{Point{2, 0, 0}}, 5.0f},
        Quad{{Point{3, 0, 0}}, 5.0f},
    };

    sort_quads(quads);

    CHECK(approx(quads[0].points[0].x, 1));
    CHECK(approx(quads[1].points[0].x, 2));
    CHECK(approx(quads[2].points[0].x, 3));
}

TEST_CASE("draw_hline fills an inclusive horizontal span") {
    Framebuffer fb(10, 5);
    draw_hline(fb, 2, 5, 3, 0xff0000);

    for (int x = 0; x < 10; ++x) {
        uint32_t expected = (x >= 2 && x <= 5) ? 0xff0000u : 0u;
        CHECK(fb.pixel(x, 3) == expected);
    }
}

TEST_CASE("draw_hline_moired writes only where (x^y) is even") {
    Framebuffer fb(6, 1);
    draw_hline_moired(fb, 0, 5, 0, 0xabcdef);

    // y=0, so (x^0)&1 == x&1: even x written, odd x left untouched.
    for (int x = 0; x < 6; ++x) {
        uint32_t expected = (x % 2 == 0) ? 0xabcdefu : 0u;
        CHECK(fb.pixel(x, 0) == expected);
    }
}

TEST_CASE("fill_line clips a span to the viewport's x bounds and skips out-of-range rows") {
    View view;
    view.set_viewport(0, 0, 2, 8, 1, 6); // clip x:[2,8] y:[1,6]

    Framebuffer fb(20, 10);
    fill_line(fb, view, 0x123456, 3, (-5) << kFracShift, 20 << kFracShift);
    for (int x = 0; x < 20; ++x) {
        uint32_t expected = (x >= 2 && x <= 8) ? 0x123456u : 0u;
        CHECK(fb.pixel(x, 3) == expected);
    }

    Framebuffer fb2(20, 10);
    fill_line(fb2, view, 0x123456, 0, 2 << kFracShift, 8 << kFracShift); // y=0 < clip y1=1
    for (int x = 0; x < 20; ++x) CHECK(fb2.pixel(x, 0) == 0);
}

TEST_CASE("fill_slope draws a vertical-edged rectangle across its scanline range") {
    View view;
    view.set_viewport(0, 0, 0, 19, 0, 9); // wide-open clip bounds

    Framebuffer fb(20, 10);
    int32_t nx1 = 0, nx2 = 0;
    fill_slope(fb, view, 0xff, 3 << kFracShift, 7 << kFracShift, 0, 0, 2, 5, &nx1, &nx2);

    // Rows 2,3,4 drawn (y1=2 up to but excluding y2=5); row 5 untouched.
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 20; ++x) {
            const bool in_row = (y >= 2 && y < 5);
            uint32_t expected = (in_row && x >= 3 && x <= 7) ? 0xffu : 0u;
            CHECK(fb.pixel(x, y) == expected);
        }
    }
    CHECK(nx1 == 3 << kFracShift); // slope 0 -- unchanged
    CHECK(nx2 == 7 << kFracShift);
}

TEST_CASE("fill_slope leaves nx1/nx2 untouched when y1 is already past the viewport bottom") {
    View view;
    view.set_viewport(0, 0, 0, 9, 0, 5); // clip y2=5

    Framebuffer fb(10, 10);
    int32_t nx1 = 111, nx2 = 222;
    fill_slope(fb, view, 0xff, 0, 0, 0, 0, 6, 8, &nx1, &nx2); // y1=6 > clip y2=5

    CHECK(nx1 == 111);
    CHECK(nx2 == 222);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) CHECK(fb.pixel(x, y) == 0);
}

TEST_CASE("fill_slope extrapolates nx1/nx2 without drawing when the segment ends above the viewport") {
    View view;
    view.set_viewport(0, 0, 0, 9, 5, 9); // clip y1=5 (top of clip range)

    Framebuffer fb(10, 10);
    int32_t nx1 = 0, nx2 = 0;
    // y1=0, y2=3, both <= clip.y1(5): purely extrapolated, delta=3.
    fill_slope(fb, view, 0xff, 1 << kFracShift, 2 << kFracShift, 1 << kFracShift, 2 << kFracShift, 0, 3, &nx1,
               &nx2);

    CHECK(nx1 == (1 << kFracShift) + 3 * (1 << kFracShift));
    CHECK(nx2 == (2 << kFracShift) + 3 * (2 << kFracShift));
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) CHECK(fb.pixel(x, y) == 0);
}

TEST_CASE("fill_slope's internal left/right swap preserves nx1/nx2 correspondence to the original edges") {
    View view;
    view.set_viewport(0, 0, -1000, 1000, 0, 10); // wide open

    Framebuffer fb(700, 10);
    int32_t nx1 = 0, nx2 = 0;
    // x1(500) > x2(100) triggers the internal left/right swap.
    fill_slope(fb, view, 0xff, 500 << kFracShift, 100 << kFracShift, 1 << kFracShift, 2 << kFracShift, 0, 3, &nx1,
               &nx2);

    // nx1 must still track the ORIGINAL (x1=500, sl1=1) edge: 500+3*1=503.
    CHECK(nx1 == 503 << kFracShift);
    // nx2 must still track the ORIGINAL (x2=100, sl2=2) edge: 100+3*2=106.
    CHECK(nx2 == 106 << kFracShift);

    // Row 0 spans [100,500], row 1 [102,501], row 2 [104,502].
    CHECK(fb.pixel(100, 0) == 0xffu);
    CHECK(fb.pixel(500, 0) == 0xffu);
    CHECK(fb.pixel(99, 0) == 0);
    CHECK(fb.pixel(102, 1) == 0xffu);
    CHECK(fb.pixel(501, 1) == 0xffu);
    CHECK(fb.pixel(104, 2) == 0xffu);
    CHECK(fb.pixel(502, 2) == 0xffu);
}

TEST_CASE("draw_wireframe_line draws an exact diagonal via Bresenham") {
    ClipRect clip{0, 9, 0, 9};
    Framebuffer fb(10, 10);

    draw_wireframe_line(fb, clip, 0, 0, 3, 3, 0xff, false);

    CHECK(fb.pixel(0, 0) == 0xffu);
    CHECK(fb.pixel(1, 1) == 0xffu);
    CHECK(fb.pixel(2, 2) == 0xffu);
    CHECK(fb.pixel(3, 3) == 0xffu);
    CHECK(fb.pixel(1, 0) == 0); // off the diagonal
}

TEST_CASE("draw_wireframe_line draws nothing for a segment entirely outside the clip rect") {
    ClipRect clip{0, 9, 0, 9};
    Framebuffer fb(10, 10);

    // Hand-derived via Liang-Barsky: both endpoints have x<0 throughout
    // the segment's parametrization, so t0 ends up > t1.
    draw_wireframe_line(fb, clip, -5, -5, -1, -1, 0xff, false);

    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) CHECK(fb.pixel(x, y) == 0);
}

TEST_CASE("draw_wireframe_line clips a partially-out-of-range segment to the clip rect") {
    ClipRect clip{0, 9, 0, 9};
    Framebuffer fb(10, 10);

    // Hand-derived via Liang-Barsky: (-2,5)->(5,5) clips to (0,5)->(5,5)
    // (t0=2/7, t1 unchanged at 1).
    draw_wireframe_line(fb, clip, -2, 5, 5, 5, 0xff, false);

    for (int x = 0; x <= 5; ++x) CHECK(fb.pixel(x, 5) == 0xffu);
    CHECK(fb.pixel(6, 5) == 0);
    for (int x = 0; x < 10; ++x) CHECK(fb.pixel(x, 4) == 0);
}

TEST_CASE("draw_wireframe_line dithers with the moire pattern when requested") {
    ClipRect clip{0, 9, 0, 9};
    Framebuffer fb(10, 10);

    draw_wireframe_line(fb, clip, 0, 0, 5, 0, 0xff, true);

    // y=0 (even): (x^0)&1 == x&1, so even x are drawn, odd x are skipped.
    for (int x = 0; x <= 5; ++x) {
        uint32_t expected = (x % 2 == 0) ? 0xffu : 0u;
        CHECK(fb.pixel(x, 0) == expected);
    }
}

TEST_CASE("fill_quad rasterizes a rectangle, including its colinear top/bottom edges") {
    View view;
    view.set_viewport(0, 0, 0, 9, 0, 9); // wide-open clip, matches the 10x10 framebuffer

    Quad quad{{Point{.sx = 2, .sy = 1}, Point{.sx = 6, .sy = 1}, Point{.sx = 6, .sy = 4}, Point{.sx = 2, .sy = 4}},
              /*z*/ 0, /*col*/ 0x00ff00};

    Framebuffer fb(10, 10);
    fill_quad(fb, view, quad);

    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            const bool inside = (y >= 1 && y <= 4 && x >= 2 && x <= 6);
            uint32_t expected = inside ? 0x00ff00u : 0u;
            CHECK(fb.pixel(x, y) == expected);
        }
    }
}

TEST_CASE("fill_quad draws a single scanline for a degenerate flat (equal-y) quad") {
    View view;
    view.set_viewport(0, 0, 0, 9, 0, 9);

    // 4 distinct x's, all at y=5 -- exercises the cury==limy path (a
    // single fill_line spanning the min/max x among all 4 vertices), not
    // the wireframe path (which needs exactly 2 distinct points).
    Quad quad{{Point{.sx = 2, .sy = 5}, Point{.sx = 4, .sy = 5}, Point{.sx = 6, .sy = 5}, Point{.sx = 3, .sy = 5}},
              /*z*/ 0, /*col*/ 0x123456};

    Framebuffer fb(10, 10);
    fill_quad(fb, view, quad);

    for (int x = 2; x <= 6; ++x) CHECK(fb.pixel(x, 5) == 0x123456u);
    CHECK(fb.pixel(1, 5) == 0);
    CHECK(fb.pixel(7, 5) == 0);
    for (int x = 0; x < 10; ++x) CHECK(fb.pixel(x, 4) == 0);
}

TEST_CASE("fill_quad dispatches degenerate 2-point quads to the wireframe line, masking kMoireFlag") {
    View view;
    view.set_viewport(0, 0, 0, 9, 0, 9);

    // Two coincident vertex pairs (1,5)/(1,5) and (6,5)/(6,5) -- the
    // reference's wireframe-quad convention.
    Quad quad{{Point{.sx = 1, .sy = 5}, Point{.sx = 1, .sy = 5}, Point{.sx = 6, .sy = 5}, Point{.sx = 6, .sy = 5}},
              /*z*/ 0, /*col*/ static_cast<int>(kMoireFlag) | 0xff};

    Framebuffer fb(10, 10);
    fill_quad(fb, view, quad);

    // y=5 is odd, so (x^5)&1==0 (drawn) exactly when x is also odd.
    CHECK(fb.pixel(1, 5) == 0xffu);
    CHECK(fb.pixel(2, 5) == 0);
    CHECK(fb.pixel(3, 5) == 0xffu);
    CHECK(fb.pixel(4, 5) == 0);
    CHECK(fb.pixel(5, 5) == 0xffu);
    CHECK(fb.pixel(6, 5) == 0);
}

TEST_CASE("fill_quad unwraps a negative col via -1-col, matching the reference's debug-marker encoding") {
    View view;
    view.set_viewport(0, 0, 0, 9, 0, 9);

    Quad quad{{Point{.sx = 2, .sy = 5}, Point{.sx = 4, .sy = 5}, Point{.sx = 6, .sy = 5}, Point{.sx = 3, .sy = 5}},
              /*z*/ 0, /*col*/ -256}; // -1-(-256) == 255 == 0xff

    Framebuffer fb(10, 10);
    fill_quad(fb, view, quad);

    for (int x = 2; x <= 6; ++x) CHECK(fb.pixel(x, 5) == 0xffu);
}

TEST_CASE("fill_quad culls a quad that lies entirely below the viewport's bottom clip bound") {
    View view;
    view.set_viewport(0, 0, 0, 9, -10, -5); // clip y2=-5, quad's cury=1 > -5

    Quad quad{{Point{.sx = 2, .sy = 1}, Point{.sx = 6, .sy = 1}, Point{.sx = 6, .sy = 4}, Point{.sx = 2, .sy = 4}},
              /*z*/ 0, /*col*/ 0xff};

    Framebuffer fb(10, 10);
    fill_quad(fb, view, quad);

    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) CHECK(fb.pixel(x, y) == 0);
}
