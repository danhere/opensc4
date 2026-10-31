// The game's pool of fixed-size blocks and the particle's clear, written by
// hand from the game's code: the pool's grow (0x005A61B0), its allocate
// (0x005A6290) and a particle's clear (0x005B9DF0). Included by
// water_effects.cpp before the particle pool, whose takes call them; the
// switches are at the end of water_effects.cpp ("The pool's three parts").
//
// A pool is four words: +0 the size of a block, +4 the bytes of a chunk, +8
// the chunks (each linked to the one before by its first word) and +0xC the
// free list (each block linked to the next by its first word). The particle
// pools (0x00B0A67C, 0x00B0A5C0, 0x00B0A594) take from the list inline;
// the database files' records go through the allocate.

// 0x005A61B0 (no arguments, plain ret): a new chunk of +4 plus 8 bytes from
// `allocate` (the game's malloc, 0x009F066E, its answer not checked). Its
// first word links it to the chunks; from its fifth byte on, as many blocks
// as fit, each linked to the next and the last to none (written even when
// only one fits); the free list is the first. What follows the blocks and
// the blocks' own bytes are left as the allocation gave them. The divisions
// and products are of unsigned words, as the game's. Answers the last block,
// which the game's leaves in EAX.
template <typename Allocate>
uint32_t PlGrowWith(char* pool, Allocate allocate)
{
    char* const chunk = allocate(At<uint32_t>(pool, 4) + 8);
    At<uint32_t>(chunk, 0) = At<uint32_t>(pool, 8);
    At<char*>(pool, 8) = chunk;
    const uint32_t size = At<uint32_t>(pool, 0);
    const uint32_t first = reinterpret_cast<uint32_t>(chunk) + 4;
    const uint32_t last = (At<uint32_t>(pool, 4) / size - 1) * size + first;
    for (uint32_t block = first; block < last; block += size)
        M<uint32_t>(block) = block + size;
    M<uint32_t>(last) = 0;
    At<uint32_t>(pool, 0xC) = first;
    return last;
}

inline char* PlMalloc(uint32_t bytes)
{
    return reinterpret_cast<char*(__cdecl*)(std::size_t)>(addr::CRT_malloc)(bytes);
}

inline void PlFree(void* block)
{
    reinterpret_cast<void(__cdecl*)(void*)>(addr::CRT_free)(block);
}

inline uint32_t PlGrow(char* pool)
{
    return PlGrowWith(pool, &PlMalloc);
}

// 0x005A6290 (no arguments, plain ret): the first block of the free list,
// after `grow` when the list is empty; the list goes on from its link.
template <typename Grow>
uint32_t PlAllocateWith(char* pool, Grow grow)
{
    if (!At<uint32_t>(pool, 0xC))
        grow(pool);
    const uint32_t block = At<uint32_t>(pool, 0xC);
    At<uint32_t>(pool, 0xC) = M<uint32_t>(block);
    return block;
}

// 0x005B9DF0 (no arguments, plain ret, answers the particle): +0xC, +0x10
// and +0x14 get the float at 0x00B22E20, then the first 0x1C bytes are
// zeroed (0x0090FFB1, the C runtime's memset), those three with them.
inline char* PlClear(char* particle)
{
    for (const std::size_t offset : {std::size_t{0xC}, std::size_t{0x10}, std::size_t{0x14}})
        At<uint32_t>(particle, offset) = M<uint32_t>(0x00B22E20);
    std::memset(particle, 0, 0x1C);
    return particle;
}

// The ported callers: ours when the switch is ours (set at install), else
// the game's by its address (through the detour when the switch compares).
inline void PlGrowFor(std::uintptr_t pool)
{
    if (g_growOurs)
        PlGrow(reinterpret_cast<char*>(pool));
    else
        CallThis<void>(addr::SC4Pool_Grow, reinterpret_cast<void*>(pool));
}

inline void PlClearFor(void* particle)
{
    if (g_clearOurs)
        PlClear(static_cast<char*>(particle));
    else
        CallThis<void>(addr::SC4Particle_Clear, particle);
}
