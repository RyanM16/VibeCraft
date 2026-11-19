#include "core/caves.hpp"
#include <algorithm>
#include <cmath>

namespace vc {
namespace {
constexpr int cell_size = 256;
struct Point {
    double x, y, z;
};

struct CaveCell {
    int64_t x, z;
    int mouth_x, mouth_z, variation;
};

CaveCell cave_cell(uint64_t seed, int64_t x, int64_t z) {
    auto cx = floor_div(x, cell_size), cz = floor_div(z, cell_size);
    auto key = mix(seed ^ mix(uint64_t(cx)) ^ mix(uint64_t(cz) + 0x918acb43ULL));
    return {cx * cell_size, cz * cell_size, 24 + int(key % 9), 32 + int((key >> 8) % 17),
            int((key >> 16) % 11) - 5};
}

bool chamber(Point p, Point center, Point radius) {
    double x = (p.x - center.x) / radius.x;
    double y = (p.y - center.y) / radius.y;
    double z = (p.z - center.z) / radius.z;
    return x * x + y * y + z * z < 1.;
}

bool passage(Point p, Point a, Point b, double radius) {
    if (p.x < std::min(a.x, b.x) - radius || p.x > std::max(a.x, b.x) + radius ||
        p.y < std::min(a.y, b.y) - radius || p.y > std::max(a.y, b.y) + radius ||
        p.z < std::min(a.z, b.z) - radius || p.z > std::max(a.z, b.z) + radius)
        return false;
    double x = b.x - a.x, y = b.y - a.y, z = b.z - a.z;
    double t =
        std::clamp(((p.x - a.x) * x + (p.y - a.y) * y + (p.z - a.z) * z) / (x * x + y * y + z * z), 0., 1.);
    return chamber(p, {a.x + x * t, a.y + y * t, a.z + z * t}, {radius, radius, radius});
}

Point mouth(const World &world, CaveCell cell) {
    return {double(cell.mouth_x) + .5, world.height(cell.x + cell.mouth_x, cell.z + cell.mouth_z) + 2.5,
            double(cell.mouth_z) + .5};
}
} // namespace

bool cave_air(const World &world, Pos block, int surface) {
    const auto cell = cave_cell(world.seed(), block.x, block.z);
    const Point p{double(block.x - cell.x) + .5, double(block.y) + .5, double(block.z - cell.z) + .5};
    if (p.x < 18 || p.x > 214 || p.z < 22 || p.z > 216 || p.y < surface - 180)
        return false;

    // One planned network per cell. All endpoints are in cell-local coordinates,
    // and the local field is evaluated identically by collision and either brick size.
    if (p.z < 113 && p.x <= 210) {
        Point entry = mouth(world, cell);
        Point bend{entry.x + 44, entry.y - 16, entry.z};
        Point hall{entry.x + 89, entry.y - 32, entry.z + 11};
        Point branch{entry.x + 145, entry.y - 46, entry.z + 39 + cell.variation};
        Point descent{entry.x + 120, entry.y - 58, entry.z - 1};
        Point deep{entry.x + 152, entry.y - 74, entry.z + 13};
        return passage(p, entry, bend, 3.8) || passage(p, bend, hall, 4.5) ||
               chamber(p, hall, {24, 14, 15}) || passage(p, hall, branch, 5.) ||
               chamber(p, branch, {18, 11, 16}) || passage(p, hall, descent, 4.5) ||
               passage(p, descent, deep, 5.5) || chamber(p, deep, {19, 17, 17});
    }

    // Smaller independent passages are common in the same area, off the main route.
    if (p.z >= 120 && p.y < surface - 5 && p.y > surface - 70) {
        double y = world.height(cell.x + 110, cell.z + 166) - 25.;
        Point a{43, y + 4, 151. + cell.variation}, b{88, y, 172}, c{129, y - 11, 165},
            d{165, y - 16, 193. + cell.variation};
        return passage(p, a, b, 3.) || chamber(p, b, {9, 6, 8}) || passage(p, b, c, 3.5) ||
               passage(p, c, d, 3.) || chamber(p, d, {9, 7, 10});
    }
    return false;
}

Pos cave_start(const World &world) {
    Point entry = mouth(world, cave_cell(world.seed(), 0, 0));
    return {int64_t(entry.x) + 20, int64_t(std::floor(entry.y - 16. * 20 / 44)), int64_t(entry.z)};
}
} // namespace vc
