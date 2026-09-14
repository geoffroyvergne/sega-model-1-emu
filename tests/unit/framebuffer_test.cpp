#include "doctest.h"
#include "video/framebuffer.h"

using namespace model1::video;

TEST_CASE("A freshly constructed framebuffer is all zero (black)") {
    Framebuffer fb(4, 3);

    CHECK(fb.width() == 4);
    CHECK(fb.height() == 3);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 4; ++x) CHECK(fb.pixel(x, y) == 0);
}

TEST_CASE("set_pixel/pixel round-trip without disturbing neighboring pixels") {
    Framebuffer fb(4, 3);

    fb.set_pixel(2, 1, 0x00ff00);

    CHECK(fb.pixel(2, 1) == 0x00ff00u);
    // Neighbors in both the same row and the same column stay untouched.
    CHECK(fb.pixel(1, 1) == 0);
    CHECK(fb.pixel(3, 1) == 0);
    CHECK(fb.pixel(2, 0) == 0);
    CHECK(fb.pixel(2, 2) == 0);
}

TEST_CASE("Rows are indexed independently for a non-square framebuffer") {
    // A width != height framebuffer catches a stride bug (e.g. indexing
    // by height instead of width) that a square one would hide.
    Framebuffer fb(5, 2);

    fb.set_pixel(4, 0, 0x111111); // last pixel of row 0
    fb.set_pixel(0, 1, 0x222222); // first pixel of row 1

    CHECK(fb.pixel(4, 0) == 0x111111u);
    CHECK(fb.pixel(0, 1) == 0x222222u);
}
