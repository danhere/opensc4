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
#include <vector>

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
    // Copies (x, y, width, height) of the region's surface to (regionX, regionY) in the region. The
    // region's position comes first: the DirectX driver (0x008853B0) blits the surface rectangle at
    // the last two arguments to the region at the first two.
    virtual bool ReadBufferRegion(int32_t region, int32_t regionX, int32_t regionY, int32_t width, int32_t height,
                                  int32_t x, int32_t y) = 0;
    // Copies (regionX, regionY, width, height) of the region back to (x, y) of its surface (0x008855F0).
    // Different positions move a saved picture: the city view scrolls this way.
    virtual bool DrawBufferRegion(int32_t region, int32_t regionX, int32_t regionY, int32_t width, int32_t height,
                                  int32_t x, int32_t y) = 0;
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

// [renderer] scale_city = no: the divisor applied to the city camera's fine
// zoom while it builds its projection (the render scale; 1 when the city is
// enlarged with the interface or the D3D11 driver is off).
float CityZoomDivisor();

// While one lives, the D3D11 driver makes the textures it is asked for at
// the physical size when there is a render scale ([renderer] scale): the
// interface pictures' tiles (renderer.interface.sharp_tiles), which the game
// draws with coordinates over the size it asked for, so the larger texture
// fills the same place. Their uploads stay game-sized: the driver keeps a
// copy and fills the physical pixels from it.
struct PhysicalTextures {
    PhysicalTextures();
    ~PhysicalTextures();
    PhysicalTextures(const PhysicalTextures&) = delete;
    PhysicalTextures& operator=(const PhysicalTextures&) = delete;
};

// Physical pixels per game pixel while the D3D11 driver's window is up
// ([renderer] scale); 1 otherwise.
float RenderScale();

// Called with every upload of a tile made under PhysicalTextures, once the
// driver has filled the physical pixels it will upload: the texture, the
// physical rectangle (left, top, across, down, in the tile) and its BGRA
// pixels, which the hook may change (renderer.interface.sharp_text puts
// text drawn at the physical size there), and the tile's physical pixels
// per game pixel. One hook; null to remove it.
using ScaledUploadHook = void (*)(uint32_t texture, uint32_t left, uint32_t top, uint32_t across, uint32_t down,
                                  uint8_t* bgra, double scaleX, double scaleY);
void SetScaledUploadHook(ScaledUploadHook hook);

// The filter the scaled tiles are filled with: the physical pixels (left,
// top) to (left + across, top + down) of a picture, each the average of the
// game pixels it covers, weighted by how much of it each covers and by their
// alpha. `source` is width x height BGRA game pixels whose top left is the
// game pixel (sourceX, sourceY); physical pixel x covers game x / sx to
// (x + 1) / sx. Game pixels outside the source count for nothing.
void FilterUp(const uint8_t* source, uint32_t width, uint32_t height, int32_t sourceX, int32_t sourceY, double sx,
              double sy, int32_t left, int32_t top, uint32_t across, uint32_t down, uint8_t* out);

// The last frame shown in the game's window at its full (physical)
// resolution, as BGRA rows; false without the D3D11 driver's window.
bool ReadFrame(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height);

// Same-frame comparison of draws, for the compare mode of replacements that
// draw: Begin records a checksum per draw (frame_trace::HashDraw) and, with
// `draw` false, keeps the draws off the screen; End returns the checksums.
// SaveState/RestoreState copy the driver's pipeline state (matrices, enables,
// blending, stages, viewport...), so that a quiet run can be undone.
void BeginCapture(bool draw);
std::vector<uint32_t> EndCapture();
std::vector<char> SaveState();
void RestoreState(const std::vector<char>& state);

// Shows the back buffer on a timer while photo mode is on (the game stops
// presenting when every interface window is hidden).
void PresentWhileActive();

} // namespace opensc4::gd
