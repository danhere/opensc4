// Our cSC4CameraControl (camera_control.cpp; docs/renderer.md, "The camera
// control").
#pragma once

namespace opensc4::camera_control {

// OPENSC4_TEST_CAMERA_EXERCISE=1 in the shots harness: scrolls past the
// bounds, rotates, zooms past the maximum, shakes, recentres and updates
// the city camera through the game's entry points (so whichever code is
// installed there runs), before the shot's own zoom and rotation.
void Exercise(void* control, int step);

} // namespace opensc4::camera_control
