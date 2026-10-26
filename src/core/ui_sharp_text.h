// renderer.interface.sharp_text: interface text drawn again at the physical
// size under a render scale (docs/ui-scaling.md, "Sharp interface text").
// Our DrawString (font_draw.cpp) reports each line it draws into a picture;
// this keeps a physical-size copy of the line's rectangle (the background as
// it was before the line, filtered up, with the line's glyphs rendered by
// FreeType from the font's own face at the physical size), and the driver's
// uploads of the picture's tiles (renderer.interface.sharp_tiles) take it
// while the picture still holds what the line left.
#pragma once

#include <cstdint>
#include <vector>

namespace opensc4::sharptext {

// Whether text drawn into `picture` gets a physical-size copy: the group on,
// a render scale, a 32-bit picture.
bool Wanted(void* picture);

// One line DrawString draws: made before its glyphs are placed, given each
// glyph it keeps (its pen and the box it fills), then Before() just before
// the blitter draws them and After() just after.
class Line {
public:
    Line(void* font, void* picture, const int32_t* clip, uint32_t kind, const uint32_t* colours);
    void Glyph(int32_t penX, int32_t penY, uint32_t character, int32_t left, int32_t top, int32_t right,
               int32_t bottom);
    void Before();
    void After();

private:
    struct Placed {
        int32_t penX, penY;
        uint32_t character;
    };
    void* font_;
    void* picture_;
    int32_t clip_[4];
    bool clipped_;
    uint32_t kind_;
    const uint32_t* colours_;
    std::vector<Placed> glyphs_;
    int32_t rect_[4];
    bool any_ = false;
    std::vector<uint8_t> before_; // the rectangle's BGRA before the line
    bool keyed_ = false;
    uint32_t key_ = 0;
};

} // namespace opensc4::sharptext
