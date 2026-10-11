// City files as the region sees them (docs/region.md), part 2: 0x005DCA10 to
// 0x005DEA80, the pictures a city file carries for the region map.
// Replacement, off by default:
//
//   region.files.part2: storing a file on disk as a picture record, the
//     bounds of the drawn part of a picture, cutting a picture to a rectangle,
//     the average of four pictures, reading the pictures of a save file
//     (SC4::LoadSavePictures and the function under it), writing them, the
//     stock pictures by city size, and the function that makes the pictures
//     when a city is saved.
//
// The records: type 0x8A2482B9, group 0x4A2482BB, instances 0 (the city), 2
// (its mask) and, when there are any, 4 and 6 (a second pair); each is a
// picture file (a PNG) stored whole. Text records (type 0x2026960B, the same
// group) with instances 1, 3, 5 and 7 hold the name the picture file had
// ('Temp.png'); only its extension is used when reading. Pictures go through
// a temporary file both ways because the game's picture reader and writer
// work on files.
//
// Still the game's: 0x005DCCF0 (drawing code: it sets render states, fills
// four vertices per terrain cell and draws them; the renderer's), 0x005DEA50
// and 0x005DEA60 (slots of the vtables at 0x00AA2D9C and 0x00AA2DA0, a class
// whose code follows this block). 0x005DCBF0 and 0x005DE140 are not
// functions: they are inside 0x005DCAD0 and 0x005DDEC0.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::region {
namespace {

constexpr uint32_t kPictureType = 0x8A2482B9;
constexpr uint32_t kPictureGroup = 0x4A2482BB;
constexpr uint32_t kSizeLimit = 0x4C4B40;      // 5,000,000 bytes
constexpr uint32_t kFocusMessage = 0x4348B111;

namespace image { // the game's image buffer, vtable offsets
constexpr std::size_t kAddRef = 0x4;
constexpr std::size_t kRelease = 0x8;
constexpr std::size_t kInit = 0xC;         // (width, height, kind, bits per pixel)
constexpr std::size_t kLock = 0x18;        // (flags)
constexpr std::size_t kUnlock = 0x1C;      // (flags)
constexpr std::size_t kArea = 0x30;        // -> int32_t[4]: left, top, right, bottom
constexpr std::size_t kColourType = 0x3C;  // (int32_t[2] out) -> the same pointer
constexpr std::size_t kFill = 0x48;        // (colour, rectangle out or null)
constexpr std::size_t kGetPixel = 0x54;    // (x, y)
constexpr std::size_t kSetPixel = 0x58;    // (x, y, colour)
constexpr std::size_t kCopy = 0x74;        // (source, source rectangle, target rectangle, clip or null)
constexpr std::size_t kBits = 0x88;
constexpr std::size_t kStride = 0x8C;
} // namespace image

namespace segment { // the save file (a database segment), vtable offsets
constexpr std::size_t kHasRecord = 0x4C;   // (key)
constexpr std::size_t kRecordSize = 0x50;  // (key)
constexpr std::size_t kReadRecord = 0x70;  // (key, buffer, size in and out)
constexpr std::size_t kWriteRecord = 0x74; // (key, buffer, size)
} // namespace segment

// The game's string.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// The game's file object (0x80 bytes).
struct GameFile {
    alignas(4) char bytes[0x80];
};

// The game's picture file reader and writer (0x38 bytes).
struct PictureCodec {
    alignas(4) char bytes[0x38];
};

// The game's image holder with a resource key (0x30 bytes; the image at +4).
struct ImageLoader {
    alignas(4) char bytes[0x30];
};

// The game's stopwatch (0x18 bytes).
struct Stopwatch {
    alignas(4) char bytes[0x18];
};

// A vector of bytes.
struct ByteVector {
    uint8_t* begin;
    uint8_t* end;
    uint8_t* capacity;
};

struct Rect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};
static_assert(sizeof(Rect) == 0x10);

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

uint32_t Free(void* memory)
{
    return Cdecl<uint32_t>(addr::SC4_Free, memory);
}

void AddRef(void* object)
{
    Virtual<uint32_t>(object, image::kAddRef);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, image::kRelease);
}

// An empty string as the original builds it inline: eight bytes from the STL
// allocator.
void InitEmptyBody(GameString& text)
{
    text.begin = nullptr;
    text.end = nullptr;
    text.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.capacity = buffer + 8;
    text.begin = buffer;
    text.end = buffer;
    *buffer = 0;
}

// The inlined destructor.
void DestroyString(GameString& text)
{
    char* const buffer = text.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        Free(buffer);
}

// The game's 0x005DCCF0: draws the mask over the city (the renderer's).
void DrawMask()
{
    Cdecl<void>(addr::SC4CityFiles_DrawPictureMask);
}

// The game's 0x00602180: slot 0x18 of the service at 0x00B43DC4.
void* ServiceObject()
{
    return Cdecl<void*>(addr::SC4_Service43DC4Slot6);
}

// The game's 0x005DCA10 (cdecl, three arguments). Stores a file of under
// 5,000,000 bytes as the picture record `instance` of a save file; true when
// the record was written.
uint8_t __cdecl StoreFile(void* file, void* path, uint32_t instance)
{
    uint8_t stored = 0;
    const uint32_t size = Cdecl<uint32_t>(addr::RZFile_Size, path);
    if (size >= kSizeLimit)
        return stored;
    GameFile source;
    This<void*>(addr::cRZFile_Construct, &source, path);
    if (This<uint8_t>(addr::cRZFile_Open, &source, uint32_t{1}, uint32_t{2}, uint32_t{1})) {
        uint8_t* const data = Cdecl<uint8_t*>(addr::SC4_operator_new, size);
        if (This<uint8_t>(addr::cRZFile_Read, &source, data, size)) {
            uint32_t key[3];
            key[2] = instance;
            key[0] = kPictureType;
            key[1] = kPictureGroup;
            if (Virtual<uint8_t>(file, segment::kWriteRecord, key, data, size))
                stored = 1;
        }
        Cdecl<void>(addr::SC4_operator_delete, data);
        This<uint8_t>(addr::cRZFile_Close, &source);
    }
    This<void>(addr::cRZFile_Destruct, &source);
    return stored;
}

// The game's 0x005DCAD0 (the picture in EBX, the rectangle on the stack,
// caller cleans). Makes every pixel whose red, green and blue ORed together
// are 4 or more opaque and gives the rectangle around those pixels (all zero
// when there is none); false when the picture cannot be locked.
uint8_t __cdecl FindBounds(void* picture, Rect* bounds)
{
    bounds->bottom = 0;
    bounds->right = 0;
    bounds->top = 0;
    bounds->left = 0;
    if (!Virtual<uint8_t>(picture, image::kLock, uint32_t{0x8080}))
        return 0;
    const int32_t* area = Virtual<int32_t*>(picture, image::kArea);
    const int32_t width = area[2] - area[0];
    area = Virtual<int32_t*>(picture, image::kArea);
    const int32_t height = area[3] - area[1];
    uint8_t found = 0;
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            const uint32_t pixel = Virtual<uint32_t>(picture, image::kGetPixel, x, y);
            if (((((pixel >> 8) | pixel) >> 8 | pixel) & 0xFF) < 4)
                continue;
            Virtual<void>(picture, image::kSetPixel, x, y, pixel | 0xFF000000u);
            const int32_t right = x + 1;
            const int32_t bottom = y + 1;
            if (!found) {
                bounds->left = x;
                bounds->top = y;
                bounds->right = right;
                bounds->bottom = bottom;
                found = 1;
                continue;
            }
            bounds->left = bounds->left < x ? bounds->left : x;
            bounds->top = bounds->top < y ? bounds->top : y;
            bounds->right = bounds->right <= right ? right : bounds->right;
            bounds->bottom = bounds->bottom > bottom ? bounds->bottom : bottom;
        }
    }
    Virtual<uint8_t>(picture, image::kUnlock, uint32_t{0x8080});
    return 1;
}

__declspec(naked) void FindBoundsThunk()
{
    __asm push dword ptr [esp + 4]
    __asm push ebx
    __asm call FindBounds
    __asm add esp, 8
    __asm ret
}

// The game's 0x005DCC00 (the picture in EBX, the rectangle in ESI, no stack
// arguments). The part of a picture inside a rectangle as a new picture of
// the same colour type, or the picture itself with one more reference when
// the rectangle is all of it; null when the new picture cannot be made.
void* __cdecl Crop(void* picture, const Rect* part)
{
    Virtual<int32_t*>(picture, image::kArea);
    const int32_t* const area = Virtual<int32_t*>(picture, image::kArea);
    if (part->left == area[0] && part->top == area[1] && part->right == area[2] && part->bottom == area[3]) {
        AddRef(picture);
        return picture;
    }
    void* made; // not cleared by the original either
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C9C), 0xC, &made))
        return nullptr;
    int32_t scratch[2];
    const int32_t kind = *Virtual<int32_t*>(picture, image::kColourType, scratch);
    int32_t type[2];
    type[0] = kind;
    type[1] = This<int32_t>(addr::SC4BufferColorType_DepthOf, type, kind);
    const int32_t height = part->bottom - part->top;
    const int32_t width = part->right - part->left;
    if (Virtual<uint8_t>(made, image::kInit, width, height, type[0], type[1]) &&
        Virtual<uint8_t>(made, image::kLock, uint32_t{0x8010})) {
        const int32_t* const whole = Virtual<int32_t*>(made, image::kArea);
        Virtual<void>(made, image::kCopy, picture, part, whole, uint32_t{0});
        Virtual<uint8_t>(made, image::kUnlock, uint32_t{0x8010});
        return made;
    }
    Release(made);
    return nullptr;
}

__declspec(naked) void CropThunk()
{
    __asm push esi
    __asm push ebx
    __asm call Crop
    __asm add esp, 8
    __asm ret
}

// The game's 0x005DD180 (cdecl, one argument: four pictures of one size). A
// new 32-bit picture of the first one's size whose pixels are the rounded
// average of the four, red, green and blue each (the top byte comes out 0);
// null when the picture cannot be made. When a lock fails the new picture is
// returned as it is.
void* __cdecl AverageFour(void** pictures)
{
    const int32_t* const area = Virtual<int32_t*>(pictures[0], image::kArea);
    const int32_t width = area[2] - area[0];
    const int32_t height = area[3] - area[1];
    void* made = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C9C), 0xC, &made) ||
        !Virtual<uint8_t>(made, image::kInit, width, height, int32_t{9}, int32_t{0x20})) {
        if (made)
            Release(made);
        return nullptr;
    }
    if (Virtual<uint8_t>(made, image::kLock, uint32_t{0x8040})) {
        if (Virtual<uint8_t>(pictures[0], image::kLock, uint32_t{0x40})) {
            if (Virtual<uint8_t>(pictures[1], image::kLock, uint32_t{0x40})) {
                if (Virtual<uint8_t>(pictures[2], image::kLock, uint32_t{0x40})) {
                    if (Virtual<uint8_t>(pictures[3], image::kLock, uint32_t{0x40})) {
                        uint8_t* target = Virtual<uint8_t*>(made, image::kBits);
                        uint8_t* first = Virtual<uint8_t*>(pictures[0], image::kBits);
                        uint8_t* second = Virtual<uint8_t*>(pictures[1], image::kBits);
                        uint8_t* third = Virtual<uint8_t*>(pictures[2], image::kBits);
                        uint8_t* fourth = Virtual<uint8_t*>(pictures[3], image::kBits);
                        const int32_t targetStride = Virtual<int32_t>(made, image::kStride);
                        const int32_t firstStride = Virtual<int32_t>(pictures[0], image::kStride);
                        const int32_t secondStride = Virtual<int32_t>(pictures[1], image::kStride);
                        const int32_t thirdStride = Virtual<int32_t>(pictures[2], image::kStride);
                        const int32_t fourthStride = Virtual<int32_t>(pictures[3], image::kStride);
                        for (int32_t row = height; row > 0; --row) {
                            for (int32_t x = 0; x < width; ++x) {
                                const uint32_t a = reinterpret_cast<const uint32_t*>(first)[x];
                                const uint32_t c = reinterpret_cast<const uint32_t*>(third)[x];
                                const uint32_t d = reinterpret_cast<const uint32_t*>(fourth)[x];
                                const uint32_t b = reinterpret_cast<const uint32_t*>(second)[x];
                                uint32_t sum = (d & 0x30303) + (c & 0x30303) + (b & 0x30303) + (a & 0x30303) + 2;
                                sum &= 0xC0C0C;
                                sum += d & 0xFCFCFC;
                                sum += c & 0xFCFCFC;
                                sum += b & 0xFCFCFC;
                                sum += a & 0xFCFCFC;
                                reinterpret_cast<uint32_t*>(target)[x] = sum >> 2;
                            }
                            target += targetStride;
                            first += firstStride;
                            second += secondStride;
                            fourth += fourthStride;
                            third += thirdStride;
                        }
                        Virtual<uint8_t>(pictures[3], image::kUnlock, uint32_t{0x40});
                    }
                    Virtual<uint8_t>(pictures[2], image::kUnlock, uint32_t{0x40});
                }
                Virtual<uint8_t>(pictures[1], image::kUnlock, uint32_t{0x40});
            }
            Virtual<uint8_t>(pictures[0], image::kUnlock, uint32_t{0x40});
        }
        Virtual<uint8_t>(made, image::kUnlock, uint32_t{0x8040});
    }
    // The holder is copied into the result and then destroyed.
    void* const result = made;
    if (made)
        AddRef(made);
    if (made)
        Release(made);
    return result;
}

// One picture record of a save file after the first: read into the byte
// vector, written out as the temporary file and loaded from it into `holder`.
// `loaded` changes only when the record's bytes were read and the temporary
// file could be opened.
void ReadFurtherPicture(void* file, uint32_t* key, uint32_t instance, ByteVector& bytes, uint32_t& size,
                        GameString& temporary, void** holder, uint8_t& loaded)
{
    key[2] = instance;
    if (!Virtual<uint8_t>(file, segment::kHasRecord, key))
        return;
    size = Virtual<uint32_t>(file, segment::kRecordSize, key);
    if (size >= kSizeLimit)
        return;
    This<void>(addr::SC4ByteVector_ResizeZeroed, &bytes, size);
    uint8_t* const data = bytes.begin;
    if (Virtual<uint32_t>(file, segment::kReadRecord, key, data, &size) >= kSizeLimit)
        return;
    GameFile target;
    This<void*>(addr::cRZFile_Construct, &target, &temporary);
    if (This<uint8_t>(addr::cRZFile_Open, &target, uint32_t{2}, uint32_t{3}, uint32_t{1})) {
        loaded = This<uint8_t>(addr::cRZFile_Write, &target, data, size);
        This<uint8_t>(addr::cRZFile_Close, &target);
        if (loaded) {
            void** const cleared = This<void**>(addr::SC4RefPtr_Reset, holder);
            loaded = Cdecl<uint8_t>(addr::SC4CityFiles_LoadPictureFile, &temporary, cleared);
        }
    }
    This<void>(addr::cRZFile_Destruct, &target);
}

// The game's 0x005DD480 (cdecl, five arguments). Reads the pictures of a save
// file: records 0 and 2 into `first` and `second` (both cleared at the start,
// without a release) and, when the holders are given, records 4 and 6 into
// `third` and `fourth`. The first record must be there and load; a later one
// that is missing or too big is passed over. True when nothing failed.
uint8_t __cdecl ReadPictures(void* file, void** first, void** second, void** third, void** fourth)
{
    *first = nullptr;
    *second = nullptr;
    uint8_t loaded = 0;
    void* firstPicture = nullptr;
    void* secondPicture = nullptr;
    void* thirdPicture = nullptr;
    void* fourthPicture = nullptr;
    uint32_t key[3] = {kPictureType, kPictureGroup, 0};
    if (Virtual<uint8_t>(file, segment::kHasRecord, key)) {
        GameString temporary;
        InitEmptyBody(temporary);
        temporary.vtable = addr::SC4String_Vtable;
        temporary.zero = 0;
        GameString extension;
        This<void*>(addr::SC4String_FromText, &extension, Global<const char*>(addr::SC4CityFiles_PictureExtension));
        GameString name;
        InitEmptyBody(name);
        name.vtable = addr::SC4String_Vtable;
        name.zero = 0;
        if (Cdecl<uint8_t>(addr::SC4Resource_LoadText, file, kPictureGroup, uint32_t{1}, &name))
            Cdecl<uint8_t>(addr::RZFile_Extension, &name, &extension);
        if (Cdecl<uint8_t>(addr::RZFile_MakeTempName, &temporary, &extension)) {
            uint32_t size = Virtual<uint32_t>(file, segment::kRecordSize, key);
            ByteVector bytes = {nullptr, nullptr, nullptr};
            if (size < kSizeLimit) {
                uint8_t zero = 0;
                This<void>(addr::SC4ByteVector_InsertN, &bytes, static_cast<uint8_t*>(nullptr), size, &zero);
                uint8_t* const data = bytes.begin;
                if (Virtual<uint32_t>(file, segment::kReadRecord, key, data, &size) < kSizeLimit) {
                    GameFile target;
                    This<void*>(addr::cRZFile_Construct, &target, &temporary);
                    if (This<uint8_t>(addr::cRZFile_Open, &target, uint32_t{2}, uint32_t{3}, uint32_t{1})) {
                        loaded = This<uint8_t>(addr::cRZFile_Write, &target, data, size);
                        This<uint8_t>(addr::cRZFile_Close, &target);
                        if (loaded) {
                            void** const cleared = This<void**>(addr::SC4RefPtr_Reset, &firstPicture);
                            loaded = Cdecl<uint8_t>(addr::SC4CityFiles_LoadPictureFile, &temporary, cleared);
                        }
                    }
                    This<void>(addr::cRZFile_Destruct, &target);
                    if (loaded) {
                        ReadFurtherPicture(file, key, 2, bytes, size, temporary, &secondPicture, loaded);
                        if (loaded) {
                            if (third)
                                ReadFurtherPicture(file, key, 4, bytes, size, temporary, &thirdPicture, loaded);
                            if (loaded && fourth)
                                ReadFurtherPicture(file, key, 6, bytes, size, temporary, &fourthPicture, loaded);
                        }
                    }
                }
            }
            Cdecl<uint8_t>(addr::RZFile_Remove, &temporary);
            if (bytes.begin)
                Free(bytes.begin);
        }
        DestroyString(name);
        DestroyString(extension);
        DestroyString(temporary);
        if (loaded) {
            if (firstPicture)
                AddRef(firstPicture);
            *first = firstPicture;
            if (secondPicture)
                AddRef(secondPicture);
            *second = secondPicture;
            if (third) {
                if (thirdPicture)
                    AddRef(thirdPicture);
                *third = thirdPicture;
            }
            if (fourth) {
                if (fourthPicture)
                    AddRef(fourthPicture);
                *fourth = fourthPicture;
            }
        }
    }
    if (fourthPicture)
        Release(fourthPicture);
    if (thirdPicture)
        Release(thirdPicture);
    if (secondPicture)
        Release(secondPicture);
    if (firstPicture)
        Release(firstPicture);
    return loaded;
}

// The game's 0x005DDA40 (cdecl, five arguments). Opens a save file by its
// path (0x005DBFE0 with 0 and 1), reads its pictures and closes it
// (0x005DBC10); false when the file does not open.
uint8_t __cdecl LoadSavePictures(void* path, void** first, void** second, void** third, void** fourth)
{
    uint8_t loaded = 0;
    void* const file = Cdecl<void*>(addr::SC4CityFiles_OpenSegment, path, uint32_t{0}, uint32_t{1});
    if (file) {
        loaded = ReadPictures(file, first, second, third, fourth);
        Cdecl<void>(addr::SC4CityFiles_CloseSegment, file);
    }
    return loaded;
}

// The text record that goes with a picture: 'Temp' and the extension.
uint8_t WritePictureName(void* file, uint32_t instance, const GameString& extension)
{
    GameString name;
    This<void*>(addr::SC4String_FromText, &name, "Temp");
    char* const end = extension.end;
    char* const begin = extension.begin;
    uint8_t tag;
    This<void>(addr::SC4String_AppendRange, &name.begin, begin, end, &tag);
    const uint8_t written = Cdecl<uint8_t>(addr::SC4Resource_SaveText, file, kPictureGroup, instance, &name);
    DestroyString(name);
    return written;
}

// The game's 0x005DDA90 (cdecl, five arguments). Writes up to four pictures
// into a save file as records 0, 2, 4 and 6, each saved as a temporary PNG
// file (quality 100) and stored whole, with its text record after it. The
// first picture is not checked for null; the others are skipped when null. A
// later picture that cannot be saved as a file leaves the result as it was
// and still gets its text record.
uint8_t __cdecl WritePictures(void* file, void* first, void* second, void* third, void* fourth)
{
    uint8_t written = 0;
    GameString temporary;
    InitEmptyBody(temporary);
    const char* const text = Global<const char*>(addr::SC4CityFiles_PictureExtension);
    temporary.vtable = addr::SC4String_Vtable;
    temporary.zero = 0;
    GameString extension;
    extension.begin = nullptr;
    extension.end = nullptr;
    extension.capacity = nullptr;
    const char* textEnd = text;
    while (*textEnd)
        ++textEnd;
    uint8_t tag;
    This<void>(addr::SC4String_FromRange, &extension.begin, text, textEnd, &tag);
    extension.vtable = addr::SC4String_Vtable;
    extension.zero = 0;
    if (Cdecl<uint8_t>(addr::RZFile_MakeTempName, &temporary, &extension)) {
        PictureCodec codec;
        This<void*>(addr::cGZImageReader_Construct, &codec);
        if (This<uint8_t>(addr::cGZImageReader_Save, &codec, &temporary, first, uint32_t{0x64})) {
            written = StoreFile(file, &temporary, 0);
            if (written)
                written = WritePictureName(file, 1, extension);
        }
        void* const others[3] = {second, third, fourth};
        for (uint32_t i = 0; i < 3; ++i) {
            void* const picture = others[i];
            if (!picture || !written)
                continue;
            const uint32_t instance = 2 + 2 * i;
            if (This<uint8_t>(addr::cGZImageReader_Save, &codec, &temporary, picture, uint32_t{0x64}))
                written = StoreFile(file, &temporary, instance);
            if (written)
                written = WritePictureName(file, instance + 1, extension);
        }
        Cdecl<uint8_t>(addr::RZFile_Remove, &temporary);
        This<void>(addr::cGZImageReader_Destruct, &codec);
    }
    DestroyString(extension);
    DestroyString(temporary);
    return written;
}

// The game's 0x005DDDC0 (cdecl, one argument). Writes the stock pictures for
// the open city's size (the city's slot 0x15C: 0 small, 1 medium, 2 large)
// into a save file: images of group 0x6A1EED2C in 0x856DDBAC, the city's
// picture and its mask. False when either image is missing.
uint8_t __cdecl WriteStockPictures(void* file)
{
    const uint32_t size = Virtual<uint32_t>(Global<void*>(addr::SC4_City), 0x15C);
    // The two tables sit one after the other in the original's frame.
    const uint32_t instances[6] = {0x6A231947, 0xEA23195E, 0x0A2312D8, 0x6A231946, 0xEA23195D, 0x0A2312D9};
    uint32_t pictureKey[3];
    uint32_t maskKey[3];
    pictureKey[1] = 0x6A1EED2C;
    maskKey[1] = 0x6A1EED2C;
    const uint32_t maskInstance = instances[size];
    pictureKey[0] = 0x856DDBAC;
    maskKey[0] = 0x856DDBAC;
    const uint32_t pictureInstance = instances[3 + size];
    maskKey[2] = maskInstance;
    pictureKey[2] = pictureInstance;
    ImageLoader picture;
    This<void*>(addr::SC4ImageLoader_ConstructKey, &picture, pictureKey, uint32_t{9}, uint32_t{0});
    ImageLoader mask;
    This<void*>(addr::SC4ImageLoader_ConstructKey, &mask, maskKey, uint32_t{9}, uint32_t{0});
    void* const pictureImage = At<void*>(&picture, 0x4);
    void* const maskImage = pictureImage ? At<void*>(&mask, 0x4) : nullptr;
    if (!pictureImage || !maskImage) {
        This<void>(addr::SC4ImageLoader_Destruct, &mask);
        This<void>(addr::SC4ImageLoader_Destruct, &picture);
        return 0;
    }
    const uint8_t written = WritePictures(file, pictureImage, maskImage, nullptr, nullptr);
    This<void>(addr::SC4ImageLoader_Destruct, &mask);
    This<void>(addr::SC4ImageLoader_Destruct, &picture);
    return written;
}

// A new message of the given type, sent at once; the reference is the
// caller's.
void* SendNewMessage(uint32_t type)
{
    void* message = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
    if (message) {
        message = This<void*>(addr::cGZMessage_cGZMessage, message);
        if (message)
            AddRef(message);
    }
    Virtual<void>(message, 0x14, type);
    Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0xC, message);
    return message;
}

// Whether the application is in front (0x005F85F0), noted in the watcher as
// the original does after every question.
uint8_t AskInFront(char* watcher)
{
    const uint8_t inFront = Cdecl<uint8_t>(addr::SC4_IsAppInFront);
    At<uint8_t>(watcher, 0x8) = inFront;
    if (!inFront)
        At<uint8_t>(watcher, 0x9) = 1;
    return inFront;
}

// The game's 0x005DDEC0 (cdecl, one argument; 0x005DE140 is inside it). Makes
// the region pictures of the open city and writes them into a save file. It
// hides the interface, sets the view up (the camera at the middle of the city
// with zoom factor 0.25, a 64 by 64 black picture in the view's slot 0xD8),
// and takes sixteen screen shots of the middle of the view: for each of four
// small shifts of the view (the table at 0x00AA2B8C) the city and then the
// city under the mask 0x005DCCF0 draws, once before and once after the
// service's slot 0x10 is called with 0xF3. Each four are averaged, cut to
// the rows FindBounds finds in the first average, and written with
// WritePictures. Everything is then put back. When the application was not
// in front at some point, it waits up to 10 stopwatch units for it and then
// runs again (at most twice more) or writes the stock pictures; the stock
// pictures are also written when anything fails, with false returned.
uint8_t __cdecl MakePictures(void* file)
{
    int32_t& depth = Global<int32_t>(addr::SC4CityFiles_PictureDepth);
    uint8_t made = 1;
    ++depth;
    char* watcher = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0xC});
    if (watcher) {
        At<std::uintptr_t>(watcher, 0x0) = addr::SC4FocusWatcher_Vtable;
        At<uint32_t>(watcher, 0x4) = 0;
        At<uint8_t>(watcher, 0x8) = 1;
        At<uint8_t>(watcher, 0x9) = 0;
        AskInFront(watcher);
        AddRef(watcher);
    }
    Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x14, watcher, kFocusMessage);

    void* const buffers = Global<void*>(addr::SC4_Service43C9C);
    void* const service = Global<void*>(addr::SC4_Service43DC4);
    void* const view = Global<void*>(addr::SC4_View43DD0);
    void* const pump = Global<void*>(addr::SC4_Service43C94);
    char* const cameraView = Global<char*>(addr::SC4_CameraView);
    void* const city = Global<void*>(addr::SC4_City);
    char* const camera = Virtual<char*>(view, 0x40);
    void* const preferences = Global<void*>(addr::SC4_App43CC4);
    void* const lighting = Virtual<void*>(view, 0x24);
    void* const picker = Cdecl<void*>(addr::SC4_TerrainPicker);
    const uint32_t zero = Global<uint32_t>(addr::SC4CityFiles_ZeroB23050);
    uint32_t savedValue = 0;
    void* savedFilter = nullptr;
    uint32_t savedCentre[3] = {zero, zero, zero};
    void* savedPicture = nullptr;
    Cdecl<void>(addr::SC4UI_SwitchInterface, uint32_t{1}, uint32_t{1}, uint32_t{1}, uint32_t{1}, uint32_t{1});
    Virtual<void>(pump, 0x14, uint32_t{0x14}, uint32_t{0x64}, 0xFFFFFFFFu);
    // Loaded as a float and truncated back by the original (fild, ftol).
    const int32_t savedFirst =
        static_cast<int32_t>(static_cast<int64_t>(static_cast<double>(At<int32_t>(camera, 0x108))));
    const uint32_t savedSecond = At<uint32_t>(camera, 0x10C);
    Virtual<void>(service, 0x50, &savedValue);
    Virtual<void>(service, 0x48, &savedFilter);
    savedCentre[0] = At<uint32_t>(camera, 0x44);
    savedCentre[1] = At<uint32_t>(camera, 0x48);
    savedCentre[2] = At<uint32_t>(camera, 0x4C);
    const float savedZoom = static_cast<float>(This<double>(addr::cSC4CameraControl_GetZoomFactor, camera));
    uint32_t savedPictureValue = 0; // the original leaves it for the view to fill
    void* const shown = Virtual<void*>(view, 0xD0, &savedPictureValue);
    if (shown) {
        AddRef(shown);
        savedPicture = shown;
    }
    const float savedLight = static_cast<float>(Virtual<double>(lighting, 0x14));
    const uint8_t savedPickerFlag = Virtual<uint8_t>(picker, 0x2C);
    Virtual<void>(picker, 0x30, uint32_t{0});
    Virtual<void>(preferences, 0x24, uint32_t{0x1C}, uint32_t{1});
    Virtual<void>(lighting, 0x18, 12.0f);
    Virtual<void>(ServiceObject(), 0x10, uint32_t{0});

    {
        void* black = nullptr;
        if (Virtual<uint8_t>(buffers, 0xC, &black) &&
            Virtual<uint8_t>(black, image::kInit, int32_t{0x40}, int32_t{0x40}, int32_t{9}, int32_t{0x20})) {
            if (Virtual<uint8_t>(black, image::kLock, uint32_t{0x8001})) {
                Virtual<void>(black, image::kFill, uint32_t{0}, uint32_t{0});
                Virtual<uint8_t>(black, image::kUnlock, uint32_t{0x8001});
            }
            Virtual<void>(view, 0xD8, black, uint32_t{0});
        } else {
            made = 0;
        }
        if (black)
            Release(black);
    }

    if (made) {
        char* filter = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x10});
        if (filter) {
            This<void*>(addr::SC4_OccupantFilterBase, filter);
            At<std::uintptr_t>(filter, 0x0) = addr::SC4CityFiles_PictureFilterVtable;
            At<std::uintptr_t>(filter, 0x4) = addr::SC4CityFiles_PictureFilterVtable4;
            At<uint8_t>(filter, 0xC) = 0;
            At<uint8_t>(filter, 0xD) = 0;
            At<uint8_t>(filter, 0xE) = 0;
            AddRef(filter);
        }
        uint32_t one = 1;
        void* filterHolder = filter;
        void* const before = SendNewMessage(0x4AB04CBE);
        Virtual<void>(service, 0x44, uint32_t{0}, uint32_t{0});
        Virtual<void>(pump, 0x14, uint32_t{0x14}, uint32_t{0x64}, 0xFFFFFFFFu);
        Virtual<void>(service, 0x54, &one);
        Virtual<void>(pump, 0x14, uint32_t{0x14}, uint32_t{0x64}, 0xFFFFFFFFu);
        Virtual<void>(service, 0x4C, &filterHolder);
        Virtual<void>(pump, 0x14, uint32_t{0x14}, uint32_t{0x64}, 0xFFFFFFFFu);
        void* const after = SendNewMessage(0x4AB04CBF);
        Release(after);
        Release(before);
        if (filter)
            Release(filter);

        const float sizeX = static_cast<float>(Virtual<double>(city, 0x164));
        const double sizeZ = Virtual<double>(city, 0x168);
        float centre[3];
        centre[1] = 0.0f;
        const double half = static_cast<double>(Global<float>(addr::SC4_FloatHalf));
        centre[0] = static_cast<float>(static_cast<double>(sizeX) * half);
        centre[2] = static_cast<float>(sizeZ * half);
        This<void>(addr::cSC4CameraControl_SetZoomFactor, camera, 0.25f);
        This<void>(addr::cSC4CameraControl_SetCenter, camera, centre);
        Virtual<void>(pump, 0x14, uint32_t{0x14}, uint32_t{0x64}, 0xFFFFFFFFu);

        const uint32_t citySize = Virtual<uint32_t>(city, 0x15C);
        const int32_t middle = At<int32_t>(cameraView, 0x28) / 2;
        const uint32_t halfWidth = Global<uint32_t>(addr::SC4CityFiles_PictureWidths + citySize * 4) >> 1;
        int32_t left = middle - static_cast<int32_t>(halfWidth);
        int32_t right = static_cast<int32_t>(halfWidth) + middle + 1;
        const int32_t bottom =
            At<int32_t>(cameraView, 0x2C) / 2 + Global<int32_t>(addr::SC4CityFiles_PictureOffsets + citySize * 4);
        --left;
        const int32_t width = right - left;

        void* plainShots[4] = {nullptr, nullptr, nullptr, nullptr};
        void* maskShots[4] = {nullptr, nullptr, nullptr, nullptr};
        void* secondPlainShots[4] = {nullptr, nullptr, nullptr, nullptr};
        void* secondMaskShots[4] = {nullptr, nullptr, nullptr, nullptr};
        const uint32_t* const shifts = &Global<uint32_t>(addr::SC4CityFiles_ViewShifts);

        bool firstRoundDone = true;
        for (uint32_t i = 0; i < 4; ++i) {
            Virtual<void>(view, 0x34, shifts[i * 2], shifts[i * 2 + 1]);
            Virtual<void>(view, 0x50);
            Virtual<void>(view, 0x54);
            plainShots[i] = Cdecl<void*>(addr::SC4UI_Snapshot, left, int32_t{0}, width, bottom);
            if (!plainShots[i]) {
                firstRoundDone = false;
                break;
            }
            DrawMask();
            maskShots[i] = Cdecl<void*>(addr::SC4UI_Snapshot, left, int32_t{0}, width, bottom);
            if (!maskShots[i]) {
                firstRoundDone = false;
                break;
            }
        }
        if (!firstRoundDone) {
            made = 0;
        } else {
            Virtual<void>(ServiceObject(), 0x10, uint32_t{0xF3});
            Virtual<void>(pump, 0x14, uint32_t{0x14}, uint32_t{0x64}, 0xFFFFFFFFu);
            for (uint32_t i = 0; i < 4; ++i) {
                Virtual<void>(view, 0x34, shifts[i * 2], shifts[i * 2 + 1]);
                Virtual<void>(view, 0x50);
                Virtual<void>(view, 0x54);
                secondPlainShots[i] = Cdecl<void*>(addr::SC4UI_Snapshot, left, int32_t{0}, width, bottom);
                if (!secondPlainShots[i]) {
                    made = 0;
                    break;
                }
                DrawMask();
                secondMaskShots[i] = Cdecl<void*>(addr::SC4UI_Snapshot, left, int32_t{0}, width, bottom);
                if (!secondMaskShots[i]) {
                    made = 0;
                    break;
                }
            }
            Virtual<void>(ServiceObject(), 0x14, uint32_t{0xF3});
        }
        Virtual<void>(view, 0x34, uint32_t{0}, uint32_t{0});

        if (made) {
            void* four[4];
            four[1] = plainShots[1];
            four[0] = plainShots[0];
            four[2] = plainShots[2];
            four[3] = plainShots[3];
            void* plain = AverageFour(four);
            four[1] = maskShots[1];
            four[0] = maskShots[0];
            four[2] = maskShots[2];
            four[3] = maskShots[3];
            void* mask = AverageFour(four);
            four[0] = secondPlainShots[0];
            four[1] = secondPlainShots[1];
            four[2] = secondPlainShots[2];
            four[3] = secondPlainShots[3];
            void* secondPlain = AverageFour(four);
            four[1] = secondMaskShots[1];
            four[0] = secondMaskShots[0];
            four[2] = secondMaskShots[2];
            four[3] = secondMaskShots[3];
            void* secondMask = AverageFour(four);
            if (plain && mask) {
                right -= left;
                left = 0;
                Rect bounds; // the original reuses the four pointers' place
                Rect* const part = &bounds;
                if (FindBounds(plain, part)) {
                    part->right = right;
                    part->left = left;
                    part->bottom = bottom;
                    if (part->top != 0) {
                        void* const oldPlain = plain;
                        void* const cutPlain = Crop(oldPlain, part);
                        if (cutPlain && cutPlain != oldPlain) {
                            AddRef(cutPlain);
                            plain = cutPlain;
                            if (oldPlain)
                                Release(oldPlain);
                        }
                        void* const oldMask = mask;
                        void* const cutMask = Crop(oldMask, part);
                        if (cutMask && cutMask != oldMask) {
                            AddRef(cutMask);
                            mask = cutMask;
                            if (oldMask)
                                Release(oldMask);
                        }
                        if (secondPlain && secondMask) {
                            void* cutSecondPlain = Crop(secondPlain, part);
                            if (cutSecondPlain)
                                This<void>(addr::SC4RefPtr_AssignFrom, &secondPlain, &cutSecondPlain);
                            void* cutSecondMask = Crop(secondMask, part);
                            if (cutSecondMask) {
                                This<void>(addr::SC4RefPtr_AssignFrom, &secondMask, &cutSecondMask);
                                Release(cutSecondMask);
                            }
                            if (cutSecondPlain)
                                Release(cutSecondPlain);
                        }
                        if (cutMask)
                            Release(cutMask);
                        if (cutPlain)
                            Release(cutPlain);
                    }
                }
                made = WritePictures(file, plain, mask, secondPlain, secondMask);
            } else {
                made = 0;
            }
            if (secondMask)
                Release(secondMask);
            if (secondPlain)
                Release(secondPlain);
            if (mask)
                Release(mask);
            if (plain)
                Release(plain);
        }

        for (int32_t i = 3; i >= 0; --i) {
            if (secondMaskShots[i])
                Release(secondMaskShots[i]);
        }
        for (int32_t i = 3; i >= 0; --i) {
            if (secondPlainShots[i])
                Release(secondPlainShots[i]);
        }
        for (int32_t i = 3; i >= 0; --i) {
            if (maskShots[i])
                Release(maskShots[i]);
        }
        for (int32_t i = 3; i >= 0; --i) {
            if (plainShots[i])
                Release(plainShots[i]);
        }
    }
    if (!made)
        WriteStockPictures(file);

    Virtual<void>(preferences, 0x24, uint32_t{0x1C}, uint32_t{0});
    Virtual<void>(lighting, 0x18, savedLight);
    Virtual<void>(picker, 0x30, uint32_t{savedPickerFlag});
    Virtual<void>(view, 0xD8, savedPicture, savedPictureValue);
    This<void>(addr::cSC4CameraControl_SetCenter, camera, savedCentre);
    This<void>(addr::cSC4CameraControl_SetZoomFactor, camera, savedZoom);
    Virtual<void>(service, 0x54, &savedValue);
    Virtual<void>(service, 0x4C, &savedFilter);
    if (savedFilter)
        Release(savedFilter);
    Virtual<void>(service, 0x44, savedFirst, savedSecond);
    Virtual<void>(ServiceObject(), 0x14, uint32_t{0});
    Virtual<void>(pump, 0x14, uint32_t{0x14}, uint32_t{0x64}, 0xFFFFFFFFu);
    Virtual<void>(view, 0x50);
    Virtual<void>(view, 0x54);
    Cdecl<void>(addr::SC4UI_SwitchInterface, uint32_t{0}, uint32_t{1}, uint32_t{1}, uint32_t{1}, uint32_t{1});

    AskInFront(watcher);
    if (At<uint8_t>(watcher, 0x9)) {
        Stopwatch stopwatch;
        This<void*>(addr::SC4Stopwatch_Construct, &stopwatch, uint32_t{5});
        This<void>(addr::SC4Stopwatch_Start, &stopwatch);
        for (;;) {
            if (AskInFront(watcher))
                break;
            if (This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, &stopwatch) >= 10)
                break;
            Cdecl<void>(addr::RZ_SleepMicroseconds, uint32_t{0x7A120});
        }
        if (AskInFront(watcher) && !(depth > 2))
            made = MakePictures(file);
        else
            made = WriteStockPictures(file);
        This<void>(addr::SC4Stopwatch_Destruct, &stopwatch);
    }
    Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x18, watcher, kFocusMessage);
    --depth;
    if (savedPicture)
        Release(savedPicture);
    Release(watcher);
    return made;
}

bool InstallFilesPart2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4CityFiles_StoreFileAsPicture, reinterpret_cast<void*>(&StoreFile)},
        {addr::SC4CityFiles_FindPictureBounds, reinterpret_cast<void*>(&FindBoundsThunk)},
        {addr::SC4CityFiles_CropPicture, reinterpret_cast<void*>(&CropThunk)},
        {addr::SC4CityFiles_AveragePictures, reinterpret_cast<void*>(&AverageFour)},
        {addr::SC4CityFiles_ReadPictures, reinterpret_cast<void*>(&ReadPictures)},
        {addr::SC4_LoadSavePictures, reinterpret_cast<void*>(&LoadSavePictures)},
        {addr::SC4CityFiles_WritePictures, reinterpret_cast<void*>(&WritePictures)},
        {addr::SC4CityFiles_WriteStockPictures, reinterpret_cast<void*>(&WriteStockPictures)},
        {addr::SC4CityFiles_MakePictures, reinterpret_cast<void*>(&MakePictures)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static void* originals[count] = {};
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_filesPart2Registered = registry::Register({
    "region.files.part2",
    "region",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4_LoadSavePictures,
    "Our city file pictures: reading, writing and making the region pictures of a city (docs/region.md)",
    false,
    InstallFilesPart2,
});

} // namespace
} // namespace opensc4::region
