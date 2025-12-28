#pragma once

namespace arksim {

// Marker component: entity is scheduled for destruction at the beginning of next frame.
// Within the current frame, treat it as logically destroyed.
struct Destroyed {};

} // namespace arksim
