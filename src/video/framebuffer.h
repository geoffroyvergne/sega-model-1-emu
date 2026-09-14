#pragma once

#include <cstdint>
#include <vector>

// A minimal packed-0xRRGGBB framebuffer for the rasterizer stage --
// "Role B" in docs/hardware-notes/02-tgp-coprocessor.md and
// docs/hardware-notes/05-video.md. Stands in for the reference's
// bitmap_rgb32 (MAME's own display-surface type, not a hardware fact);
// this project's actual display backend, whatever it ends up being, will
// presumably render from something like this.
namespace model1::video {

// Row-major, top-to-bottom. Pixel writes are intentionally NOT bounds-
// checked, matching the reference's own draw_hline/draw_hline_moired
// (model1_v.cpp), which index bitmap.pix(y) directly with no range check
// -- the reference relies entirely on its callers (fill_slope/fill_line,
// clipped against the viewport) to keep every write in range, and this
// class replicates that contract rather than adding a safety net the
// reference doesn't have.
class Framebuffer {
public:
    Framebuffer(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }

    void set_pixel(int x, int y, uint32_t color);
    uint32_t pixel(int x, int y) const;

private:
    int width_ = 0, height_ = 0;
    std::vector<uint32_t> pixels_;
};

} // namespace model1::video
