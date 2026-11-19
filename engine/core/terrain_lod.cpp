#include "core/terrain_lod.hpp"
#include <cstdlib>
#include <stdexcept>

namespace vc {
Mesh terrain_horizon(const World &world, Pos origin, int near_half_width, int extent) {
    if (near_half_width < 32 || near_half_width > 64 || near_half_width % 16 ||
        (extent != 2048 && extent != 8192))
        throw std::invalid_argument("unsupported terrain horizon dimensions");

    Mesh mesh;
    auto sample = [&](int x, int z) {
        return Vertex{float(x), float(world.height(origin.x + x, origin.z + z) + 1 - origin.y),
                      float(z), .25f,
                      .51f,     .23f};
    };
    auto skirt = [&](int x, int z, int u, int v) {
        auto a = sample(x, z), b = sample(u, v);
        auto bottom_a = a, bottom_b = b;
        bottom_a.y -= 32;
        bottom_b.y -= 32;
        auto base = uint32_t(mesh.vertices.size());
        mesh.vertices.insert(mesh.vertices.end(), {a, b, bottom_b, bottom_a});
        for (auto index : {0u, 1u, 2u, 0u, 2u, 3u})
            mesh.indices.push_back(base + index);
    };

    int inner = near_half_width;
    for (int outer = 128, step = 4; outer <= extent; outer *= 2, step *= 2) {
        for (int z = -outer; z < outer; z += step)
            for (int x = -outer; x < outer; x += step) {
                int u = x + step, v = z + step;
                if (x >= -inner && u <= inner && z >= -inner && v <= inner)
                    continue;

                auto base = uint32_t(mesh.vertices.size());
                mesh.vertices.push_back(sample(x, z));
                // Split the coarse side where it meets two fine edges. Both tiers use the
                // identical world sample at the midpoint, so there is no LOD T-junction.
                bool stitch = outer != 128;
                if (stitch && z == inner && x >= -inner && u <= inner)
                    mesh.vertices.push_back(sample(x + step / 2, z));
                mesh.vertices.push_back(sample(u, z));
                if (stitch && u == -inner && z >= -inner && v <= inner)
                    mesh.vertices.push_back(sample(u, z + step / 2));
                mesh.vertices.push_back(sample(u, v));
                if (stitch && v == -inner && x >= -inner && u <= inner)
                    mesh.vertices.push_back(sample(x + step / 2, v));
                mesh.vertices.push_back(sample(x, v));
                if (stitch && x == inner && z >= -inner && v <= inner)
                    mesh.vertices.push_back(sample(x, z + step / 2));
                auto end = uint32_t(mesh.vertices.size());
                if (end - base == 4) {
                    for (auto index : {0u, 2u, 1u, 0u, 3u, 2u})
                        mesh.indices.push_back(base + index);
                } else {
                    mesh.vertices.push_back(sample(x + step / 2, z + step / 2));
                    for (auto i = base; i < end; ++i)
                        mesh.indices.insert(mesh.indices.end(), {end, i + 1 == end ? base : i + 1, i});
                }
            }
        inner = outer;
    }

    // The approximation meets vertical voxel faces at the near edge. Hide the gap
    // with a curtain here only. The surface-to-surface joins above share edges.
    for (int i = -near_half_width; i < near_half_width; i += 4) {
        int j = i + 4, edge = near_half_width;
        skirt(i, -edge, j, -edge);
        skirt(j, edge, i, edge);
        skirt(-edge, j, -edge, i);
        skirt(edge, i, edge, j);
    }
    return mesh;
}
} // namespace vc
