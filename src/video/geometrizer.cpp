#include "video/geometrizer.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace model1::video {

float view_determinant(const Point& p1, const Point& p2, const Point& p3) {
    const float x1 = p2.x - p1.x;
    const float y1 = p2.y - p1.y;
    const float z1 = p2.z - p1.z;
    const float x2 = p3.x - p1.x;
    const float y2 = p3.y - p1.y;
    const float z2 = p3.z - p1.z;
    return p1.x * (y1 * z2 - y2 * z1) + p1.y * (z1 * x2 - z2 * x1) + p1.z * (x1 * y2 - x2 * y1);
}

void View::init_translation_matrix() {
    translation_.fill(0.0f);
    translation_[0] = 1.0f;
    translation_[4] = 1.0f;
    translation_[8] = 1.0f;
}

void View::set_translation_matrix(const std::array<float, 12>& matrix) { translation_ = matrix; }

void View::set_viewport(float xcenter, float ycenter, float xl, float xr, float yb, float yt) {
    xc_ = xcenter;
    yc_ = ycenter;
    x1_ = xl;
    x2_ = xr;
    y1_ = yb;
    y2_ = yt;
    recompute_frustum();
}

void View::set_zoom(float x, float y) {
    zoomx_ = x;
    zoomy_ = y;
    recompute_frustum();
}

void View::set_view_translation(float x, float y) {
    viewx_ = x;
    viewy_ = y;
    recompute_frustum();
}

// Confirmed against view_t::recompute_frustum exactly.
void View::recompute_frustum() {
    a_left_ = (x1_ - xc_ - viewx_) / zoomx_;
    a_right_ = (x2_ - xc_ - viewx_) / zoomx_;
    a_bottom_ = (-y1_ + yc_ - viewy_) / zoomy_;
    a_top_ = (-y2_ + yc_ - viewy_) / zoomy_;
}

// Confirmed against view_t::transform_point, with the dead view-offset/yaw
// terms omitted -- see the header comment.
void View::transform_point(Point& p) const {
    const float qx = p.x, qy = p.y, qz = p.z;
    const auto& t = translation_;
    p.x = t[0] * qx + t[3] * qy + t[6] * qz + t[9];
    p.y = t[1] * qx + t[4] * qy + t[7] * qz + t[10];
    p.z = t[2] * qx + t[5] * qy + t[8] * qz + t[11];
}

// Confirmed against view_t::transform_vector: same 3x3 matrix as
// transform_point, no translation or view-offset/yaw terms.
void View::transform_vector(Vec3& v) const {
    const float qx = v.x, qy = v.y, qz = v.z;
    const auto& t = translation_;
    v.x = t[0] * qx + t[3] * qy + t[6] * qz;
    v.y = t[1] * qx + t[4] * qy + t[7] * qz;
    v.z = t[2] * qx + t[5] * qy + t[8] * qz;
}

void View::project_point(Point& p) const {
    p.xx = p.x / p.z;
    p.yy = p.y / p.z;
    p.sx = xc_ + (p.xx * zoomx_ + viewx_);
    p.sy = yc_ - (p.yy * zoomy_ + viewy_);
}

void View::project_point_direct(Point& p) const {
    p.xx = p.x;
    p.yy = p.y;
    p.sx = xc_ + p.xx;
    p.sy = yc_ - p.yy;
}

ClipRect View::clip_rect() const {
    return ClipRect{static_cast<int>(x1_), static_cast<int>(x2_), static_cast<int>(y1_), static_cast<int>(y2_)};
}

// Confirmed against fclip_isc_bottom/top/left/right.
bool View::is_clipped_bottom(const Point& p) const { return p.y > p.z * a_bottom_; }
bool View::is_clipped_top(const Point& p) const { return p.y < p.z * a_top_; }
bool View::is_clipped_left(const Point& p) const { return p.x < p.z * a_left_; }
bool View::is_clipped_right(const Point& p) const { return p.x > p.z * a_right_; }

// Confirmed against fclip_clip_bottom/top/left/right.
Point View::clip_bottom(const Point& p1, const Point& p2) const {
    const float t = (p2.z * a_bottom_ - p2.y) / ((p2.z - p1.z) * a_bottom_ - (p2.y - p1.y));
    Point result;
    result.x = p1.x * t + p2.x * (1 - t);
    result.y = p1.y * t + p2.y * (1 - t);
    result.z = p1.z * t + p2.z * (1 - t);
    project_point(result);
    return result;
}

Point View::clip_top(const Point& p1, const Point& p2) const {
    const float t = (p2.z * a_top_ - p2.y) / ((p2.z - p1.z) * a_top_ - (p2.y - p1.y));
    Point result;
    result.x = p1.x * t + p2.x * (1 - t);
    result.y = p1.y * t + p2.y * (1 - t);
    result.z = p1.z * t + p2.z * (1 - t);
    project_point(result);
    return result;
}

Point View::clip_left(const Point& p1, const Point& p2) const {
    const float t = (p2.z * a_left_ - p2.x) / ((p2.z - p1.z) * a_left_ - (p2.x - p1.x));
    Point result;
    result.x = p1.x * t + p2.x * (1 - t);
    result.y = p1.y * t + p2.y * (1 - t);
    result.z = p1.z * t + p2.z * (1 - t);
    project_point(result);
    return result;
}

Point View::clip_right(const Point& p1, const Point& p2) const {
    const float t = (p2.z * a_right_ - p2.x) / ((p2.z - p1.z) * a_right_ - (p2.x - p1.x));
    Point result;
    result.x = p1.x * t + p2.x * (1 - t);
    result.y = p1.y * t + p2.y * (1 - t);
    result.z = p1.z * t + p2.z * (1 - t);
    project_point(result);
    return result;
}

// Confirmed against view_t::set_light_direction (glm::normalize -- no
// zero-vector guard).
void View::set_light_direction(float x, float y, float z) {
    const float len = std::sqrt(x * x + y * y + z * z);
    light_ = Vec3{x / len, y / len, z / len};
}

// Confirmed against view_t::set_lightparam.
void View::set_light_param(int index, float diffuse, float ambient, float specular, int power) {
    LightParam& lp = light_params_[index];
    lp.ambient = ambient;
    lp.diffuse = diffuse;
    lp.specular = specular;
    lp.power = power;
}

void View::set_specular_enabled(bool enabled) { spec_enabled_ = enabled; }

// Confirmed against model1_state::compute_specular.
float View::compute_specular(const Vec3& normal, float diffuse, int light_mode) const {
    if (!spec_enabled_) return 0.0f;

    const LightParam& lp = light_params_[light_mode];
    if (lp.power == 0 || lp.specular <= 0.0f) return 0.0f;

    float s = (2.0f * diffuse * normal.z) - light_.z;
    if (s <= 0.0f) return 0.0f;
    if (lp.power >= 2) s *= s;
    if (lp.power >= 4) s *= s;
    if (lp.power >= 7) s *= s;
    return std::min(s * lp.specular, 1.0f);
}

// Confirmed against push_object's `ln` computation (its one call site).
float View::compute_lighting(const Vec3& normal, int light_mode) const {
    const float diffuse_dot = normal.x * light_.x + normal.y * light_.y + normal.z * light_.z;
    const LightParam& lp = light_params_[light_mode];
    const float spec = compute_specular(normal, diffuse_dot, light_mode);
    return lp.ambient + lp.diffuse * std::max(0.0f, diffuse_dot) + spec;
}

bool View::is_clipped(int level, const Point& p) const {
    switch (level) {
    case 0:
        return is_clipped_bottom(p);
    case 1:
        return is_clipped_top(p);
    case 2:
        return is_clipped_left(p);
    default:
        return is_clipped_right(p);
    }
}

Point View::clip(int level, const Point& p1, const Point& p2) const {
    switch (level) {
    case 0:
        return clip_bottom(p1, p2);
    case 1:
        return clip_top(p1, p2);
    case 2:
        return clip_left(p1, p2);
    default:
        return clip_right(p1, p2);
    }
}

// Confirmed against fclip_push_quad/fclip_push_quad_next.
void View::clip_quad_level(int level, const Quad& q, std::vector<Quad>& out) const {
    if (level == 4) {
        out.push_back(q);
        return;
    }

    bool is_out[4];
    for (int i = 0; i < 4; ++i) is_out[i] = is_clipped(level, q.points[i]);

    // No clipping at this plane -- pass through to the next one.
    if (!is_out[0] && !is_out[1] && !is_out[2] && !is_out[3]) {
        clip_quad_level(level + 1, q, out);
        return;
    }

    // Fully outside this plane -- the whole quad is dropped.
    if (is_out[0] && is_out[1] && is_out[2] && is_out[3]) {
        return;
    }

    // Find i such that point i is clipped and point i-1 (mod 4) isn't --
    // i.e. rotate so pt[0] is the first clipped-out vertex going around
    // and pt[3] is guaranteed to be inside.
    int i = 0;
    for (; i < 4; ++i) {
        if (is_out[i] && !is_out[(i - 1) & 3]) break;
    }

    Point pt[4];
    bool is_out2[4];
    for (int j = 0; j < 4; ++j) {
        pt[j] = q.points[(i + j) & 3];
        is_out2[j] = is_out[(i + j) & 3];
    }

    // pt[0] is clipped out and pt[3] isn't; the 4 remaining cases below
    // cover every possible combination of pt[1]/pt[2] being in or out.
    // Every new sub-quad carries forward the parent's sort key `z` and
    // color `col`, matching fclip_push_quad_next's
    // `quad_t cquad(q.col, q.z, ...)`.
    if (is_out2[1]) {
        if (is_out2[2]) {
            // pt 0,1,2 clipped out -- one triangle left.
            Point pi1 = clip(level, pt[2], pt[3]);
            Point pi2 = clip(level, pt[3], pt[0]);
            clip_quad_level(level + 1, Quad{{pi1, pt[3], pi2, pi2}, q.z, q.col}, out);
        } else {
            // pt 0,1 clipped out -- one quad left.
            Point pi1 = clip(level, pt[1], pt[2]);
            Point pi2 = clip(level, pt[3], pt[0]);
            clip_quad_level(level + 1, Quad{{pi1, pt[2], pt[3], pi2}, q.z, q.col}, out);
        }
    } else if (is_out2[2]) {
        // pt 0,2 clipped out (shouldn't happen for a convex quad) -- two
        // triangles.
        Point pi1 = clip(level, pt[0], pt[1]);
        Point pi2 = clip(level, pt[1], pt[2]);
        clip_quad_level(level + 1, Quad{{pi1, pt[1], pi2, pi2}, q.z, q.col}, out);
        Point pi3 = clip(level, pt[2], pt[3]);
        Point pi4 = clip(level, pt[3], pt[0]);
        clip_quad_level(level + 1, Quad{{pi3, pt[3], pi4, pi4}, q.z, q.col}, out);
    } else {
        // pt 0 clipped out only -- one pentagon left, split into a quad
        // and a triangle.
        Point pi1 = clip(level, pt[0], pt[1]);
        Point pi2 = clip(level, pt[3], pt[0]);
        clip_quad_level(level + 1, Quad{{pi1, pt[1], pt[2], pt[3]}, q.z, q.col}, out);
        clip_quad_level(level + 1, Quad{{pt[3], pi2, pi1, pi1}, q.z, q.col}, out);
    }
}

std::vector<Quad> View::clip_quad(const Quad& quad) const {
    std::vector<Quad> out;
    clip_quad_level(0, quad, out);
    return out;
}

// Confirmed against quad_t::compare/sort_quads -- see the header comment
// for why a plain stable_sort by descending z reproduces the reference's
// qsort-plus-pointer-tiebreak behavior exactly.
void sort_quads(std::vector<Quad>& quads) {
    std::stable_sort(quads.begin(), quads.end(), [](const Quad& a, const Quad& b) { return a.z > b.z; });
}

// Confirmed against the reference's draw_hline.
void draw_hline(Framebuffer& fb, int x1, int x2, int y, uint32_t color) {
    for (int x = x1; x <= x2; ++x) fb.set_pixel(x, y, color);
}

// Confirmed against the reference's draw_hline_moired.
void draw_hline_moired(Framebuffer& fb, int x1, int x2, int y, uint32_t color) {
    for (int x = x1; x <= x2; ++x) {
        if (!((x ^ y) & 1)) fb.set_pixel(x, y, color);
    }
}

// Confirmed against the reference's fill_line (see the header comment for
// the tautological `||` pre-check this replicates as-is).
void fill_line(Framebuffer& fb, const View& view, uint32_t color, int32_t y, int32_t x1, int32_t x2) {
    const ClipRect clip = view.clip_rect();
    int xx1 = x1 >> kFracShift;
    int xx2 = x2 >> kFracShift;

    if (y > clip.y2 || y < clip.y1) return;

    if (xx1 <= clip.x2 || xx2 >= clip.x1) {
        if (xx1 < clip.x1) xx1 = clip.x1;
        if (xx2 > clip.x2) xx2 = clip.x2;

        if (color & kMoireFlag) {
            draw_hline_moired(fb, xx1, xx2, y, color);
        } else {
            draw_hline(fb, xx1, xx2, y, color);
        }
    }
}

// Confirmed against the reference's fill_slope (see the header comment
// for the early-return-leaves-outputs-untouched and nx1/nx2-pointer-swap
// subtleties this replicates exactly).
void fill_slope(Framebuffer& fb, const View& view, uint32_t color, int32_t x1, int32_t x2, int32_t sl1,
                 int32_t sl2, int32_t y1, int32_t y2, int32_t* nx1, int32_t* nx2) {
    const ClipRect clip = view.clip_rect();

    if (y1 > clip.y2) return;

    if (y2 <= clip.y1) {
        const int32_t delta = y2 - y1;
        *nx1 = x1 + delta * sl1;
        *nx2 = x2 + delta * sl2;
        return;
    }

    if (y2 > clip.y2) y2 = clip.y2 + 1;

    if (y1 < clip.y1) {
        const int32_t delta = clip.y1 - y1;
        x1 += delta * sl1;
        x2 += delta * sl2;
        y1 = clip.y1;
    }

    if (x1 > x2 || (x1 == x2 && sl1 > sl2)) {
        std::swap(x1, x2);
        std::swap(sl1, sl2);
        std::swap(nx1, nx2);
    }

    while (y1 < y2) {
        if (y1 >= clip.y1) {
            int xx1 = x1 >> kFracShift;
            int xx2 = x2 >> kFracShift;
            if (xx1 <= clip.x2 || xx2 >= clip.x1) {
                if (xx1 < clip.x1) xx1 = clip.x1;
                if (xx2 > clip.x2) xx2 = clip.x2;

                if (color & kMoireFlag) {
                    draw_hline_moired(fb, xx1, xx2, y1, color);
                } else {
                    draw_hline(fb, xx1, xx2, y1, color);
                }
            }
        }

        x1 += sl1;
        x2 += sl2;
        y1++;
    }
    *nx1 = x1;
    *nx2 = x2;
}

// Confirmed against the reference's (file-local static) draw_wireframe_line.
void draw_wireframe_line(Framebuffer& fb, const ClipRect& clip, int x1, int y1, int x2, int y2, uint32_t color,
                          bool moire) {
    // Liang-Barsky clip against the viewport rectangle.
    const double lx1 = x1, ly1 = y1;
    const double cdx = double(x2) - lx1, cdy = double(y2) - ly1;
    const double p[4] = {-cdx, cdx, -cdy, cdy};
    const double q[4] = {lx1 - clip.x1, clip.x2 - lx1, ly1 - clip.y1, clip.y2 - ly1};
    double t0 = 0.0, t1 = 1.0;
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) return; // parallel to this edge and outside it
        } else {
            const double t = q[i] / p[i];
            if (p[i] < 0.0) {
                t0 = std::max(t0, t);
            } else {
                t1 = std::min(t1, t);
            }
        }
    }
    if (t0 > t1) return; // entirely outside the viewport

    if (t1 < 1.0) {
        x2 = static_cast<int>(std::lround(lx1 + t1 * cdx));
        y2 = static_cast<int>(std::lround(ly1 + t1 * cdy));
    }
    if (t0 > 0.0) {
        x1 = static_cast<int>(std::lround(lx1 + t0 * cdx));
        y1 = static_cast<int>(std::lround(ly1 + t0 * cdy));
    }

    const int dx = x2 > x1 ? x2 - x1 : x1 - x2;
    const int dy = y2 > y1 ? y2 - y1 : y1 - y2;
    const int sx = x1 < x2 ? 1 : -1;
    const int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        if (x1 >= 0 && x1 < fb.width() && y1 >= 0 && y1 < fb.height()) {
            if (!moire || !((x1 ^ y1) & 1)) fb.set_pixel(x1, y1, color);
        }
        if (x1 == x2 && y1 == y2) break;
        const int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

// Confirmed against the reference's fill_quad (see the header comment for
// the wireframe-detection/decomposition strategy and the truncation-point
// and negative-color-unwrap notes).
void fill_quad(Framebuffer& fb, const View& view, const Quad& quad) {
    int color = quad.col;
    if (color < 0) color = -1 - color;
    const uint32_t ucolor = static_cast<uint32_t>(color);
    const ClipRect clip = view.clip_rect();

    // Wireframe special case: a degenerate quad with only 2 distinct
    // screen vertices (from two coincident vertex pairs A,A,B,B).
    {
        const int ax = static_cast<int>(quad.points[0].sx);
        const int ay = static_cast<int>(quad.points[0].sy);
        int bx = ax, by = ay;
        int ndist = 1;
        for (int i = 1; i < 4; ++i) {
            const int vx = static_cast<int>(quad.points[i].sx);
            const int vy = static_cast<int>(quad.points[i].sy);
            if (vx == ax && vy == ay) continue;
            if (ndist == 1) {
                bx = vx;
                by = vy;
                ndist = 2;
            } else if (vx != bx || vy != by) {
                ndist = 3;
                break;
            }
        }
        if (ndist == 2) {
            draw_wireframe_line(fb, clip, ax, ay, bx, by, ucolor & ~kMoireFlag, (ucolor & kMoireFlag) != 0);
            return;
        }
    }

    struct ScreenPoint {
        int32_t x = 0, y = 0;
    };
    ScreenPoint p[8];
    for (int i = 0; i < 4; ++i) {
        p[i].x = p[i + 4].x = static_cast<int32_t>(quad.points[i].sx) << kFracShift;
        p[i].y = p[i + 4].y = static_cast<int32_t>(quad.points[i].sy);
    }

    int pmin = 0, pmax = 0;
    for (int i = 1; i < 4; ++i) {
        if (p[i].y < p[pmin].y) pmin = i;
        if (p[i].y > p[pmax].y) pmax = i;
    }

    int32_t cury = p[pmin].y;
    int32_t limy = p[pmax].y;

    if (cury == limy) {
        int32_t x1 = p[0].x, x2 = p[0].x;
        for (int i = 1; i < 4; ++i) {
            if (p[i].x < x1) x1 = p[i].x;
            if (p[i].x > x2) x2 = p[i].x;
        }
        fill_line(fb, view, ucolor, cury, x1, x2);
        return;
    }

    if (cury > clip.y2) return;
    if (limy <= clip.y1) return;
    if (limy > clip.y2) limy = clip.y2;

    int ps1 = pmin + 4;
    int ps2 = pmin;
    int32_t x1, x2, sl1, sl2;

    // "startup": skip past any additional vertices already at cury (a
    // colinear top edge), then seed x1/x2/sl1/sl2 for the first segment.
    // Duplicated once more below (after each vertex transition) rather
    // than using the reference's `goto startup` -- same control flow,
    // without the goto.
    while (p[ps1 - 1].y == cury) ps1--;
    while (p[ps2 + 1].y == cury) ps2++;
    x1 = p[ps1].x;
    x2 = p[ps2].x;
    sl1 = (x1 - p[ps1 - 1].x) / (cury - p[ps1 - 1].y);
    sl2 = (x2 - p[ps2 + 1].x) / (cury - p[ps2 + 1].y);

    for (;;) {
        if (p[ps1 - 1].y == p[ps2 + 1].y) {
            fill_slope(fb, view, ucolor, x1, x2, sl1, sl2, cury, p[ps1 - 1].y, &x1, &x2);
            cury = p[ps1 - 1].y;
            if (cury >= limy) break;
            ps1--;
            ps2++;
            while (p[ps1 - 1].y == cury) ps1--;
            while (p[ps2 + 1].y == cury) ps2++;
            x1 = p[ps1].x;
            x2 = p[ps2].x;
            sl1 = (x1 - p[ps1 - 1].x) / (cury - p[ps1 - 1].y);
            sl2 = (x2 - p[ps2 + 1].x) / (cury - p[ps2 + 1].y);
        } else if (p[ps1 - 1].y < p[ps2 + 1].y) {
            fill_slope(fb, view, ucolor, x1, x2, sl1, sl2, cury, p[ps1 - 1].y, &x1, &x2);
            cury = p[ps1 - 1].y;
            if (cury >= limy) break;
            ps1--;
            while (p[ps1 - 1].y == cury) ps1--;
            x1 = p[ps1].x;
            sl1 = (x1 - p[ps1 - 1].x) / (cury - p[ps1 - 1].y);
        } else {
            fill_slope(fb, view, ucolor, x1, x2, sl1, sl2, cury, p[ps2 + 1].y, &x1, &x2);
            cury = p[ps2 + 1].y;
            if (cury >= limy) break;
            ps2++;
            while (p[ps2 + 1].y == cury) ps2++;
            x2 = p[ps2].x;
            sl2 = (x2 - p[ps2 + 1].x) / (cury - p[ps2 + 1].y);
        }
    }
    if (cury == limy) {
        fill_line(fb, view, ucolor, cury, x1, x2);
    }
}

} // namespace model1::video
