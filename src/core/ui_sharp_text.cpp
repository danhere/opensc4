// renderer.interface.sharp_text: see ui_sharp_text.h and docs/ui-scaling.md
// ("Sharp interface text").
//
// A line's copy covers the rectangle of the glyphs it keeps (its "text
// rectangle"), cut to the clip and the picture. Its pixels are the
// rectangle as it was before the line (two game pixels more around it, so
// the edges filter as the driver's do), filtered up with gd::FilterUp, and
// the line's characters rendered by FreeType from the font's face (the
// .mxf files' Type 1 data, unscrambled) at the font's size times the
// scale, each at the line's pen for it times the scale on the baseline
// (the pen's y plus the line spacing and the ascent), so the layout stays
// the game's. The colours are composited as the blitter's kind does: 1
// solid where the coverage is above half, 2 the ramp's colour of the
// coverage, 3 the colour blended by the coverage.
//
// The copy is kept for the picture with a hash of what the line left in its
// text rectangle. When the driver uploads one of the picture's tiles
// (renderer.interface.sharp_tiles), a copy whose rectangle still hashes the
// same replaces the filtered pixels there; one that does not is dropped.
// The tile's place in the picture comes from the textures object Upload
// works on (0x00990FF0, detoured here to note it): its tiles' textures at
// +0x14 and, for each tile, the pieces it uploads (0x00A6CD10 on its +0x24),
// each the picture's x and y, the tile's x and y and the size.
#include "core/ui_sharp_text.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/gd/gd.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>

namespace opensc4::sharptext {
namespace {

using Address = uint32_t;

inline Address At(const void* p) { return static_cast<Address>(reinterpret_cast<uintptr_t>(p)); }
inline void* Ptr(Address at) { return reinterpret_cast<void*>(static_cast<uintptr_t>(at)); }

template <typename T>
inline T Load(Address at)
{
    T value;
    std::memcpy(&value, Ptr(at), sizeof value);
    return value;
}

template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

template <typename R, typename... Args>
inline R This(std::uintptr_t fn, void* self, Args... args)
{
    return reinterpret_cast<R(__fastcall*)(void*, void*, Args...)>(fn)(self, nullptr, args...);
}

// The picture (cIGZBuffer): its size, bits per pixel, colour key, pixels
// and stride in bytes.
namespace buffer {
constexpr std::size_t kWidth = 0x24, kHeight = 0x28, kBitsPerPixel = 0x40, kHasColorKey = 0x60, kColorKey = 0x68,
                      kBits = 0x88, kStride = 0x8C;
}

// The font: its path (a game string: Data 0x18, Length 0x1C), size,
// spacing added to every line, ascent.
namespace font {
constexpr Address kPath = 0x0C, kSize = 0x38, kLineSpacing = 0x78, kAscent = 0x84;
}

// The textures object: the picture, the tiles' textures (a vector of
// handles), the tiles' layout.
namespace textures {
constexpr Address kPicture = 0x10, kTiles = 0x14, kTilesEnd = 0x18, kLayout = 0x24;
}
constexpr std::uintptr_t kTileOf = 0x00A6CD10; // thiscall on the layout (index): the tile

constexpr int32_t kMargin = 2;

struct Rect {
    int32_t left, top, right, bottom;
    bool Empty() const { return right <= left || bottom <= top; }
};

Rect Intersect(const Rect& a, const Rect& b)
{
    return {std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
}

bool g_on = false;


struct Picture {
    Address bits;
    uint32_t stride;
    int32_t width, height;
};

Picture PictureOf(void* picture)
{
    return {At(Virtual<void*>(picture, buffer::kBits)), Virtual<uint32_t>(picture, buffer::kStride),
            Virtual<int32_t>(picture, buffer::kWidth), Virtual<int32_t>(picture, buffer::kHeight)};
}

// FNV-1a over a rectangle of a 32-bit picture.
uint32_t HashOf(const Picture& p, const Rect& r)
{
    uint32_t hash = 2166136261u;
    for (int32_t y = r.top; y < r.bottom; ++y) {
        const auto* row = static_cast<const uint8_t*>(Ptr(p.bits + static_cast<uint32_t>(y) * p.stride)) + r.left * 4;
        for (int32_t i = 0; i < (r.right - r.left) * 4; ++i)
            hash = (hash ^ row[i]) * 16777619u;
    }
    return hash;
}

// ====================================================================================
// FreeType faces

FT_Library g_library = nullptr;

struct Face {
    std::vector<uint8_t> data;
    FT_Face face = nullptr;
};

std::map<std::string, std::unique_ptr<Face>> g_faces;

bool ReadFile(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(size > 0 ? static_cast<size_t>(size) : 0);
    const bool ok = size > 0 && std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

// The font's face: the file at its path (else in Windows's Fonts folder, as
// the font system looks), an .mxf one unscrambled ("MXFN", the scrambled
// length big-endian, the key, then the face, its first bytes XORed with the
// key; fonts.md, "The stream": inside is a Type 1 font as a PFB file).
// Null when FreeType cannot open it.
FT_Face FaceOf(void* fontObject)
{
    void* const pathString = Ptr(At(fontObject) + font::kPath);
    const auto data = Virtual<const char*>(pathString, 0x18);
    const uint32_t length = Virtual<uint32_t>(pathString, 0x1C);
    if (!data || !length)
        return nullptr;
    const std::string path(data, length);
    auto& slot = g_faces[path];
    if (slot)
        return slot->face;
    slot = std::make_unique<Face>();
    if (!g_library && FT_Init_FreeType(&g_library) != 0) {
        g_library = nullptr;
        return nullptr;
    }
    std::vector<uint8_t> bytes;
    if (!ReadFile(path, bytes)) {
        char windows[MAX_PATH] = {};
        GetWindowsDirectoryA(windows, MAX_PATH);
        if (!ReadFile(std::string(windows) + "\\Fonts\\" + path, bytes))
            return nullptr;
    }
    if (bytes.size() > 9 && std::memcmp(bytes.data(), "MXFN", 4) == 0) {
        const uint32_t scrambled = static_cast<uint32_t>(bytes[4]) << 24 | static_cast<uint32_t>(bytes[5]) << 16 |
                                   static_cast<uint32_t>(bytes[6]) << 8 | bytes[7];
        const uint8_t key = bytes[8];
        bytes.erase(bytes.begin(), bytes.begin() + 9);
        for (size_t i = 0; i < bytes.size() && i < scrambled; ++i)
            bytes[i] ^= key;
    }
    slot->data = std::move(bytes);
    if (FT_New_Memory_Face(g_library, slot->data.data(), static_cast<FT_Long>(slot->data.size()), 0, &slot->face) !=
        0) {
        slot->face = nullptr;
        log::Write("sharptext: FreeType could not open %s; its text stays as the game draws it", path.c_str());
    }
    return slot->face;
}

// ====================================================================================
// The copies

struct Copy {
    Rect text;     // game pixels: what the line drew in, cut to the clip and the picture
    uint32_t hash; // of the text rectangle after the line
    Rect physical; // physical pixels covered
    std::vector<uint8_t> bgra;
};

std::map<void*, std::vector<Copy>> g_copies;
constexpr size_t kCopiesPerPicture = 256;

void Keep(void* picture, Copy copy)
{
    auto& list = g_copies[picture];
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const Copy& c) {
                                  return c.text.left >= copy.text.left && c.text.top >= copy.text.top &&
                                         c.text.right <= copy.text.right && c.text.bottom <= copy.text.bottom;
                              }),
               list.end());
    if (list.size() >= kCopiesPerPicture)
        list.erase(list.begin());
    list.push_back(std::move(copy));
}

// Blends `cover` (0 to 255) of `colour` (BGRA) over the pixel.
void Blend(uint8_t* out, const uint8_t* colour, uint32_t cover)
{
    for (int k = 0; k < 4; ++k)
        out[k] = static_cast<uint8_t>((out[k] * (255 - cover) + colour[k] * cover + 127) / 255);
}

// ====================================================================================
// The upload hook

void* g_uploading = nullptr; // the textures object whose Upload runs

void OnScaledUpload(uint32_t texture, uint32_t left, uint32_t top, uint32_t across, uint32_t down, uint8_t* bgra,
                    double sx, double sy)
{
    if (!g_on || !g_uploading)
        return;
    const Address obj = At(g_uploading);
    void* const picture = Ptr(Load<Address>(obj + textures::kPicture));
    const auto found = g_copies.find(picture);
    if (!picture || found == g_copies.end() || found->second.empty())
        return;
    // The tile of this texture and where it lies in the picture.
    const Address tiles = Load<Address>(obj + textures::kTiles);
    const uint32_t count = (Load<Address>(obj + textures::kTilesEnd) - tiles) / 4;
    uint32_t index = 0;
    while (index < count && Load<uint32_t>(tiles + index * 4) != texture)
        ++index;
    if (index == count)
        return;
    const Address tile = This<Address>(kTileOf, Ptr(obj + textures::kLayout), index);
    const Address piece = Load<Address>(tile);
    if (!tile || piece == Load<Address>(tile + 4))
        return;
    const int32_t originX = Load<int32_t>(piece) - Load<int32_t>(piece + 8);
    const int32_t originY = Load<int32_t>(piece + 4) - Load<int32_t>(piece + 0xC);
    const auto physicalX = static_cast<int32_t>(std::lround(originX * sx));
    const auto physicalY = static_cast<int32_t>(std::lround(originY * sy));
    const Rect region{physicalX + static_cast<int32_t>(left), physicalY + static_cast<int32_t>(top),
                      physicalX + static_cast<int32_t>(left + across), physicalY + static_cast<int32_t>(top + down)};
    const Picture p = PictureOf(picture);
    if (!p.bits)
        return;
    auto& list = found->second;
    for (size_t i = 0; i < list.size();) {
        Copy& c = list[i];
        if (HashOf(p, c.text) != c.hash) {
            list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        const Rect both = Intersect(region, c.physical);
        for (int32_t y = both.top; !both.Empty() && y < both.bottom; ++y) {
            const uint8_t* from = c.bgra.data() +
                                  (static_cast<size_t>(y - c.physical.top) * (c.physical.right - c.physical.left) +
                                   (both.left - c.physical.left)) *
                                      4;
            uint8_t* to = bgra + (static_cast<size_t>(y - region.top) * across + (both.left - region.left)) * 4;
            std::memcpy(to, from, static_cast<size_t>(both.right - both.left) * 4);
        }
        ++i;
    }
}

using UploadFn = uint32_t(__fastcall*)(void*, void*, uint32_t);
UploadFn g_upload = nullptr;

uint32_t __fastcall DUpload(void* self, void*, uint32_t argument)
{
    void* const outer = g_uploading;
    g_uploading = self;
    const uint32_t result = g_upload(self, nullptr, argument);
    g_uploading = outer;
    return result;
}

OPENSC4_THUNK(TUpload, DUpload, 4)

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* entry = addr_check::Find(addr::UIPictureTextures_Upload);
    if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(&TUpload), reinterpret_cast<void**>(&g_upload)))
        return false;
    gd::SetScaledUploadHook(&OnScaledUpload);
    g_on = true;
    log::Write("sharptext: text drawn again at the physical size under a render scale (needs "
               "renderer.interface.sharp_tiles and framework.font.font.draw)");
    return true;
}

const bool g_registered = registry::Register(
    {"renderer.interface.sharp_text", "renderer/interface", registry::Kind::Detour, registry::Phase::Attach,
     addr::UIPictureTextures_Upload,
     "Interface text drawn again at the physical size with FreeType under a render scale (with "
     "renderer.interface.sharp_tiles and framework.font.font.draw; docs/ui-scaling.md)",
     false, Install});

} // namespace

// ====================================================================================
// The lines

bool Wanted(void* picture)
{
    return g_on && picture && gd::RenderScale() != 1.0f && Virtual<uint32_t>(picture, buffer::kBitsPerPixel) == 32;
}

Line::Line(void* fontObject, void* picture, const int32_t* clip, uint32_t kind, const uint32_t* colours)
    : font_(fontObject), picture_(picture), clipped_(clip != nullptr), kind_(kind), colours_(colours)
{
    if (clip)
        std::memcpy(clip_, clip, sizeof clip_);
    // A picture with a colour key reaches its tiles with the key's pixels
    // cleared to 0 (Upload's copy 0x008D08A0: the low 24 bits compared).
    if (Virtual<uint32_t>(picture, buffer::kHasColorKey) & 0xFF) {
        keyed_ = true;
        Virtual<void>(picture, buffer::kColorKey, &key_);
    }
}

void Line::Glyph(int32_t penX, int32_t penY, uint32_t character, int32_t left, int32_t top, int32_t right,
                 int32_t bottom)
{
    glyphs_.push_back({penX, penY, character});
    if (!any_) {
        rect_[0] = left, rect_[1] = top, rect_[2] = right, rect_[3] = bottom;
        any_ = true;
        return;
    }
    rect_[0] = std::min(rect_[0], left);
    rect_[1] = std::min(rect_[1], top);
    rect_[2] = std::max(rect_[2], right);
    rect_[3] = std::max(rect_[3], bottom);
}

namespace {

Rect Bounds(const Picture& p, const int32_t* clip, bool clipped)
{
    Rect r{0, 0, p.width, p.height};
    if (clipped)
        r = Intersect(r, Rect{clip[0], clip[1], clip[2], clip[3]});
    return r;
}

} // namespace

void Line::Before()
{
    if (!any_)
        return;
    const Picture p = PictureOf(picture_);
    if (!p.bits) {
        any_ = false;
        return;
    }
    const Rect around = Intersect(Bounds(p, clip_, clipped_),
                                  Rect{rect_[0] - kMargin, rect_[1] - kMargin, rect_[2] + kMargin, rect_[3] + kMargin});
    if (around.Empty()) {
        any_ = false;
        return;
    }
    before_.resize(static_cast<size_t>(around.right - around.left) * (around.bottom - around.top) * 4);
    for (int32_t y = around.top; y < around.bottom; ++y)
        std::memcpy(before_.data() + static_cast<size_t>(y - around.top) * (around.right - around.left) * 4,
                    Ptr(p.bits + static_cast<uint32_t>(y) * p.stride + static_cast<uint32_t>(around.left) * 4),
                    static_cast<size_t>(around.right - around.left) * 4);
    if (keyed_) {
        for (size_t i = 0; i < before_.size(); i += 4) {
            uint32_t pixel;
            std::memcpy(&pixel, before_.data() + i, 4);
            if (((pixel ^ key_) & 0xFFFFFF) == 0)
                std::memset(before_.data() + i, 0, 4);
        }
    }
}

void Line::After()
{
    if (!any_ || before_.empty())
        return;
    const Picture p = PictureOf(picture_);
    if (!p.bits)
        return;
    const Rect bounds = Bounds(p, clip_, clipped_);
    const Rect around =
        Intersect(bounds, Rect{rect_[0] - kMargin, rect_[1] - kMargin, rect_[2] + kMargin, rect_[3] + kMargin});
    const Rect text = Intersect(bounds, Rect{rect_[0], rect_[1], rect_[2], rect_[3]});
    if (text.Empty())
        return;
    FT_Face face = FaceOf(font_);
    if (!face)
        return;
    const double s = gd::RenderScale();
    const auto size = Load<uint32_t>(At(font_) + font::kSize);
    if (size == 0 || FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(std::lround(size * s))) != 0)
        return;

    Copy copy;
    copy.text = text;
    copy.hash = HashOf(p, text);
    copy.physical = {static_cast<int32_t>(std::floor(text.left * s)), static_cast<int32_t>(std::floor(text.top * s)),
                     static_cast<int32_t>(std::ceil(text.right * s)), static_cast<int32_t>(std::ceil(text.bottom * s))};
    const int32_t across = copy.physical.right - copy.physical.left;
    const int32_t down = copy.physical.bottom - copy.physical.top;
    copy.bgra.resize(static_cast<size_t>(across) * down * 4);
    gd::FilterUp(before_.data(), static_cast<uint32_t>(around.right - around.left),
                 static_cast<uint32_t>(around.bottom - around.top), around.left, around.top, s, s,
                 copy.physical.left, copy.physical.top, static_cast<uint32_t>(across), static_cast<uint32_t>(down),
                 copy.bgra.data());

    // The glyphs: kept to the copy and the clip.
    Rect limit = copy.physical;
    if (clipped_)
        limit = Intersect(limit, Rect{static_cast<int32_t>(std::floor(clip_[0] * s)),
                                      static_cast<int32_t>(std::floor(clip_[1] * s)),
                                      static_cast<int32_t>(std::ceil(clip_[2] * s)),
                                      static_cast<int32_t>(std::ceil(clip_[3] * s))});
    const int32_t baselineExtra = static_cast<int32_t>(Load<uint32_t>(At(font_) + font::kLineSpacing) +
                                                       Load<uint32_t>(At(font_) + font::kAscent));
    uint8_t solid[4];
    std::memcpy(solid, colours_, 4);
    for (const Placed& g : glyphs_) {
        const FT_UInt index = FT_Get_Char_Index(face, g.character);
        if (index == 0) {
            if (g.character == 0x20 || g.character == 0xA0)
                continue;
            return; // a character the face lacks: the line stays as the game drew it
        }
        if (FT_Load_Glyph(face, index, FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT) != 0 ||
            FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0)
            return;
        const FT_Bitmap& bm = face->glyph->bitmap;
        const int32_t x0 = static_cast<int32_t>(std::lround(g.penX * s)) + face->glyph->bitmap_left;
        const int32_t y0 = static_cast<int32_t>(std::lround((g.penY + baselineExtra) * s)) - face->glyph->bitmap_top;
        for (uint32_t row = 0; row < bm.rows; ++row) {
            const int32_t y = y0 + static_cast<int32_t>(row);
            if (y < limit.top || y >= limit.bottom)
                continue;
            const uint8_t* cover = bm.buffer + static_cast<ptrdiff_t>(row) * bm.pitch;
            for (uint32_t col = 0; col < bm.width; ++col) {
                const int32_t x = x0 + static_cast<int32_t>(col);
                if (x < limit.left || x >= limit.right || !cover[col])
                    continue;
                uint8_t* out = copy.bgra.data() +
                               (static_cast<size_t>(y - copy.physical.top) * across + (x - copy.physical.left)) * 4;
                if (kind_ == 1) {
                    if (cover[col] > 0x7F)
                        std::memcpy(out, solid, 4);
                } else if (kind_ == 2) {
                    std::memcpy(out, colours_ + std::min<uint32_t>(127, cover[col] >> 1), 4);
                } else {
                    Blend(out, solid, cover[col]);
                }
            }
        }
    }
    Keep(picture_, std::move(copy));
}

} // namespace opensc4::sharptext
