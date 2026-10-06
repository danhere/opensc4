// Rendering backend behind OpenSC4's graphics driver (driver.h): everything
// that touches a graphics API. The driver keeps the game's fixed-function
// state and calls this; a Vulkan backend can sit beside the D3D11 one.
//
// Frame buffers: the back buffer is drawn to and keeps its contents across
// frames (the game redraws only what changed); Present copies it to the front
// buffer and the window. Rectangles are pixels with a top-left origin.
#pragma once

#include "core/gd/gd_types.h"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>

namespace opensc4::gd {

struct Rect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
};

constexpr uint32_t kMaxStages = 8;

// One texture stage, as the driver hands it to the backend for a draw.
struct StageDesc {
    bool enabled = false;
    uint32_t texture = 0; // backend handle
    uint32_t envMode = 1; // eGDTextureEnvParam (modulate)
    Combiner combiner{};
    uint32_t texCoord = 0; // TexStageCoord: set | generation
    bool hasMatrix = false;
    float matrix[16]{}; // GL layout
    uint32_t params[4] = {Nearest, Nearest, Repeat, Repeat}; // TextureParam -> TextureParamValue
};

// The fixed-function state a draw uses. Matrices in GL layout; viewport in
// top-left pixels.
struct DrawState {
    float modelView[16];
    float projection[16];
    Rect viewport;
    bool enabled[8]; // eGDCapability (texturing is per stage instead)
    uint32_t depthFunc;
    bool depthMask;
    uint32_t stencilFunc, stencilRef, stencilReadMask, stencilWriteMask;
    uint32_t stencilOps[3]; // fail, depth fail, pass
    uint32_t blendSrc, blendDst;
    uint32_t alphaFunc;
    float alphaRef;
    uint32_t shadeModel;
    int32_t polygonOffset;
    bool lighting; // ColorMultiplier/AlphaMultiplier have been called
    float colorMultiplier[4]; // ambient light colour, and the material's diffuse alpha
    bool vertexColors;        // ambient material from the vertex colour (else white)
    bool diffuseFromVertex;   // diffuse material, hence alpha, from the vertex colour (else the material)
    float textureFactor[4];
    uint32_t fogMode;
    float fogColor[4];
    float fogDensity, fogStart, fogEnd;
    uint32_t stageCount;
    StageDesc stages[kMaxStages];
    // An interface draw shown enlarged ([renderer] scale): a pixel-exact
    // projection for its viewport. Nearest-filtered images in it are drawn
    // sharp but even (D3D11 backend: sharp bilinear).
    bool interfaceDraw;
};

// The vertices and indices of a draw. `indices` null: vertices [first, first + count).
struct DrawCall {
    uint32_t primitive;
    uint32_t vertexFormat;
    uint32_t vertexStride;
    const uint8_t* vertices;
    uint32_t first;
    uint32_t count; // vertices, or indices
    const void* indices;
    uint32_t indexType; // DataType: UnsignedByte, UnsignedShort or UnsignedInt
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
    // Back buffer to front buffer, shown in the window as soon as the display
    // can take it.
    virtual void Present() = 0;
    // Shows a front buffer that Present could not show yet. Returns whether
    // one is still waiting.
    virtual bool ShowFront() = 0;

    // Buffer regions (BufferRegionExtension): surface-sized saved copies.
    virtual int32_t CreateRegion(Surface surface) = 0; // 0 on failure, else a handle from 1
    virtual bool DeleteRegion(int32_t region) = 0;
    virtual bool IsRegion(int32_t region) const = 0;
    virtual bool ReadRegion(int32_t region, const Rect& source, int32_t regionX, int32_t regionY) = 0;
    virtual bool DrawRegion(int32_t region, const Rect& target, int32_t regionX, int32_t regionY) = 0;

    // Reads `rect` of the front buffer as 32-bit BGRA (A8R8G8B8) rows.
    virtual bool ReadFront(const Rect& rect, uint8_t* pixels, uint32_t pitch) = 0;

    // Textures: handles from 1, 0 on failure.
    virtual uint32_t CreateTexture(uint32_t format, uint32_t width, uint32_t height, uint32_t levels) = 0;
    virtual void DeleteTexture(uint32_t texture) = 0;
    // `rowLength` in pixels, 0 for `width`.
    virtual void UploadTexture(uint32_t texture, uint32_t level, int32_t x, int32_t y, int32_t width, int32_t height,
                               uint32_t format, uint32_t type, uint32_t rowLength, const void* pixels) = 0;

    virtual void Draw(const DrawState& state, const DrawCall& call) = 0;
};

std::unique_ptr<Backend> CreateD3D11Backend();

// The primary display adapter, as GetDriverInfo reports it: name, PCI IDs and
// user-mode driver version. Empty name if DXGI is unavailable.
struct AdapterInfo {
    std::string name;
    uint32_t vendor = 0, device = 0, subsystem = 0, revision = 0;
    uint16_t driverVersion[4] = {};
};
AdapterInfo QueryAdapterInfo();

} // namespace opensc4::gd
