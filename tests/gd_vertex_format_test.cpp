// GD vertex format helpers against values from the game's own helpers
// (0x008D0370's table of legacy strides, and layouts the OpenGL driver reads).
#include "core/gd/vertex_format.h"

#include <cstdio>

using namespace opensc4::gd;

static int g_failures = 0;

static void Check(bool ok, const char* what, unsigned got, unsigned want)
{
    if (!ok) {
        std::printf("FAIL: %s: got %u (0x%X), want %u (0x%X)\n", what, got, got, want, want);
        ++g_failures;
    }
}

#define EXPECT_EQ(got, want) Check((got) == (want), #got, static_cast<unsigned>(got), static_cast<unsigned>(want))

int main()
{
    // Legacy IDs: 0x008D0370 returns these strides directly.
    const struct { unsigned id, stride; } legacy[] = {
        {0x01, 16}, {0x02, 20}, {0x03, 28}, {0x0A, 24}, {0x0B, 32}, {0x20, 12},
        {0x21, 24}, {0x22, 28}, {0x23, 32}, {0x24, 40}, {0x25, 36}, {0x26, 44},
    };
    for (const auto& f : legacy) {
        EXPECT_EQ(Stride(f.id), f.stride);
        EXPECT_EQ(Stride(ExpandFormat(f.id)), f.stride);
    }
    EXPECT_EQ(ExpandFormat(0x07), 0u);

    // Position, normal, color, 2 texcoords (legacy 0x26).
    const unsigned f = ExpandFormat(0x26);
    EXPECT_EQ(ElementCount(f, Element::Position), 1);
    EXPECT_EQ(ElementCount(f, Element::Normal), 1);
    EXPECT_EQ(ElementCount(f, Element::Color), 1);
    EXPECT_EQ(ElementCount(f, Element::TexCoord2), 2);
    EXPECT_EQ(ElementOffset(f, Element::Position, 0), 0);
    EXPECT_EQ(ElementOffset(f, Element::Normal, 0), 12);
    EXPECT_EQ(ElementOffset(f, Element::Color, 0), 24);
    EXPECT_EQ(ElementOffset(f, Element::TexCoord2, 0), 28);
    EXPECT_EQ(ElementOffset(f, Element::TexCoord2, 1), 36);

    // MakeVertexFormat from an element list.
    const unsigned elements[] = {0, 5, 7, 7};
    EXPECT_EQ(MakeFormat(4, elements), ExpandFormat(0x0B));

    if (g_failures == 0)
        std::printf("gd vertex format test OK\n");
    return g_failures == 0 ? 0 : 1;
}
