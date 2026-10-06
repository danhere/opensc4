// Plain GD types and enum values (no gzcom-dll dependency), for the driver,
// the backends and the upload conversions. Everything here comes from the
// DirectX 7 and OpenGL drivers in build 641 (docs/renderer.md, "What the
// driver calls mean").
#pragma once

#include <cstdint>

namespace opensc4::gd {

// sGDMode: one entry of the driver's video mode list (0x34 bytes; the drivers
// copy 13 dwords). Masks are the frame buffer's channel masks. Field names
// from gzcom-dll's sGDMode.h. The capability fields matter: the game reads the
// current mode's (GetVideoModeInfo, 0x007CF780) into the configuration that
// Graphics Rules.sgr is evaluated against (textureStages, DXT, ...).
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
    uint8_t stencil;
    uint8_t unknown22; // 1 in every mode the DirectX driver lists
    uint8_t pad23;
    uint32_t textureStages;   // "textureStages" in Graphics Rules.sgr; 2 or more enables useSecondStage
    uint8_t multitexture;     // GL_ARB_multitexture
    uint8_t envCombine;       // GL_EXT_texture_env_combine
    uint8_t envCombine4;      // GL_NV_texture_env_combine4
    uint8_t fogCoordinate;    // GL_EXT_fog_coord
    uint8_t dxt;              // "DXT" in Graphics Rules.sgr
    uint8_t unknown2D[2];
    uint8_t unknown2F;        // 1 in the DirectX driver's current mode
    uint8_t initialized;
    uint8_t pad31[3];
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

// BufferRegion surfaces.
enum Surface : int32_t { FrontBuffer, BackBuffer, DepthBuffer };

// Enable/Disable states (eGDCapability). Texturing is per texture stage.
enum Capability : uint32_t { AlphaTest, DepthTest, StencilTest, CullFace, Blending, Texture2D, Fog, CapabilityCount = 8 };

// TexEnv modes (eGDTextureEnvParam).
enum TextureEnvMode : uint32_t { EnvReplace, EnvModulate, EnvDecal, EnvBlend, EnvCombine, EnvCombine4 };

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

// TexParameter parameters and values (OpenGL driver tables 0x00B12158 and
// 0x00B12168). The DirectX driver stores them per texture stage.
enum TextureParam : uint32_t { MagFilter, MinFilter, WrapS, WrapT };
enum TextureParamValue : uint32_t {
    Nearest,
    Linear,
    Clamp,
    Repeat,
    NearestMipmapNearest,
    LinearMipmapNearest,
    NearestMipmapLinear,
    LinearMipmapLinear,
};

// Fog parameters and modes (tables 0x00B12188 and 0x00B121A0).
enum FogParam : uint32_t { FogMode, FogColor, FogDensity, FogStart, FogEnd, FogCoordinateSource };
enum FogModeValue : uint32_t { FogExp, FogExp2, FogLinear, FogCoordinate };

// TexStageCoord: low 3 bits = texture coordinate set, the rest = generation.
constexpr uint32_t kTexCoordSetMask = 7;
constexpr uint32_t kTexGenCameraPosition = 0x10;
constexpr uint32_t kTexGenCameraNormal = 0x20;
constexpr uint32_t kTexGenCameraReflection = 0x30;

} // namespace opensc4::gd
