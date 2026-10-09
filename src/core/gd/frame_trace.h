// Draw-call parity (docs/renderer.md, "Draw-call parity"): a checksum of
// every draw of a city frame (cSC43DRender::Draw), from the state and vertex
// data the D3D11 driver hands its backend. Two renderers that draw the same
// frame give the same checksum, whatever the backend does with it.
#pragma once

#include "core/gd/backend.h"

#include <cstdint>

namespace opensc4::gd::frame_trace {

struct Frame {
    uint32_t number = 0; // city frames finished so far
    uint32_t draws = 0;
    uint32_t hash = 0;
};

// Whether frames are traced (the diagnostics.trace.frames replacement).
bool Active();

// Called by the driver for every draw it hands the backend.
void Record(const DrawState& state, const DrawCall& call);

// Called by the driver for texture creation and uploads: draws are hashed
// with their textures' contents, not the handles (which follow load order).
void TextureCreated(uint32_t texture, uint32_t format, uint32_t width, uint32_t height, uint32_t levels);
void TextureUploaded(uint32_t texture, int32_t level, int32_t x, int32_t y, int32_t width, int32_t height,
                     uint32_t format, uint32_t type, uint32_t rowLength, const void* pixels);

// The last finished city frame.
Frame Last();

// Starts watching for the next frame (after a full redraw is asked for);
// FirstSinceMark is the first frame finished after it (number 0: none yet).
void Mark();
Frame FirstSinceMark();

} // namespace opensc4::gd::frame_trace
