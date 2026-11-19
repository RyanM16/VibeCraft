#pragma once
#include "core/world.hpp"

namespace vc {
struct Player {
    std::array<double, 3> feet{};
    double vertical_speed = 0;
    bool grounded = false;

    void respawn(const World &world);
    void spawn_near(const World &world, Pos position);
    void move(const World &world, double vx, double vz, bool jump, double seconds);
    bool overlaps(Pos block) const;
};

inline constexpr double eye_height = 1.62;
} // namespace vc
