#include "core/player.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vc {
namespace {
constexpr double half_width = .30;
constexpr double body_height = 1.80;
bool blocked(const World &world, std::array<double, 3> feet) {
    auto [x, y, z] = feet;
    return collides(world, {x - half_width, y, z - half_width},
                    {x + half_width, y + body_height, z + half_width});
}
} // namespace

void Player::respawn(const World &world) {
    for (int y = world.height(0, 0) - 4; y <= world.height(0, 0) + 32; ++y) {
        std::array<double, 3> candidate{.5, double(y), .5};
        if (world.block({0, y - 1, 0}) && !blocked(world, candidate)) {
            feet = candidate;
            vertical_speed = 0;
            grounded = true;
            return;
        }
    }
    throw std::runtime_error("spawn column obstructed; select another world");
}

bool Player::overlaps(Pos block) const {
    auto [x, y, z] = feet;
    return x + half_width > block.x && x - half_width < block.x + 1 && y + body_height > block.y &&
           y < block.y + 1 && z + half_width > block.z && z - half_width < block.z + 1;
}

void Player::move(const World &world, double vx, double vz, bool jump, double seconds) {
    if (jump && grounded)
        vertical_speed = 7.5;
    // Cap frame catch-up and substep so neither sprinting nor falling can skip through a voxel.
    for (double remaining = std::clamp(seconds, 0., .1); remaining > 0;) {
        const double dt = std::min(remaining, .008);
        remaining -= dt;
        vertical_speed = std::max(-32., vertical_speed - 22. * dt);
        const std::array displacement{vx * dt, vertical_speed * dt, vz * dt};
        for (int axis : {0, 2, 1}) {
            auto candidate = feet;
            candidate[axis] += displacement[axis];
            const bool hit = blocked(world, candidate);
            if (!hit)
                feet = candidate;
            if (axis == 1) {
                grounded = hit && vertical_speed < 0;
                if (hit)
                    vertical_speed = 0;
            }
        }
    }
}
} // namespace vc
