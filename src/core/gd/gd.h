// The game's graphics driver API ("GD", cIGZGDriver) as the stock drivers
// implement it. gzcom-dll's cIGZGDriver.h declares the methods; this adds what
// it leaves out: the mode and combiner layouts, the enum values the drivers
// translate, and the two extension interfaces the game queries. Everything here
// comes from the DirectX 7 and OpenGL drivers in build 641 (docs/renderer.md,
// "What the driver calls mean").
#pragma once

#include <cIGZUnknown.h>

#include <cstdint>

class cIGZBuffer;

namespace opensc4::gd {

// sGDMode: one entry of the driver's video mode list (0x34 bytes; the drivers
// copy 13 dwords). Masks are the frame buffer's channel masks.
struct Mode {
    int32_t index;
    uint32_t width;
    uint32_t height;
    uint32_t depth; // bits per pixel: 16 or 32
    uint32_t alphaMask;
    uint32_t redMask;
    uint32_t greenMask;
    uint32_t blueMask;
    uint8_t fullScreen; // 0: windowed mode (the DirectX driver creates an overlapped window)
    uint8_t unknown21;
    uint8_t unknown22;
    uint8_t pad23;
    uint32_t unknown24;
    uint32_t unknown28;
    uint8_t unknown2C[8];
};
static_assert(sizeof(Mode) == 0x34);

// cGDCombiner (SetCombiner): a texture stage's combine setup in 10 bytes.
// Args: low nibble = source (eGDTextureStageCombineSourceParam), high nibble =
// operand (0 color, 1 one minus color, 2 alpha, 3 one minus alpha).
struct Combiner {
    uint8_t rgbOp; // eGDTextureStageCombineModeParam; 0xFF: leave the stage's combiner alone
    uint8_t rgbArg[3];
    uint8_t alphaOp;
    uint8_t alphaArg[3];
    uint8_t rgbScale; // eGDTextureStageCombineScaleParam
    uint8_t alphaScale;
};
static_assert(sizeof(Combiner) == 10);

// DrawArrays/DrawElements primitive types.
enum Primitive : uint32_t { Triangles, TriangleStrip, TriangleFan, Points, Lines, LineStrip, Quads, QuadStrip };

// Data types (index types for DrawElements, pixel types for texture uploads).
enum DataType : uint32_t {
    Byte,
    UnsignedByte,
    Short,
    UnsignedShort,
    Int,
    UnsignedInt,
    Float,
    Double,
    UnsignedShort4444, // GL_UNSIGNED_SHORT_4_4_4_4
    UnsignedShort5551, // GL_UNSIGNED_SHORT_5_5_5_1
    UnsignedInt8888 = 12,
    UnsignedShort4444Rev = 13,
};

// DepthFunc, AlphaFunc, StencilFunc.
enum TestFunc : uint32_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };

enum StencilOp : uint32_t { StencilZero, StencilKeep, StencilReplace, StencilIncrement, StencilDecrement, StencilInvert };

enum ShadeModel : uint32_t { Flat, Smooth };

enum MatrixMode : uint32_t { ModelView, Projection };

// Clear mask bits.
constexpr uint32_t kClearDepth = 0x1000;
constexpr uint32_t kClearStencil = 0x2000;
constexpr uint32_t kClearColor = 0x4000;

// LoadTextureLevel source formats.
enum PixelFormat : uint32_t { RGB, RGBA, BGR, BGRA, Alpha, Luminance, LuminanceAlpha, DXT1, DXT1A, DXT3, DXT5 };

// CreateTexture formats.
enum TextureFormat : uint32_t { TexRGB5, TexRGB8, TexRGBA4, TexRGB5A1, TexRGBA8, TexDXT1A, TexDXT3, TexDXT5 };

// TexParameter parameters (the DirectX driver stores them per stage).
enum TextureParam : uint32_t { MinFilter, MagFilter, WrapS, WrapT };

// TexStageCoord: low 3 bits = texture coordinate set, the rest = generation.
constexpr uint32_t kTexCoordSetMask = 7;
constexpr uint32_t kTexGenCameraPosition = 0x10;
constexpr uint32_t kTexGenCameraNormal = 0x20;
constexpr uint32_t kTexGenCameraReflection = 0x30;

// BufferRegion surfaces.
enum Surface : int32_t { FrontBuffer, BackBuffer, DepthBuffer };

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

} // namespace opensc4::gd
