// The index batch an effect draws its quads with, written by hand from the
// game's code: its clear (0x005EC4A0), its add of quads' indices
// (0x005EC850), its add of quads' vertices (0x005EC8C0) and its draw
// (0x005EC5D0). Included by water_effects.cpp before the particle pool; the
// draws that call them are in effects_particles_draw.h and effects_third.h;
// the switches are at the end of water_effects.cpp ("The index batch's three
// parts", "The index batch's draw").
//
// A batch is 0x40 bytes: +4 the primitive it is drawn as, +8 the vertices of
// a quad, +0xC the indices of a quad, +0x10 the vertices' format, +0x14 to
// +0x1C a vector of bytes (the vertices, +0x20 bytes each),
// +0x24 the vertex count, +0x28 a function that fills vertices, +0x2C to
// +0x34 a vector of 16-bit indices, +0x38 the index count and +0x3C a
// function that fills indices. The two fill functions are cdecl
// (begin, from, to). The vectors grow by their resize (the game's,
// 0x00465710 and 0x004610A0, called by address), and only when their size
// (not their capacity) is short.

// 0x005EC4A0 (no arguments, plain ret, answers 0): the vertex and index
// counts zeroed.
inline uint32_t IbClear(char* batch)
{
    At<uint32_t>(batch, 0x24) = 0;
    At<uint32_t>(batch, 0x38) = 0;
    return 0;
}

// 0x005EC850 (ret 4): the indices of `quads` quads (+0xC each) after the
// index count. When the vector's size (its bytes shifted right by one as a
// signed word) is below the count and those, `resize` (the vector at +0x2C,
// the new size, a zero) and then, when the batch has one, the fill at +0x3C
// with the new start, the old size and the new size. Answers where the added
// indices start; the count goes on by them. All of it in unsigned words, as
// the game's.
template <typename Resize, typename Fill>
uint32_t IbAddQuadsWith(char* batch, uint32_t quads, Resize resize, Fill fill)
{
    const uint32_t added = At<uint32_t>(batch, 0xC) * quads;
    const uint32_t need = At<uint32_t>(batch, 0x38) + added;
    const auto size = [&] {
        return static_cast<uint32_t>(static_cast<int32_t>(At<uint32_t>(batch, 0x30) - At<uint32_t>(batch, 0x2C)) >> 1);
    };
    if (size() < need) {
        const uint32_t old = size();
        const uint16_t zero = 0;
        resize(batch + 0x2C, need, zero);
        if (At<uint32_t>(batch, 0x3C))
            fill(At<uint32_t>(batch, 0x3C), At<uint32_t>(batch, 0x2C), old, need);
    }
    const uint32_t count = At<uint32_t>(batch, 0x38);
    At<uint32_t>(batch, 0x38) = count + added;
    return At<uint32_t>(batch, 0x2C) + count * 2;
}

// 0x005EC8C0 (ret 4): the vertices of `quads` quads (+8 each, +0x20 bytes
// each) after the vertex count. When the vector's bytes are below those of
// the count and them, `resize` (the vector at +0x14, the new bytes, a zero
// byte) and then, when the batch has one, the fill at +0x28 with the new
// start, the count and the count and those added. Answers where the added
// vertices start; the count goes on by them.
template <typename Resize, typename Fill>
uint32_t IbAddVerticesWith(char* batch, uint32_t quads, Resize resize, Fill fill)
{
    const uint32_t added = At<uint32_t>(batch, 8) * quads;
    const uint32_t bytes = (At<uint32_t>(batch, 0x24) + added) * At<uint32_t>(batch, 0x20);
    if (At<uint32_t>(batch, 0x18) - At<uint32_t>(batch, 0x14) < bytes) {
        const uint8_t zero = 0;
        resize(batch + 0x14, bytes, zero);
        if (At<uint32_t>(batch, 0x28)) {
            const uint32_t count = At<uint32_t>(batch, 0x24);
            fill(At<uint32_t>(batch, 0x28), At<uint32_t>(batch, 0x14), count, count + added);
        }
    }
    const uint32_t count = At<uint32_t>(batch, 0x24);
    At<uint32_t>(batch, 0x24) = count + added;
    return At<uint32_t>(batch, 0x20) * count + At<uint32_t>(batch, 0x14);
}

// The game's resizes and a batch's fill, as ours calls them.
inline void IbResizeIndices(char* vector, uint32_t size, uint16_t value)
{
    CallThis<void>(addr::SC4Uint16Vector_Resize, vector, size, &value);
}

inline void IbResizeVertices(char* vector, uint32_t bytes, uint8_t value)
{
    CallThis<void>(addr::SC4ByteVector_Resize2, vector, bytes, &value);
}

inline void IbFill(uint32_t fill, uint32_t begin, uint32_t from, uint32_t to)
{
    reinterpret_cast<void(__cdecl*)(uint32_t, uint32_t, uint32_t)>(fill)(begin, from, to);
}

inline uint32_t IbAddQuads(char* batch, uint32_t quads)
{
    return IbAddQuadsWith(batch, quads, &IbResizeIndices, &IbFill);
}

inline uint32_t IbAddVertices(char* batch, uint32_t quads)
{
    return IbAddVerticesWith(batch, quads, &IbResizeVertices, &IbFill);
}

// 0x005EC5D0 (ret 4): the batch drawn. Its vertices handed over first
// (`vertices`: the format at +0x10, the vertex count and the vertices at
// +0x14). With an index count (+0x38), while it is above 0x600, runs of
// 0x600 indices from its end (`indexed`: the primitive at +4, 0x600, the
// indices at +0x2C and the count less 0x600, as 16-bit words), the count
// lowered by 0x600 after each; then the rest from the first index. With
// none, the same on the vertex count (+0x24) with runs of vertices
// (`ranged`: the primitive, the first vertex, 0x600) and the rest from the
// first vertex (the primitive, zero, the count). The counts are read from
// the batch again after each call and are left at the last run's, as the
// game's leaves them; all of it unsigned. The calls' answers are not used.
template <typename Vertices, typename Indexed, typename Ranged>
void IbDrawWith(char* batch, Vertices vertices, Indexed indexed, Ranged ranged)
{
    constexpr uint32_t kRun = 0x600;
    vertices(At<uint32_t>(batch, 0x10), At<uint32_t>(batch, 0x24), At<uint32_t>(batch, 0x14));
    if (At<uint32_t>(batch, 0x38)) {
        while (At<uint32_t>(batch, 0x38) > kRun) {
            indexed(At<uint32_t>(batch, 4), kRun, At<uint32_t>(batch, 0x2C) + At<uint32_t>(batch, 0x38) * 2 - kRun * 2);
            At<uint32_t>(batch, 0x38) -= kRun;
        }
        indexed(At<uint32_t>(batch, 4), At<uint32_t>(batch, 0x38), At<uint32_t>(batch, 0x2C));
    } else {
        while (At<uint32_t>(batch, 0x24) > kRun) {
            ranged(At<uint32_t>(batch, 4), At<uint32_t>(batch, 0x24) - kRun, kRun);
            At<uint32_t>(batch, 0x24) -= kRun;
        }
        ranged(At<uint32_t>(batch, 4), 0u, At<uint32_t>(batch, 0x24));
    }
}

// The render context's three, as ours calls them (0x007D2970, 0x007D29F0
// and 0x007FC2D0, each passing on to the driver).
inline void IbDraw(char* batch, char* context)
{
    IbDrawWith(
        batch,
        [&](uint32_t format, uint32_t count, uint32_t at) {
            CallThis<void>(addr::SC4RenderContext_SetVertexArray, context, format, count, at);
        },
        [&](uint32_t primitive, uint32_t count, uint32_t at) {
            CallThis<void>(addr::SC4RenderContext_DrawElements, context, primitive, count, at);
        },
        [&](uint32_t primitive, uint32_t first, uint32_t count) {
            CallThis<void>(addr::SC4RenderContext_DrawVertexRange, context, primitive, first, count);
        });
}

// The ported callers: ours when the switch is ours (set at install), else
// the game's by its address (through the detour when the switch compares).
inline void IbClearFor(char* batch)
{
    if (g_batchClearOurs)
        IbClear(batch);
    else
        CallThis<uint32_t>(addr::SC4IndexBatch_Clear, batch);
}

inline uint32_t IbAddQuadsFor(char* batch, uint32_t quads)
{
    return g_batchAddQuadsOurs ? IbAddQuads(batch, quads)
                               : CallThis<uint32_t>(addr::SC4IndexBatch_AddQuads, batch, quads);
}

inline uint32_t IbAddVerticesFor(char* batch, uint32_t quads)
{
    return g_batchAddVerticesOurs ? IbAddVertices(batch, quads)
                                  : CallThis<uint32_t>(addr::SC4IndexBatch_AddVertices, batch, quads);
}

// The draw answers nothing the callers use; 0 for ours.
inline uint32_t IbDrawFor(char* batch, char* context)
{
    if (!g_batchDrawOurs)
        return CallThis<uint32_t>(addr::SC4IndexBatch_Draw, batch, context);
    IbDraw(batch, context);
    return 0;
}
