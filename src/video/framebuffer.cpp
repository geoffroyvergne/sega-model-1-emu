#include "video/framebuffer.h"

namespace model1::video {

Framebuffer::Framebuffer(int width, int height)
    : width_(width), height_(height), pixels_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0) {}

void Framebuffer::set_pixel(int x, int y, uint32_t color) {
    pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)] = color;
}

uint32_t Framebuffer::pixel(int x, int y) const {
    return pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];
}

} // namespace model1::video
