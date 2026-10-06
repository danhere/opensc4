// The game's graphics driver API ("GD", cIGZGDriver) as the stock drivers
// implement it. gzcom-dll's cIGZGDriver.h declares the methods; this adds the
// two extension interfaces the game queries, and gd_types.h what else it
// leaves out (mode and combiner layouts, enum values). Everything here
// comes from the DirectX 7 and OpenGL drivers in build 641 (docs/renderer.md,
// "What the driver calls mean").
#pragma once

#include "core/gd/gd_types.h"

#include <cIGZUnknown.h>

#include <cstdint>

class cIGZBuffer;

namespace opensc4::gd {

// cIGZGBufferRegionExtension (0x669565FE): saves and restores rectangles of the
// frame buffers. Handles start at 1. Rectangles use top-left origin pixels
// (the DirectX driver blits them as given).
constexpr uint32_t kIID_BufferRegionExtension = 0x669565FE;
class BufferRegionExtension : public cIGZUnknown {
public:
    virtual bool BufferRegionEnabled() = 0;
    virtual int32_t NewBufferRegion(int32_t surface) = 0;
    virtual bool DeleteBufferRegion(int32_t region) = 0;
    // Copies (x, y, width, height) of the region's surface to (regionX, regionY) in the region.
    virtual bool ReadBufferRegion(int32_t region, int32_t x, int32_t y, int32_t width, int32_t height,
                                  int32_t regionX, int32_t regionY) = 0;
    // Copies (regionX, regionY, width, height) of the region back to (x, y) of its surface.
    virtual bool DrawBufferRegion(int32_t region, int32_t x, int32_t y, int32_t width, int32_t height,
                                  int32_t regionX, int32_t regionY) = 0;
    virtual bool IsBufferRegion(int32_t region) = 0;
    virtual bool CanDoPartialRegionWrites() = 0;
    virtual bool CanDoOffsetReads() = 0;
};

// cIGZGSnapshotExtension (0xE69BFE2A): copies part of the front buffer into a
// 32-bit cIGZBuffer, creating one if `buffer` is null. Returns the buffer.
constexpr uint32_t kIID_SnapshotExtension = 0xE69BFE2A;
class SnapshotExtension : public cIGZUnknown {
public:
    virtual cIGZBuffer* CopyColorBuffer(int32_t x, int32_t y, int32_t width, int32_t height, cIGZBuffer* buffer) = 0;
};

// 0x09CD86F9: dynamic vertex buffers (the DirectX driver's object at +0x1C,
// vtable 0x00AC4F10, wrapping Direct3D 7 vertex buffers). The terrain and
// scene renderers (0x007545C0, 0x00593E10) lock space for vertices, write
// them, unlock and draw them with the current driver state; without this
// interface they draw nothing (the stock OpenGL driver lacks it). A buffer
// holds Capacity() vertices of one format; Lock reserves `count` vertices
// after the previous batch (starting over when they do not fit) and starts a
// new batch, Append extends it, Draw and DrawIndexed draw the batch (indices
// relative to its first vertex).
constexpr uint32_t kIID_VertexBuffers = 0x09CD86F9;
class VertexBuffers : public cIGZUnknown {
public:
    virtual int32_t GetBuffer(uint32_t vertexFormat) = 0;     // slot 3; the DirectX driver always returns 0
    virtual uint32_t BufferFormat(int32_t buffer) = 0;        // slot 4
    virtual int32_t Capacity(int32_t buffer) = 0;             // slot 5, in vertices
    virtual void* Lock(int32_t buffer, int32_t count) = 0;    // slot 6
    virtual void* Append(int32_t buffer, int32_t count) = 0;  // slot 7
    virtual void Unlock(int32_t buffer) = 0;                  // slot 8
    virtual void Draw(int32_t buffer, uint32_t primitive) = 0; // slot 9
    virtual void DrawIndexed(int32_t buffer, uint32_t primitive, int32_t count, const uint16_t* indices) = 0; // slot 10
    virtual void Reset() = 0;                                 // slot 11
};

} // namespace opensc4::gd
