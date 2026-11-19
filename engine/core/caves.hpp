#pragma once
#include "core/world.hpp"

namespace vc {
bool cave_air(const World &world, Pos block, int surface);
Pos cave_start(const World &world);
} // namespace vc
