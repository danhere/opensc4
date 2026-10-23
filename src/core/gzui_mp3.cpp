// The GZ framework's MP3 decoder director 0x1CEC20BE ("HitMP3Decoder") and
// its classes, and the UI script's root holder 0x5A354595 (docs/gzui.md,
// part u10), on the game's own objects. The director registers two classes:
// the MP3 stream 0x3CEC2B47 (0x1104 bytes), which reads an MP3 file through
// a cRZFile at +0x20 frame by frame, gives each frame to a decoder (the
// game's mpglib-like state, 0x4A60 bytes) and hands out 16-bit PCM, and a
// maker 0x9CEC2B58 (0xC bytes) whose slot 3 makes a stream for a path. The
// root holder keeps the windows a UI script made (an STLport vector of
// pointers at +8). Replacements, each off by default:
//
//   framework.mp3.object     the director's constructor, deleting
//                            destructor and getter; the maker's
//                            constructor, QueryInterface, destructors,
//                            factory and slot 3
//   framework.mp3.stream     the stream's constructor, destructors and
//                            factory, Release, the file's slots it passes
//                            on, the getters and setters, Read, Rewind,
//                            Seek, Close and the three ways to open
//   framework.mp3.decode     the decoder's feed, decode and reset, the
//                            frame header and the ID3 tag's size
//   framework.uiscript.root  the root holder's QueryInterface and Release,
//                            its windows by id and by index, the calls to
//                            each, the count, the rectangle test, the
//                            vector's growth and its copies
//
// Still the game's: the thunks 0x0094A432 and 0x0094A972, the "false" stub
// 0x0094A5EA, the director's ID getter 0x0094AC21, and the root holder's
// AddRef 0x0094BC3F and slot 3 0x0094BC5D (both folded with other classes'
// vtables). Ours call each other directly where the game does; a virtual
// call stays one.
//
// Every detour is entered through a thunk that keeps ECX and EDX. The
// groups go in at Attach, with the UI script loader's.
#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::gzui {
namespace {

// ====================================================================================
// Helpers

template <typename R, typename... Args>
inline R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
inline R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

template <typename T>
inline T& At(void* base, std::size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

inline uint32_t Bits(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(pointer)); }

// A byte answer over what the game leaves in the rest of EAX.
inline uint32_t Answer(uint32_t eax, bool value) { return (eax & ~0xFFu) | (value ? 1u : 0u); }
inline bool Yes(uint32_t eax) { return (eax & 0xFF) != 0; }

inline uint32_t AddRef(void* object) { return Virtual<uint32_t>(object, 0x4); }
inline uint32_t Release(void* object) { return Virtual<uint32_t>(object, 0x8); }

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("gzui: mp3: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

// A cdecl detour entered with ECX and EDX kept: `Target` is __cdecl with
// `ArgBytes` bytes of arguments, which the caller pops.
#define OPENSC4_CDECL_THUNK(Name, Target, ArgBytes)                                                            \
    __declspec(naked) void Name()                                                                              \
    {                                                                                                          \
        __asm push ecx                                                                                         \
        __asm push edx                                                                                         \
        OPENSC4_REPUSH_##ArgBytes                                                                              \
        __asm call Target                                                                                      \
        __asm add esp, ArgBytes                                                                                \
        __asm pop edx                                                                                          \
        __asm pop ecx                                                                                          \
        __asm ret                                                                                              \
    }

// A game string on the stack (0x14 bytes), freed as the game's methods do
// it inline: the vtable back and the text's buffer freed.
struct LocalText {
    alignas(4) char bytes[0x14];
    void* get() { return bytes; }
    // The inline destructor's EAX: the free's when there was a buffer.
    void Free(uint32_t& eax)
    {
        At<std::uintptr_t>(bytes, 0) = addr::SC4String_Vtable;
        if (void* const text = At<void*>(bytes, 4))
            eax = CallCdecl<uint32_t>(addr::SC4_Free, text);
    }
};

// ====================================================================================
// The decoder (the game's mpglib-like state, 0x4A60 bytes; its constructor
// 0x00A667B4 and destructor 0x00A66BBE stay the game's).

constexpr std::size_t kDecPcm = 0x1244;        // the frame's samples, interleaved
constexpr std::size_t kDecPcmBytes = 0x248C;   // bytes a frame decodes to
constexpr std::size_t kDecSkipIndex = 0x2490;  // byte: the first frame's index into 0x00AD4D20
constexpr std::size_t kDecFrameSize = 0x2498;  // the frame's size less its 4-byte header
constexpr std::size_t kDecFrameStart = 0x24A4;
constexpr std::size_t kDecFrameBits = 0x24AC;
constexpr std::size_t kDecChannels = 0x4A40;
constexpr std::size_t kDecInput = 0x4A44;      // the input's next byte
constexpr std::size_t kDecInputLeft = 0x4A48;  // the input's bytes left
constexpr std::size_t kDecLimit = 0x4A4C;      // samples still wanted (negative: no limit)
constexpr std::size_t kDecReady = 0x4A50;      // samples decoded, not yet given
constexpr std::size_t kDecReadAt = 0x4A54;     // the next one's index
constexpr std::size_t kDecSkip = 0x4A58;       // samples to drop
constexpr std::size_t kDecFirst = 0x4A5C;      // byte: the next frame is the first

// The game's 0x0094A079 (`ret 0xC`): a frame given to the decoder. -1 while
// it still holds input, -2 for fewer than 4 bytes or a header the decoder
// refuses (0x00A667FE with the big-endian first dword), else 0 and the
// input and the limit kept.
int32_t __fastcall DecoderFeed(char* self, void*, const uint8_t* data, int32_t size, int32_t limit)
{
    OPENSC4_FIRST("DecoderFeed");
    if (At<int32_t>(self, kDecInputLeft) > 0)
        return -1;
    if (size < 4)
        return -2;
    At<const uint8_t*>(self, kDecInput) = data;
    const uint32_t header = ((((static_cast<uint32_t>(data[0]) << 8) + data[1]) << 8) + data[2]) << 8 | data[3];
    if (CallThis<int32_t>(addr::cMP3Decoder_Header, self, header) < 0)
        return -2;
    At<int32_t>(self, kDecLimit) = limit;
    At<int32_t>(self, kDecInputLeft) = size;
    return 0;
}

// The game's 0x0094A0E4 (`ret 8`): up to `count` samples a channel into the
// channels' buffers (a null one skipped), decoding the frame when nothing
// is ready (its first one through 0x00A66BEF, which sets the samples to
// drop; a frame that fails is silence). Answers the samples given.
int32_t __fastcall DecoderDecode(char* self, void*, int16_t** channels, int32_t count)
{
    OPENSC4_FIRST("DecoderDecode");
    const int32_t limit = At<int32_t>(self, kDecLimit);
    if (limit >= 0) {
        if (limit == 0)
            return 0;
        if (count > limit)
            count = limit;
    }
    int32_t done = 0;
    if (count <= 0)
        return 0;
    do {
        if (At<int32_t>(self, kDecReady) == 0) {
            if (At<int32_t>(self, kDecInputLeft) == 0)
                return done;
            if (At<uint8_t>(self, kDecFirst)) {
                CallThis<int32_t>(addr::cMP3Decoder_FirstFrame, self, At<const uint8_t*>(self, kDecInput), 0);
                const uint8_t index = At<uint8_t>(self, kDecSkipIndex);
                At<int32_t>(self, kDecSkip) = reinterpret_cast<const int32_t*>(addr::cMP3Decoder_SkipTable)[index];
                At<uint8_t>(self, kDecFirst) = 0;
            } else {
                At<const uint8_t*>(self, kDecFrameStart) = At<const uint8_t*>(self, kDecInput);
                At<int32_t>(self, kDecFrameBits) = 0;
            }
            if (CallThis<int32_t>(addr::cMP3Decoder_Frame, self, self + kDecPcm) < 0)
                CallCdecl<void*>(addr::CRT_memset, self + kDecPcm, 0, At<int32_t>(self, kDecPcmBytes));
            const int32_t used = At<int32_t>(self, kDecFrameSize) + 4;
            At<const uint8_t*>(self, kDecInput) += used;
            At<int32_t>(self, kDecInputLeft) -= used;
            At<int32_t>(self, kDecReady) = At<int32_t>(self, kDecPcmBytes) / (At<int32_t>(self, kDecChannels) * 2);
            At<int32_t>(self, kDecReadAt) = 0;
        }
        const int32_t skip = At<int32_t>(self, kDecSkip);
        if (skip > 0) {
            const int32_t ready = At<int32_t>(self, kDecReady);
            if (skip < ready) {
                At<int32_t>(self, kDecReadAt) += skip;
                At<int32_t>(self, kDecReady) = ready - skip;
                At<int32_t>(self, kDecSkip) = 0;
            } else {
                At<int32_t>(self, kDecSkip) = skip - ready;
                At<int32_t>(self, kDecReady) = 0;
            }
        }
        int32_t take = At<int32_t>(self, kDecReady);
        if (take > 0) {
            if (take > count)
                take = count;
            const int32_t width = At<int32_t>(self, kDecChannels);
            for (int32_t channel = 0; channel < width; ++channel) {
                int16_t* const out = channels[channel];
                if (!out)
                    continue;
                const int16_t* from =
                    reinterpret_cast<const int16_t*>(self + kDecPcm) + (At<int32_t>(self, kDecReadAt) * width + channel);
                for (int32_t i = 0; i < take; ++i) {
                    out[done + i] = *from;
                    from += width;
                }
            }
            At<int32_t>(self, kDecReadAt) += take;
            At<int32_t>(self, kDecReady) -= take;
            if (At<int32_t>(self, kDecLimit) >= 0)
                At<int32_t>(self, kDecLimit) -= take;
            done += take;
        }
    } while (done < count);
    return done;
}

// The game's 0x0094A2AB: no input, nothing ready, the next frame the first.
// EAX the caller's (ours 0).
uint32_t __fastcall DecoderReset(char* self, void*)
{
    OPENSC4_FIRST("DecoderReset");
    At<int32_t>(self, kDecInputLeft) = 0;
    At<int32_t>(self, kDecReady) = 0;
    At<uint8_t>(self, kDecFirst) = 1;
    return 0;
}

// The game's 0x0094A2C1 (cdecl): an MPEG audio frame's header read into six
// dwords (cleared first): the layer, the frame's size, its samples, the
// channels, the sample rate and the bit rate (the tables 0x00AD4D1C and
// 0x00AD4D30). -1 for no header, an MPEG version or layer that is not
// there, a bit rate 15 or a sample rate 3; else 0.
int32_t __cdecl ParseHeader(const uint8_t* header, int32_t* info)
{
    OPENSC4_FIRST("ParseHeader");
    CallCdecl<void*>(addr::CRT_memset, info, 0, 0x18);
    if (header[0] != 0xFF)
        return -1;
    const uint32_t second = header[1];
    if ((second & 0xE0) != 0xE0)
        return -1;
    int32_t version = static_cast<int32_t>((second >> 3) & 3);
    if (version == 1)
        return -1;
    const int32_t layerBits = static_cast<int32_t>((second >> 1) & 3);
    if (layerBits == 0)
        return -1;
    const uint32_t third = header[2];
    const int32_t bitRateIndex = static_cast<int32_t>(third >> 4);
    if (bitRateIndex == 15)
        return -1;
    const int32_t rateIndex = static_cast<int32_t>((third >> 2) & 3);
    if (rateIndex == 3)
        return -1;
    const int32_t padding = static_cast<int32_t>((third >> 1) & 1);
    const int32_t mode = header[3] >> 6;
    if (version > 0)
        --version;
    const int32_t lsf = version < 2 ? 1 : 0;
    const int32_t layer = 4 - layerBits;
    const int32_t* const bitRates = reinterpret_cast<const int32_t*>(addr::cMP3Decoder_BitRates);
    const int32_t* const rates = reinterpret_cast<const int32_t*>(addr::cMP3Decoder_SampleRates);
    const int32_t bitRate = bitRates[(layer + lsf * 3) * 15 + bitRateIndex];
    const int32_t rate = rates[rateIndex + version * 3];
    int32_t size;
    int32_t samples;
    if (layer == 1) {
        size = (bitRate * 12000 / rate + padding) * 4;
        samples = 0x180;
    } else {
        size = bitRate * 144000 / rate;
        if (layer == 3 && lsf)
            size >>= 1;
        size += padding;
        samples = layer == 3 && lsf ? 0x240 : 0x480;
    }
    info[1] = size;
    info[0] = layer;
    info[2] = samples;
    info[4] = rate;
    info[5] = bitRate;
    info[3] = (mode != 3 ? 1 : 0) + 1;
    return 0;
}

// The game's 0x0094A3F8 (cdecl): an ID3v2 tag's size with its 10-byte
// header (the four 7-bit size bytes); 0 when there is none.
uint32_t __cdecl Id3Size(const uint8_t* header)
{
    OPENSC4_FIRST("Id3Size");
    if (header[0] != 'I' || header[1] != 'D' || header[2] != '3')
        return 0;
    return ((((static_cast<uint32_t>(header[6]) << 7) + header[7]) << 7) + header[8]) * 128 + header[9] + 10;
}

// ====================================================================================
// The maker 0x9CEC2B58 (0xC bytes): a reference count at +4 (the game's
// cRZBaseRefCount) and a list at +8.

constexpr uint32_t kCLSID_Stream = 0x3CEC2B47;
constexpr uint32_t kCLSID_Maker = 0x9CEC2B58;

// The game's 0x0094A437.
char* __fastcall MakerConstruct(char* self, void*)
{
    OPENSC4_FIRST("MakerConstruct");
    At<uint32_t>(self, 4) = 0;
    uint8_t allocator;
    At<std::uintptr_t>(self, 0) = addr::cHitMP3Maker_Vtable;
    CallThis<void*>(addr::SC4RefList_Construct, self + 8, &allocator);
    return self;
}

// The game's 0x0094A457 (slot 0, `ret 8`): 1 and 0xFCEFF549 give the maker,
// AddRef through the vtable, AL 1 over its EAX. AL 0 over the caller's EAX
// otherwise (ours 0).
uint32_t __fastcall MakerQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    OPENSC4_FIRST("MakerQueryInterface");
    if (iid != 1 && iid != 0xFCEFF549u)
        return 0;
    *out = self;
    return Answer(AddRef(self), true);
}

// The game's 0x0094A498: the vtable back, a tail jump to the list's
// destructor (its EAX).
uint32_t __fastcall MakerDestruct(char* self, void*)
{
    OPENSC4_FIRST("MakerDestruct");
    At<std::uintptr_t>(self, 0) = addr::cHitMP3Maker_Vtable;
    return CallThis<uint32_t>(addr::SC4PairList_Destroy, self + 8);
}

// The game's 0x0094A47C (slot 4, `ret 4`).
char* __fastcall MakerDeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("MakerDeletingDestructor");
    MakerDestruct(self, nullptr);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x0094A4A6: a maker (0xC bytes); 0 without the memory.
char* __fastcall MakerCreate(void*, void*)
{
    OPENSC4_FIRST("MakerCreate");
    char* const memory = CallCdecl<char*>(addr::SC4_operator_new, 0xCu);
    return memory ? MakerConstruct(memory, nullptr) : nullptr;
}

// ====================================================================================
// The stream 0x3CEC2B47 (0x1104 bytes; vtable 0x00AD4ED8, 24 slots). A
// reference count at +4, the frame header's six dwords at +8, the cRZFile
// at +0x20, the decoder +0xA0, the left and right sample buffers +0xA4 and
// +0xA8 (0x2000 bytes each, from the C library's malloc) and their copies
// at +0xAC for the decoder, the frame read at +0xB4, the ID3 tag's size
// +0x10B8, the samples decoded +0x10BC and given +0x10C0, the next left and
// right sample +0x10C8 and +0x10CC, the open flag +0x10D0, the path (a game
// string) +0x10D4, a flag and whether it was set +0x10E8 and +0x10E9, the
// wave format +0x10EC (tag, channels, rate, bytes a second, block, bits,
// extra) and the length in bytes +0x1100.

constexpr std::size_t kStrRefs = 0x4;
constexpr std::size_t kStrInfo = 0x8;  // layer, size, samples, channels, rate, bit rate
constexpr std::size_t kStrFile = 0x20;
constexpr std::size_t kStrDecoder = 0xA0;
constexpr std::size_t kStrLeft = 0xA4;
constexpr std::size_t kStrRight = 0xA8;
constexpr std::size_t kStrOut = 0xAC;  // the decoder's two channels
constexpr std::size_t kStrFrame = 0xB4;
constexpr std::size_t kStrId3 = 0x10B8;
constexpr std::size_t kStrDecoded = 0x10BC;
constexpr std::size_t kStrGiven = 0x10C0;
constexpr std::size_t kStrNextLeft = 0x10C8;
constexpr std::size_t kStrNextRight = 0x10CC;
constexpr std::size_t kStrOpen = 0x10D0;
constexpr std::size_t kStrPath = 0x10D4;
constexpr std::size_t kStrFlag = 0x10E8;
constexpr std::size_t kStrFlagSet = 0x10E9;
constexpr std::size_t kStrFormat = 0x10EC;
constexpr std::size_t kStrLength = 0x1100;

// The game's 0x0094A4D8. The block size is made from the bits word before
// it is set (what the memory held), as the game does.
char* __fastcall StreamConstruct(char* self, void*)
{
    OPENSC4_FIRST("StreamConstruct");
    At<std::uintptr_t>(self, 0) = addr::cHitMP3Stream_Vtable;
    At<uint32_t>(self, kStrRefs) = 0;
    CallThis<void*>(addr::cRZFile_ConstructEmpty, self + kStrFile);
    At<uint32_t>(self, kStrDecoder) = 0;
    At<uint32_t>(self, kStrLeft) = 0;
    At<uint32_t>(self, kStrRight) = 0;
    At<uint32_t>(self, 0x10B4) = 0;
    At<uint32_t>(self, kStrId3) = 0;
    At<uint32_t>(self, kStrDecoded) = 0;
    At<uint32_t>(self, kStrGiven) = 0;
    At<uint32_t>(self, kStrNextLeft) = 0;
    At<uint32_t>(self, kStrNextRight) = 0;
    At<uint8_t>(self, kStrOpen) = 0;
    CallThis<void*>(addr::SC4String_SC4String, self + kStrPath);
    const int32_t block = static_cast<int32_t>(At<uint16_t>(self, 0x10FA)) * 2 / 8;
    At<uint8_t>(self, kStrFlag) = 0;
    At<uint8_t>(self, kStrFlagSet) = 0;
    At<uint32_t>(self, kStrLength) = 0;
    At<uint32_t>(self, kStrOut) = 0;
    At<uint32_t>(self, kStrOut + 4) = 0;
    At<uint32_t>(self, 0x10FC) = 0;
    At<uint16_t>(self, 0x10EC) = 1;
    At<uint16_t>(self, 0x10EE) = 2;
    At<uint32_t>(self, 0x10F0) = 44100;
    At<uint32_t>(self, 0x10F4) = 176400;
    At<uint16_t>(self, 0x10FA) = 16;
    At<uint16_t>(self, 0x10F8) = static_cast<uint16_t>(block);
    return self;
}

// The game's 0x0094A5C0 (slot 2): the count down; at 1 it goes to 0 and the
// deleting destructor (slot 21, flag 1) runs, answering 0.
uint32_t __fastcall StreamRelease(char* self, void*)
{
    OPENSC4_FIRST("StreamRelease");
    const uint32_t refs = At<uint32_t>(self, kStrRefs);
    if (refs == 1) {
        At<uint32_t>(self, kStrRefs) = 0;
        Virtual<void*>(self, 0x54, 1u);
        return 0;
    }
    At<uint32_t>(self, kStrRefs) = refs - 1;
    return refs - 1;
}

// The game's 0x0094A5DB (slot 9): the file's slot 4 (a tail jump).
uint32_t __fastcall StreamFileSlot4(char* self, void*)
{
    OPENSC4_FIRST("StreamFileSlot4");
    return Virtual<uint32_t>(self + kStrFile, 0x10);
}

// The game's 0x0094A8CA (slot 19): the file's slot 6 (a tail jump).
uint32_t __fastcall StreamFileSlot6(char* self, void*)
{
    OPENSC4_FIRST("StreamFileSlot6");
    return Virtual<uint32_t>(self + kStrFile, 0x18);
}

// The game's 0x0094A5E3 (slot 10): the length +0x1100.
uint32_t __fastcall StreamLength(char* self, void*)
{
    OPENSC4_FIRST("StreamLength");
    return At<uint32_t>(self, kStrLength);
}

// The game's 0x0094A892 (slot 16): the wave format.
char* __fastcall StreamFormat(char* self, void*)
{
    OPENSC4_FIRST("StreamFormat");
    return self + kStrFormat;
}

// The game's 0x0094A899 (slot 17): the flag when it was set, else AL 0; EAX
// the caller's above AL (ours 0).
uint32_t __fastcall StreamFlag(char* self, void*)
{
    OPENSC4_FIRST("StreamFlag");
    return At<uint8_t>(self, kStrFlagSet) ? At<uint8_t>(self, kStrFlag) : 0u;
}

// The game's 0x0094A8AC (slot 18, `ret 4`): the flag set (the argument's
// low byte); AL 1 over the caller's EAX (ours 0).
uint32_t __fastcall StreamSetFlag(char* self, void*, uint32_t flag)
{
    OPENSC4_FIRST("StreamSetFlag");
    At<uint8_t>(self, kStrFlag) = static_cast<uint8_t>(flag);
    At<uint8_t>(self, kStrFlagSet) = 1;
    return 1;
}

// The game's 0x0094A8C2 (slot 22): the flag unset. EAX the caller's (ours 0).
uint32_t __fastcall StreamClearFlag(char* self, void*)
{
    OPENSC4_FIRST("StreamClearFlag");
    At<uint8_t>(self, kStrFlagSet) = 0;
    return 0;
}

// The game's 0x0094A5ED (slot 14, `ret 8`): up to `bytes` of 16-bit PCM
// (whole blocks of the format) into `out`: first what is left of the last
// frame, then frame after frame from the file (its header parsed, the rest
// read, fed and decoded) until enough or the file gives no more. The format
// then takes the last frame's channels and rate. Answers the bytes given.
uint32_t __fastcall StreamRead(char* self, void*, uint8_t* out, int32_t bytes)
{
    OPENSC4_FIRST("StreamRead");
    const int32_t block = static_cast<int32_t>(At<uint16_t>(self, 0x10FA)) * At<uint16_t>(self, 0x10EE) / 8;
    At<uint32_t>(self, kStrGiven) = 0;
    const uint32_t want = static_cast<uint32_t>(bytes / block);
    if (At<uint32_t>(self, kStrDecoded) != 0) {
        while (At<uint32_t>(self, kStrGiven) < want) {
            *reinterpret_cast<int16_t*>(out) = *At<int16_t*>(self, kStrNextLeft);
            At<int16_t*>(self, kStrNextLeft) += 1;
            out += 2;
            if (At<uint16_t>(self, 0x10EE) == 2) {
                *reinterpret_cast<int16_t*>(out) = *At<int16_t*>(self, kStrNextRight);
                out += 2;
                At<int16_t*>(self, kStrNextRight) += 1;
            }
            if (!(++At<uint32_t>(self, kStrGiven) < At<uint32_t>(self, kStrDecoded)))
                break;
        }
    }
    uint32_t total = At<uint32_t>(self, kStrDecoded);
    if (At<uint32_t>(self, kStrGiven) < want) {
        do {
            char* const file = self + kStrFile;
            if (!Yes(Virtual<uint32_t>(file, 0x38, self + kStrFrame, 4u)))
                break;
            ParseHeader(reinterpret_cast<const uint8_t*>(self + kStrFrame), &At<int32_t>(self, kStrInfo));
            if (At<int32_t>(self, kStrInfo) == 0)
                break;
            if (!Yes(Virtual<uint32_t>(file, 0x38, self + kStrFrame + 4, At<int32_t>(self, kStrInfo + 4) - 4)))
                break;
            DecoderFeed(At<char*>(self, kStrDecoder), nullptr, reinterpret_cast<const uint8_t*>(self + kStrFrame),
                        At<int32_t>(self, kStrInfo + 4), -1);
            At<uint32_t>(self, kStrOut) = At<uint32_t>(self, kStrLeft);
            At<uint32_t>(self, kStrOut + 4) = At<uint32_t>(self, kStrRight);
            const int32_t decoded = DecoderDecode(At<char*>(self, kStrDecoder), nullptr,
                                                  &At<int16_t*>(self, kStrOut), At<int32_t>(self, kStrInfo + 8));
            int16_t* const left = At<int16_t*>(self, kStrLeft);
            total += static_cast<uint32_t>(decoded);
            At<int32_t>(self, kStrDecoded) = decoded;
            At<int16_t*>(self, kStrNextLeft) = left;
            At<int16_t*>(self, kStrNextRight) = At<int16_t*>(self, kStrRight);
            if (At<int32_t>(self, kStrInfo + 0xC) == 1)
                At<int16_t*>(self, kStrNextRight) = left;
            if (Bits(left) < Bits(left + decoded)) {
                do {
                    if (!(At<uint32_t>(self, kStrGiven) < want))
                        break;
                    *reinterpret_cast<int16_t*>(out) = *At<int16_t*>(self, kStrNextLeft);
                    At<int16_t*>(self, kStrNextLeft) += 1;
                    out += 2;
                    if (At<int32_t>(self, kStrInfo + 0xC) == 2) {
                        *reinterpret_cast<int16_t*>(out) = *At<int16_t*>(self, kStrNextRight);
                        out += 2;
                        At<int16_t*>(self, kStrNextRight) += 1;
                    }
                    ++At<uint32_t>(self, kStrGiven);
                } while (Bits(At<int16_t*>(self, kStrNextLeft)) <
                         Bits(At<int16_t*>(self, kStrLeft) + At<uint32_t>(self, kStrDecoded)));
            }
        } while (At<uint32_t>(self, kStrGiven) < want);
    }
    const uint16_t channels = At<uint16_t>(self, kStrInfo + 0xC);
    const uint32_t given = At<uint32_t>(self, kStrGiven);
    At<uint16_t>(self, 0x10EE) = channels;
    At<uint32_t>(self, kStrDecoded) = total - given;
    const int32_t newBlock = (static_cast<int32_t>(channels) << 4) / 8;
    At<uint32_t>(self, 0x10FC) = 0;
    const int32_t rate = At<int32_t>(self, kStrInfo + 0x10);
    At<int32_t>(self, 0x10F0) = rate;
    At<uint16_t>(self, 0x10FA) = 16;
    At<uint16_t>(self, 0x10EC) = 1;
    At<uint16_t>(self, 0x10F8) = static_cast<uint16_t>(newBlock);
    At<int32_t>(self, 0x10F4) = newBlock * rate;
    return static_cast<uint32_t>(newBlock) * given;
}

// The game's 0x0094A81A (slot 15): back to the file's start (its slot 9),
// nothing decoded; the first 10 bytes read, and an ID3 tag's other bytes
// read one by one (else back to the start again). AL 0 over the read's EAX
// when the 10 bytes are not there; else AL 1 over the tag's size less 10
// (over the second rewind's EAX without a tag).
uint32_t __fastcall StreamRewind(char* self, void*)
{
    OPENSC4_FIRST("StreamRewind");
    At<uint32_t>(self, kStrGiven) = 0;
    At<uint32_t>(self, kStrDecoded) = 0;
    char* const file = self + kStrFile;
    Virtual<uint32_t>(file, 0x24);
    const uint32_t read = Virtual<uint32_t>(file, 0x38, self + kStrFrame, 10u);
    if (!Yes(read))
        return read;
    const uint32_t size = Id3Size(reinterpret_cast<const uint8_t*>(self + kStrFrame));
    At<uint32_t>(self, kStrId3) = size;
    if (size == 0)
        return Answer(Virtual<uint32_t>(file, 0x24), true);
    int32_t left = static_cast<int32_t>(size) - 10;
    for (int32_t i = 0; i < left; ++i) {
        uint32_t byte = 0;
        Virtual<uint32_t>(file, 0x38, &byte, 1u);
        left = static_cast<int32_t>(At<uint32_t>(self, kStrId3)) - 10;
    }
    return Answer(static_cast<uint32_t>(left), true);
}

// The game's 0x0094A8D2 (slot 20, `ret 4`): position 0 rewinds (slot 15)
// and answers AL 0 over its EAX. Else with the file open (its slot 4) its
// slot 12 with the position: EAX 1 when it answers the position, else 0;
// AL 0 over slot 4's EAX when it is not open.
uint32_t __fastcall StreamSeek(char* self, void*, uint32_t position)
{
    OPENSC4_FIRST("StreamSeek");
    if (position == 0)
        return Answer(Virtual<uint32_t>(self, 0x3C), false);
    char* const file = self + kStrFile;
    const uint32_t open = Virtual<uint32_t>(file, 0x10);
    if (!Yes(open))
        return Answer(open, false);
    return Virtual<uint32_t>(file, 0x30, position) == position ? 1u : 0u;
}

// The game's 0x0094A907 (slot 8): when open, the file closed (slot 5), the
// sample buffers and the decoder (its destructor through 0x0094A972) freed,
// the flag cleared. AL 1 over the decoder's free's EAX, else the right
// buffer's free's, else 0 (each buffer is loaded into EAX before it is
// tested); the caller's EAX when not open (ours 0).
uint32_t __fastcall StreamClose(char* self, void*)
{
    OPENSC4_FIRST("StreamClose");
    uint32_t eax = 0;
    if (!At<uint8_t>(self, kStrOpen))
        return Answer(eax, true);
    eax = Virtual<uint32_t>(self + kStrFile, 0x14);
    void* const left = At<void*>(self, kStrLeft);
    eax = Bits(left);
    if (left) {
        eax = CallCdecl<uint32_t>(addr::CRT_free, left);
        At<void*>(self, kStrLeft) = nullptr;
    }
    void* const right = At<void*>(self, kStrRight);
    eax = Bits(right);
    if (right) {
        eax = CallCdecl<uint32_t>(addr::CRT_free, right);
        At<void*>(self, kStrRight) = nullptr;
    }
    if (void* const decoder = At<void*>(self, kStrDecoder)) {
        CallThis<uint32_t>(addr::cMP3Decoder_DestructThunk, decoder);
        eax = CallCdecl<uint32_t>(addr::CRT_free, decoder);
        At<void*>(self, kStrDecoder) = nullptr;
    }
    At<uint8_t>(self, kStrOpen) = 0;
    return Answer(eax, true);
}

// The length in bytes: the file's size in bits over the bit rate (in units
// of 1024, made a float), times the bytes a second, truncated by the
// game's _ftol, on the x87 as the game has it (the precision is the FPU's).
uint32_t StreamLengthOf(int32_t bitRate, int32_t bits, int32_t bytesASecond)
{
    int32_t scaled = bitRate << 10;
    float base;
    uint32_t result;
    const std::uintptr_t twoTo32 = addr::SC4_FloatTwoTo32;
    const std::uintptr_t ftol = addr::CRT_ftol;
    __asm {
        fild dword ptr [scaled]
        fstp dword ptr [base]
        fild dword ptr [bits]
        mov eax, bits
        test eax, eax
        jge size_ok
        mov ecx, twoTo32
        fadd dword ptr [ecx]
    size_ok:
        fdiv dword ptr [base]
        mov eax, bytesASecond
        test eax, eax
        fild dword ptr [bytesASecond]
        jge rate_ok
        mov ecx, twoTo32
        fadd dword ptr [ecx]
    rate_ok:
        fmulp st(1), st
        mov edx, ftol
        call edx
        mov result, eax
    }
    return result;
}

// The game's 0x0094AC88 (slot 7): the path's extension (lower case) must be
// the game's 0x00B63528 (else the flag set to 0, AL 0). When not yet open:
// marked open, a fresh decoder (0x4A60 bytes from malloc, constructed and
// reset twice), two 0x2000-byte sample buffers, the file given the path
// (slot 28) and opened (slot 3 with 1, 2, 1; AL 0 when it fails, the stream
// staying marked open). Then rewound (slot 15); unless the flag was set, it
// becomes whether a first read of 4 bytes (slot 14) gives any (slot 18) and
// it is rewound again; the length worked out. AL 1 over the file spec's
// destructor's EAX (also when already open).
uint32_t __fastcall StreamOpen(char* self, void*)
{
    OPENSC4_FIRST("StreamOpen");
    alignas(4) char spec[0x70];
    CallThis<void*>(addr::cRZFileSpec_Construct, spec, self + kStrPath);
    char* const name = CallThis<char*>(addr::cRZFileSpec_F91C31F, spec);
    LocalText text;
    uint8_t tag;
    CallThis<void>(addr::SC4String_CopyBuffer, text.bytes + 4, name ? name + 4 : nullptr, 0u, 0xFFFFFFFFu, &tag);
    At<std::uintptr_t>(text.bytes, 0) = addr::SC4String_Vtable;
    At<uint32_t>(text.bytes, 0x10) = 0;
    CallThis<void>(addr::cRZBaseString_MakeLower, text.get());
    bool opened = false;
    uint32_t ignored = 0;
    if (!Yes(CallCdecl<uint32_t>(addr::SC4UI_F43AB70, text.bytes + 4, addr::cHitMP3Stream_Extension))) {
        Virtual<uint32_t>(self, 0x48, 0u);
        text.Free(ignored);
    } else if (At<uint8_t>(self, kStrOpen)) {
        text.Free(ignored);
        opened = true;
    } else {
        At<uint8_t>(self, kStrOpen) = 1;
        if (char* const old = At<char*>(self, kStrDecoder)) {
            CallThis<uint32_t>(addr::cMP3Decoder_Destruct, old);
            CallCdecl<void>(addr::CRT_free, old);
            At<char*>(self, kStrDecoder) = nullptr;
        }
        char* decoder = CallCdecl<char*>(addr::CRT_malloc, 0x4A60u);
        if (decoder) {
            CallThis<void*>(addr::cMP3Decoder_Construct, decoder);
            DecoderReset(decoder, nullptr);
        }
        At<char*>(self, kStrDecoder) = decoder;
        DecoderReset(decoder, nullptr);
        At<uint32_t>(self, kStrGiven) = 0;
        At<uint32_t>(self, kStrDecoded) = 0;
        At<void*>(self, kStrLeft) = CallCdecl<void*>(addr::CRT_malloc, 0x2000u);
        At<void*>(self, kStrRight) = CallCdecl<void*>(addr::CRT_malloc, 0x2000u);
        char* const file = self + kStrFile;
        Virtual<uint32_t>(file, 0x70, self + kStrPath);
        if (!Yes(Virtual<uint32_t>(file, 0xC, 1u, 2u, 1u))) {
            text.Free(ignored);
        } else {
            Virtual<uint32_t>(self, 0x3C);
            if (!At<uint8_t>(self, kStrFlagSet)) {
                uint32_t first = 0;
                const uint32_t got = Virtual<uint32_t>(self, 0x38, &first, 4u);
                Virtual<uint32_t>(self, 0x48, Answer(got, got != 0));
                Virtual<uint32_t>(self, 0x3C);
            }
            const int32_t size = Virtual<int32_t>(file, 0x1C);
            At<uint32_t>(self, kStrLength) = StreamLengthOf(At<int32_t>(self, kStrInfo + 0x14), size << 3,
                                                            At<int32_t>(self, 0x10F4));
            text.Free(ignored);
            opened = true;
        }
    }
    return Answer(CallThis<uint32_t>(addr::SC4UI_F43AF80, spec), opened);
}

// The game's 0x0094AEAE (slot 3, `ret 4`): the flag unset (slot 22), the
// path set from the text, then opened (slot 7; its EAX).
uint32_t __fastcall StreamOpenPath(char* self, void*, const char* path)
{
    OPENSC4_FIRST("StreamOpenPath");
    Virtual<uint32_t>(self, 0x58);
    LocalText text;
    char* const made = CallThis<char*>(addr::SC4String_FromText, text.get(), path);
    CallThis<void>(addr::cSC4Advice_F408C40, self + kStrPath + 4, made ? made + 4 : nullptr);
    uint32_t ignored = 0;
    text.Free(ignored);
    return Virtual<uint32_t>(self, 0x1C);
}

// The game's 0x0094A977 (slot 5, `ret 4`): the path of a resource key: the
// resource manager's slot 32 finds the key's record, its interface
// 0x856C8CF7 gives the path (slot 18), opened by slot 3. AL its answer, 0
// when a step fails; over the reference's destructor's EAX.
uint32_t __fastcall StreamOpenKey(char* self, void*, const uint32_t* key)
{
    OPENSC4_FIRST("StreamOpenKey");
    void* manager = nullptr;
    CallThis<void*>(addr::SC4RefPtr_ResourceManager, &manager);
    bool opened = false;
    void* record = nullptr;
    if (Yes(Virtual<uint32_t>(manager, 0x80, key, &record))) {
        void* source = nullptr;
        if (Yes(Virtual<uint32_t>(record, 0x0, 0x856C8CF7u, &source))) {
            LocalText path;
            CallThis<void*>(addr::SC4String_SC4String, path.get());
            uint32_t ignored = 0;
            if (Yes(Virtual<uint32_t>(source, 0x48, key, path.get()))) {
                const char* const text = CallThis<const char*>(addr::cRZBaseString_Data, path.get());
                opened = Yes(Virtual<uint32_t>(self, 0xC, text));
            }
            path.Free(ignored);
        }
        if (source)
            Release(source);
    }
    if (record)
        Release(record);
    return Answer(CallThis<uint32_t>(addr::SC4RefPtr_Destruct, &manager), opened);
}

// The game's 0x0094AAB8: the vtable back, closed (0x0094A907), the path's
// and the file's destructors (its EAX).
uint32_t __fastcall StreamDestruct(char* self, void*)
{
    OPENSC4_FIRST("StreamDestruct");
    At<std::uintptr_t>(self, 0) = addr::cHitMP3Stream_Vtable;
    StreamClose(self, nullptr);
    CallThis<void>(addr::SC4String_Destroy, self + kStrPath);
    return CallThis<uint32_t>(addr::cRZFile_Destruct, self + kStrFile);
}

// The game's 0x0094AA9C (slot 21, `ret 4`).
char* __fastcall StreamDeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("StreamDeletingDestructor");
    StreamDestruct(self, nullptr);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x0094AB94: a stream (0x1104 bytes); 0 without the memory.
char* __fastcall StreamCreate(void*, void*)
{
    OPENSC4_FIRST("StreamCreate");
    char* const memory = CallCdecl<char*>(addr::SC4_operator_new, 0x1104u);
    return memory ? StreamConstruct(memory, nullptr) : nullptr;
}

// The game's 0x0094AB03 (the maker's slot 3, `ret 0xC`; `this` unused): a
// stream made for the path (an IGZString), opened by its slot 3 and asked
// for the interface (slot 0). AL its answer, over the string's free's EAX
// (the QueryInterface's without a buffer). The stream is not released, and
// without the memory the game calls through a null one.
uint32_t __fastcall MakerMakeStream(void*, void*, void* path, uint32_t iid, void** out)
{
    OPENSC4_FIRST("MakerMakeStream");
    LocalText text;
    CallThis<void*>(addr::SC4String_SC4String, text.get());
    CallThis<void*>(addr::SC4String_AssignFromIGZString, text.get(), path);
    char* const memory = CallCdecl<char*>(addr::SC4_operator_new, 0x1104u);
    char* const stream = memory ? StreamConstruct(memory, nullptr) : nullptr;
    Virtual<uint32_t>(stream, 0xC, At<const char*>(text.bytes, 4));
    uint32_t eax = Virtual<uint32_t>(stream, 0x0, iid, out);
    const bool found = Yes(eax);
    text.Free(eax);
    return Answer(eax, found);
}

// ====================================================================================
// The director 0x1CEC20BE (the static one at 0x00B634D0).

// The game's 0x0094ABC9: the two classes registered by their factories (the
// game's addresses, so a hooked factory stays ours).
void* __fastcall DirectorConstruct(void* self, void*)
{
    OPENSC4_FIRST("DirectorConstruct");
    CallThis<void*>(addr::cRZCOMDllDirector_Construct, self);
    At<std::uintptr_t>(self, 0) = addr::cHitMP3Director_Vtable;
    At<std::uintptr_t>(self, 4) = addr::cHitMP3Director_UnknownVtable;
    CallThis<void>(addr::cRZCOMDllDirector_AddCls, self, kCLSID_Stream, addr::cHitMP3Stream_Create);
    CallThis<void>(addr::cRZCOMDllDirector_AddCls, self, kCLSID_Maker, addr::cHitMP3Maker_Create);
    return self;
}

// The game's 0x0094AC27 (slot 16, `ret 4`): cRZCOMDllDirector's destructor
// through the thunk 0x0094A432 (the vtables are not put back).
void* __fastcall DirectorDeletingDestructor(void* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("DirectorDeletingDestructor");
    CallThis<void>(addr::cHitMP3Director_DestructThunk, self);
    if (flags & 1)
        CallCdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x0094AC43: the static director, made on first use (its
// destructor 0x00A7F63C given to atexit).
void* __fastcall DirectorGet(void*, void*)
{
    OPENSC4_FIRST("DirectorGet");
    uint32_t& guard = *reinterpret_cast<uint32_t*>(addr::cHitMP3Director_Guard);
    void* const director = reinterpret_cast<void*>(addr::cHitMP3Director_Static);
    if (!(guard & 1)) {
        guard |= 1;
        DirectorConstruct(director, nullptr);
        CallCdecl<int>(addr::SC4_atexit, addr::cHitMP3Director_StaticDestroy);
    }
    return director;
}

// ====================================================================================
// The UI script's root holder 0x5A354595 (0x24 bytes; its constructor
// 0x0094C0AF, slot 4 0x0094C0EC and deleting destructor 0x0094C188 are part
// u11's). The primary vtable at +0, the interface 0xFA35615F at +4, the
// windows in an STLport vector at +8 (begin, end, capacity; its allocator
// +0x18), the reference count +0x20.

constexpr std::size_t kRootBegin = 0x8;
constexpr std::size_t kRootEnd = 0xC;
constexpr uint32_t kIID_Window = 0x9A4527CB;
constexpr uint32_t kIID_Child = 0x5A356159;

inline uint32_t RootCount(char* self) { return (At<int32_t>(self, kRootEnd) - At<int32_t>(self, kRootBegin)) >> 2; }
inline void* RootAt(char* self, uint32_t index) { return At<void**>(self, kRootBegin)[index]; }

// The game's 0x0094BBFE (slot 0, `ret 8`; also the tail of 0x0094C0DF): 1
// and 0x1A35608A give the holder, 0xFA35615F its +4 (0 for none); AddRef
// through the primary vtable, AL 1 over its EAX. AL 0 over the caller's EAX
// otherwise (ours 0).
uint32_t __fastcall RootQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    OPENSC4_FIRST("RootQueryInterface");
    if (iid == 1 || iid == 0x1A35608Au)
        *out = self;
    else if (iid == 0xFA35615Fu)
        *out = self ? self + 4 : nullptr;
    else
        return 0;
    return Answer(AddRef(self), true);
}

// The game's 0x0094BC46 (slot 2; also the tail of 0x0094C0E7): above 1 the
// count goes down and is answered, else the deleting destructor (slot 12,
// flag 1) and 0.
uint32_t __fastcall RootRelease(char* self, void*)
{
    OPENSC4_FIRST("RootRelease");
    const uint32_t refs = At<uint32_t>(self, 0x20);
    if (refs > 1) {
        At<uint32_t>(self, 0x20) = refs - 1;
        return refs - 1;
    }
    Virtual<void*>(self, 0x30, 1u);
    return 0;
}

// The game's 0x0094BD5E (slot 5, `ret 4`): the first window whose slot 5
// answers the id; 0 for none.
void* __fastcall RootById(char* self, void*, uint32_t id)
{
    OPENSC4_FIRST("RootById");
    const uint32_t count = RootCount(self);
    for (uint32_t i = 0; i < count; ++i) {
        void* const window = RootAt(self, i);
        if (Virtual<uint32_t>(window, 0x14) == id)
            return window;
    }
    return nullptr;
}

// The game's 0x0094BD96 (slot 6, `ret 4`): the window at the index; 0 past
// the end.
void* __fastcall RootAtIndex(char* self, void*, uint32_t index)
{
    OPENSC4_FIRST("RootAtIndex");
    return index < RootCount(self) ? RootAt(self, index) : nullptr;
}

// Slots 7 and 8: a window's interface 0x9A4527CB's slot 3, the interface
// released; 0 without.
uint32_t WindowSlot3(void* window)
{
    if (!window)
        return 0;
    void* face = nullptr;
    if (!Yes(Virtual<uint32_t>(window, 0x0, kIID_Window, &face)))
        return 0;
    const uint32_t result = Virtual<uint32_t>(face, 0xC);
    Release(face);
    return result;
}

// The game's 0x0094BC69 (slot 7, `ret 4`): by id (slot 5).
uint32_t __fastcall RootWindowById(char* self, void*, uint32_t id)
{
    OPENSC4_FIRST("RootWindowById");
    return WindowSlot3(Virtual<void*>(self, 0x14, id));
}

// The game's 0x0094BCAA (slot 8, `ret 4`): by index (slot 6).
uint32_t __fastcall RootWindowAt(char* self, void*, uint32_t index)
{
    OPENSC4_FIRST("RootWindowAt");
    return WindowSlot3(Virtual<void*>(self, 0x18, index));
}

// The game's 0x0094BDB4 (slot 9, `ret 4`): each window's slot 7 with the
// argument (the count taken once). EAX the last call's (the caller's with
// none; ours 0).
uint32_t __fastcall RootEach(char* self, void*, uint32_t argument)
{
    OPENSC4_FIRST("RootEach");
    const uint32_t count = RootCount(self);
    uint32_t eax = 0;
    for (uint32_t i = 0; i < count; ++i)
        eax = Virtual<uint32_t>(RootAt(self, i), 0x1C, argument);
    return eax;
}

// The game's 0x0094BDE0 (slot 10): the count.
uint32_t __fastcall RootCountSlot(char* self, void*)
{
    OPENSC4_FIRST("RootCountSlot");
    return RootCount(self);
}

// The game's 0x0094BDEA (slot 11, `ret 8`): every window that gives the
// interface (slot 0) gets slot 8 with the holder, its index, the interface
// and the argument while all before answered yes, and is released (the
// window, not what slot 0 gave). AL whether all did, over the count.
uint32_t __fastcall RootAll(char* self, void*, uint32_t iid, uint32_t argument)
{
    OPENSC4_FIRST("RootAll");
    bool all = true;
    uint32_t count = RootCount(self);
    if (count == 0)
        return Answer(count, all);
    uint32_t i = 0;
    do {
        void* const window = RootAt(self, i);
        void* face = nullptr;
        if (window && Yes(Virtual<uint32_t>(window, 0x0, iid, &face))) {
            all = all && Yes(Virtual<uint32_t>(window, 0x20, self, i, iid, argument));
            Release(window);
        }
        count = RootCount(self);
        ++i;
    } while (i < count);
    return Answer(count, all);
}

// The game's 0x0094BD38 (`ret 4`; a rectangle's `this`, also called by
// 0x009B4E7E): EAX 1 when the point is inside (edges included).
uint32_t __fastcall RectContains(const int32_t* rect, void*, const int32_t* point)
{
    OPENSC4_FIRST("RectContains");
    return point[0] >= rect[0] && point[1] >= rect[1] && point[0] <= rect[2] && point[1] <= rect[3] ? 1u : 0u;
}

// The game's 0x0094BE64 (the interface at +4's slot 3, `ret 4`): every
// window's interface 0x5A356159 gets its slot 3 with the argument (the
// count taken once). AL 1 over the last call's EAX (the caller's with no
// windows; ours 0).
uint32_t __fastcall RootPartEach(char* part, void*, uint32_t argument)
{
    OPENSC4_FIRST("RootPartEach");
    const uint32_t count = (At<int32_t>(part, 8) - At<int32_t>(part, 4)) >> 2;
    uint32_t eax = 0;
    for (uint32_t i = 0; i < count; ++i) {
        void** const begin = At<void**>(part, 4);
        eax = Bits(begin);
        void* const window = begin[i];
        if (!window)
            continue;
        void* face = nullptr;
        eax = Virtual<uint32_t>(window, 0x0, kIID_Child, &face);
        if (!Yes(eax))
            continue;
        Virtual<uint32_t>(face, 0xC, argument);
        eax = Release(face);
    }
    return Answer(eax, true);
}

// The game's 0x0094BEBA (the interface at +4's slot 4, `ret 8`): from the
// last window to the first, until one answers: its interface 0x5A356159's
// rectangle (slot 5) holds the point, and its slot 6 with the point and
// the argument says yes. AL 0 over the last call's EAX (the caller's with
// no windows; ours 0).
uint32_t __fastcall RootPartAt(char* part, void*, const int32_t* point, uint32_t argument)
{
    OPENSC4_FIRST("RootPartAt");
    int32_t i = (At<int32_t>(part, 8) - At<int32_t>(part, 4)) >> 2;
    bool found = false;
    uint32_t eax = 0;
    while (--i >= 0) {
        if (found)
            break;
        void** const begin = At<void**>(part, 4);
        eax = Bits(begin);
        void* const window = begin[i];
        if (!window)
            continue;
        void* face = nullptr;
        eax = Virtual<uint32_t>(window, 0x0, kIID_Child, &face);
        if (!Yes(eax))
            continue;
        int32_t rect[4];
        eax = Virtual<uint32_t>(face, 0x14, rect);
        if (Yes(eax)) {
            eax = RectContains(rect, nullptr, point);
            if (Yes(eax)) {
                eax = Virtual<uint32_t>(face, 0x18, point, argument);
                found = Yes(eax);
            }
        }
        eax = Release(face);
    }
    return Answer(eax, false);
}

// The game's 0x0094BCEB (`ret 0xC`; the vector's `this` unused): pointers
// copied into place (a null destination skipped); the end answered.
void** __fastcall VectorCopy(void*, void*, void* const* first, void* const* last, void** to)
{
    OPENSC4_FIRST("VectorCopy");
    for (; first != last; ++first, ++to) {
        if (to)
            *to = *first;
    }
    return to;
}

// The game's 0x0094BD13 (`ret 0xC`): pointers copied backwards to end at
// `to`; the first answered.
void** __fastcall VectorCopyBackward(void*, void*, void* const* first, void* const* last, void** to)
{
    OPENSC4_FIRST("VectorCopyBackward");
    while (last != first)
        *--to = *--last;
    return to;
}

// The game's 0x0094BF3B (`ret 8`; called by slot 4 0x0094C0EC): a pointer
// put in at `at`. With room, the last moved up one and the rest copied
// backwards (the copy's EAX); else a new block (the allocator's slot 3,
// twice the count or 1) with the pointers copied around the new one, the
// old block freed (slot 4) and the block's end answered.
std::uintptr_t __fastcall VectorInsert(char* self, void*, void** at, void* const* value)
{
    OPENSC4_FIRST("VectorInsert");
    void** const end = At<void**>(self, 4);
    if (end != At<void**>(self, 8)) {
        if (end)
            *end = end[-1];
        At<void**>(self, 4) = end + 1;
        void* const moved = *value;
        void** const result = VectorCopyBackward(self, nullptr, at, end - 1, end);
        *at = moved;
        return reinterpret_cast<std::uintptr_t>(result);
    }
    const uint32_t count = static_cast<uint32_t>((At<int32_t>(self, 4) - At<int32_t>(self, 0)) >> 2);
    const uint32_t capacity = count ? count * 2 : 1;
    void* const allocator = At<void*>(self, 0x10);
    void** const block = Virtual<void**>(allocator, 0xC, capacity << 2);
    void** next = VectorCopy(self, nullptr, At<void**>(self, 0), at, block);
    if (next)
        *next = *value;
    ++next;
    next = VectorCopy(self, nullptr, at, At<void**>(self, 4), next);
    Virtual<uint32_t>(At<void*>(self, 0x10), 0x10, At<void**>(self, 0));
    At<void**>(self, 0) = block;
    At<void**>(self, 4) = next;
    At<void**>(self, 8) = block + capacity;
    return reinterpret_cast<std::uintptr_t>(block + capacity);
}

// ====================================================================================
// Install

OPENSC4_THUNK(DecoderFeedThunk, DecoderFeed, 12)
OPENSC4_THUNK(DecoderDecodeThunk, DecoderDecode, 8)
OPENSC4_THUNK(DecoderResetThunk, DecoderReset, 0)
OPENSC4_CDECL_THUNK(ParseHeaderThunk, ParseHeader, 8)
OPENSC4_CDECL_THUNK(Id3SizeThunk, Id3Size, 4)
OPENSC4_THUNK(MakerConstructThunk, MakerConstruct, 0)
OPENSC4_THUNK(MakerQueryInterfaceThunk, MakerQueryInterface, 8)
OPENSC4_THUNK(MakerDestructThunk, MakerDestruct, 0)
OPENSC4_THUNK(MakerDeletingDestructorThunk, MakerDeletingDestructor, 4)
OPENSC4_THUNK(MakerCreateThunk, MakerCreate, 0)
OPENSC4_THUNK(MakerMakeStreamThunk, MakerMakeStream, 12)
OPENSC4_THUNK(StreamConstructThunk, StreamConstruct, 0)
OPENSC4_THUNK(StreamReleaseThunk, StreamRelease, 0)
OPENSC4_THUNK(StreamFileSlot4Thunk, StreamFileSlot4, 0)
OPENSC4_THUNK(StreamFileSlot6Thunk, StreamFileSlot6, 0)
OPENSC4_THUNK(StreamLengthThunk, StreamLength, 0)
OPENSC4_THUNK(StreamFormatThunk, StreamFormat, 0)
OPENSC4_THUNK(StreamFlagThunk, StreamFlag, 0)
OPENSC4_THUNK(StreamSetFlagThunk, StreamSetFlag, 4)
OPENSC4_THUNK(StreamClearFlagThunk, StreamClearFlag, 0)
OPENSC4_THUNK(StreamReadThunk, StreamRead, 8)
OPENSC4_THUNK(StreamRewindThunk, StreamRewind, 0)
OPENSC4_THUNK(StreamSeekThunk, StreamSeek, 4)
OPENSC4_THUNK(StreamCloseThunk, StreamClose, 0)
OPENSC4_THUNK(StreamOpenThunk, StreamOpen, 0)
OPENSC4_THUNK(StreamOpenPathThunk, StreamOpenPath, 4)
OPENSC4_THUNK(StreamOpenKeyThunk, StreamOpenKey, 4)
OPENSC4_THUNK(StreamDestructThunk, StreamDestruct, 0)
OPENSC4_THUNK(StreamDeletingDestructorThunk, StreamDeletingDestructor, 4)
OPENSC4_THUNK(StreamCreateThunk, StreamCreate, 0)
OPENSC4_THUNK(DirectorConstructThunk, DirectorConstruct, 0)
OPENSC4_THUNK(DirectorDeletingDestructorThunk, DirectorDeletingDestructor, 4)
OPENSC4_THUNK(DirectorGetThunk, DirectorGet, 0)
OPENSC4_THUNK(RootQueryInterfaceThunk, RootQueryInterface, 8)
OPENSC4_THUNK(RootReleaseThunk, RootRelease, 0)
OPENSC4_THUNK(RootByIdThunk, RootById, 4)
OPENSC4_THUNK(RootAtIndexThunk, RootAtIndex, 4)
OPENSC4_THUNK(RootWindowByIdThunk, RootWindowById, 4)
OPENSC4_THUNK(RootWindowAtThunk, RootWindowAt, 4)
OPENSC4_THUNK(RootEachThunk, RootEach, 4)
OPENSC4_THUNK(RootCountThunk, RootCountSlot, 0)
OPENSC4_THUNK(RootAllThunk, RootAll, 8)
OPENSC4_THUNK(RectContainsThunk, RectContains, 4)
OPENSC4_THUNK(RootPartEachThunk, RootPartEach, 4)
OPENSC4_THUNK(RootPartAtThunk, RootPartAt, 8)
OPENSC4_THUNK(VectorCopyThunk, VectorCopy, 12)
OPENSC4_THUNK(VectorCopyBackwardThunk, VectorCopyBackward, 12)
OPENSC4_THUNK(VectorInsertThunk, VectorInsert, 8)

struct Detour {
    std::uintptr_t address;
    void (*ours)();
};

template <std::size_t N>
bool InstallAll(const Detour (&detours)[N])
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        void* original = nullptr;
        if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(detour.ours), &original))
            return false;
    }
    return true;
}

bool InstallObject(registry::Mode)
{
    const Detour detours[] = {
        {addr::cHitMP3Director_Construct, &DirectorConstructThunk},
        {addr::cHitMP3Director_DeletingDestructor, &DirectorDeletingDestructorThunk},
        {addr::GZDirector_Get94AC43, &DirectorGetThunk},
        {addr::cHitMP3Maker_Construct, &MakerConstructThunk},
        {addr::cHitMP3Maker_QueryInterface, &MakerQueryInterfaceThunk},
        {addr::cHitMP3Maker_Destruct, &MakerDestructThunk},
        {addr::cHitMP3Maker_DeletingDestructor, &MakerDeletingDestructorThunk},
        {addr::cHitMP3Maker_Create, &MakerCreateThunk},
        {addr::cHitMP3Maker_MakeStream, &MakerMakeStreamThunk},
    };
    return InstallAll(detours);
}

bool InstallStream(registry::Mode)
{
    const Detour detours[] = {
        {addr::cHitMP3Stream_Construct, &StreamConstructThunk},
        {addr::cHitMP3Stream_Release, &StreamReleaseThunk},
        {addr::cHitMP3Stream_FileSlot4, &StreamFileSlot4Thunk},
        {addr::cHitMP3Stream_FileSlot6, &StreamFileSlot6Thunk},
        {addr::cHitMP3Stream_Length, &StreamLengthThunk},
        {addr::cHitMP3Stream_Format, &StreamFormatThunk},
        {addr::cHitMP3Stream_Flag, &StreamFlagThunk},
        {addr::cHitMP3Stream_SetFlag, &StreamSetFlagThunk},
        {addr::cHitMP3Stream_ClearFlag, &StreamClearFlagThunk},
        {addr::cHitMP3Stream_Read, &StreamReadThunk},
        {addr::cHitMP3Stream_Rewind, &StreamRewindThunk},
        {addr::cHitMP3Stream_Seek, &StreamSeekThunk},
        {addr::cHitMP3Stream_Close, &StreamCloseThunk},
        {addr::cHitMP3Stream_Open, &StreamOpenThunk},
        {addr::cHitMP3Stream_OpenPath, &StreamOpenPathThunk},
        {addr::cHitMP3Stream_OpenKey, &StreamOpenKeyThunk},
        {addr::cHitMP3Stream_Destruct, &StreamDestructThunk},
        {addr::cHitMP3Stream_DeletingDestructor, &StreamDeletingDestructorThunk},
        {addr::cHitMP3Stream_Create, &StreamCreateThunk},
    };
    return InstallAll(detours);
}

bool InstallDecode(registry::Mode)
{
    const Detour detours[] = {
        {addr::cMP3Decoder_Feed, &DecoderFeedThunk},
        {addr::cMP3Decoder_Decode, &DecoderDecodeThunk},
        {addr::cMP3Decoder_Reset, &DecoderResetThunk},
        {addr::cMP3Decoder_ParseHeader, &ParseHeaderThunk},
        {addr::cMP3Decoder_Id3Size, &Id3SizeThunk},
    };
    return InstallAll(detours);
}

bool InstallRoot(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIScriptRoot_QueryInterface, &RootQueryInterfaceThunk},
        {addr::cGZUIScriptRoot_Release, &RootReleaseThunk},
        {addr::cGZUIScriptRoot_ById, &RootByIdThunk},
        {addr::cGZUIScriptRoot_At, &RootAtIndexThunk},
        {addr::cGZUIScriptRoot_WindowById, &RootWindowByIdThunk},
        {addr::cGZUIScriptRoot_WindowAt, &RootWindowAtThunk},
        {addr::cGZUIScriptRoot_Each, &RootEachThunk},
        {addr::cGZUIScriptRoot_Count, &RootCountThunk},
        {addr::cGZUIScriptRoot_All, &RootAllThunk},
        {addr::cGZUIScriptRoot_PartEach, &RootPartEachThunk},
        {addr::cGZUIScriptRoot_PartAt, &RootPartAtThunk},
        {addr::cGZUIScriptRoot_VectorCopy, &VectorCopyThunk},
        {addr::cGZUIScriptRoot_VectorCopyBackward, &VectorCopyBackwardThunk},
        {addr::cGZUIScriptRoot_VectorInsert, &VectorInsertThunk},
        {addr::GZRect_Contains, &RectContainsThunk},
    };
    return InstallAll(detours);
}

const bool g_registered[] = {
    registry::Register({"framework.mp3.object", "framework/mp3", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cHitMP3Director_Construct,
                        "Our MP3 decoder director 0x1CEC20BE and its maker 0x9CEC2B58: the director's constructor, "
                        "deleting destructor and getter; the maker's constructor, QueryInterface, destructors, "
                        "factory and stream making (9 detours; part u10, docs/gzui.md)",
                        false, InstallObject}),
    registry::Register({"framework.mp3.stream", "framework/mp3", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cHitMP3Stream_Read,
                        "Our MP3 stream 0x3CEC2B47: constructor, destructors, factory, Release, the file's slots, "
                        "getters and setters, Read, Rewind, Seek, Close and opening by file, path and resource key "
                        "(19 detours; part u10, docs/gzui.md)",
                        false, InstallStream}),
    registry::Register({"framework.mp3.decode", "framework/mp3", registry::Kind::Detour, registry::Phase::Attach,
                        addr::cMP3Decoder_Decode,
                        "Our MP3 decoder's feed, decode and reset, the frame header and the ID3 tag's size (5 "
                        "detours; part u10, docs/gzui.md)",
                        false, InstallDecode}),
    registry::Register({"framework.uiscript.root", "framework/uiscript", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIScriptRoot_ById,
                        "Our UI script root holder 0x5A354595: QueryInterface, Release, windows by id and index, "
                        "the calls to each, the count, the rectangle test, the vector's growth and copies (15 "
                        "detours; part u10, docs/gzui.md)",
                        false, InstallRoot}),
};

} // namespace
} // namespace opensc4::gzui
