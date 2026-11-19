#pragma once
#include "core/world.hpp"

namespace vc {
// Build the surface proxy outside the exact voxel window, in the same local coordinates.
Mesh terrain_horizon(const World &world, Pos origin, int near_half_width, int extent);
} // namespace vc
