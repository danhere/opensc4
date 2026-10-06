// Rendering backend behind OpenSC4's graphics driver (driver.h): everything
// that touches a graphics API. The driver keeps the game's fixed-function
// state and calls this; a Vulkan backend can sit beside the D3D11 one.
//
// Frame buffers: the back buffer is drawn to and keeps its contents across
// frames (the game redraws only what changed); Present copies it to the front
// buffer and the window. Rectangles are pixels with a top-left origin.
#pragma once

#include "core/gd/gd.h"

#include <windows.h>

#include <cstdint>
#include <memory>

namespace opensc4::gd {

struct Rect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
};

class Backend {
public:
    virtual ~Backend() = default;

    // Creates the device and frame buffers for `window`'s client area.
    virtual bool Init(HWND window, uint32_t width, uint32_t height) = 0;
    virtual uint32_t Width() const = 0;
    virtual uint32_t Height() const = 0;

    // Clears `rect` of the buffers named by the kClear* bits in `mask`.
    virtual void Clear(const Rect& rect, uint32_t mask, const float color[4], float depth, uint8_t stencil) = 0;
    // Back buffer to front buffer and window.
    virtual void Present() = 0;

    // Buffer regions (BufferRegionExtension): surface-sized saved copies.
    virtual int32_t CreateRegion(Surface surface) = 0; // 0 on failure, else a handle from 1
    virtual bool DeleteRegion(int32_t region) = 0;
    virtual bool IsRegion(int32_t region) const = 0;
    virtual bool ReadRegion(int32_t region, const Rect& source, int32_t regionX, int32_t regionY) = 0;
    virtual bool DrawRegion(int32_t region, const Rect& target, int32_t regionX, int32_t regionY) = 0;

    // Reads `rect` of the front buffer as 32-bit BGRA (A8R8G8B8) rows.
    virtual bool ReadFront(const Rect& rect, uint8_t* pixels, uint32_t pitch) = 0;
};

std::unique_ptr<Backend> CreateD3D11Backend();

} // namespace opensc4::gd
