#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
namespace vc {
struct Pos {
    int64_t x{}, y{}, z{};
    bool operator==(const Pos &) const = default;
};
struct Hash {
    size_t operator()(Pos p) const noexcept;
};
int64_t floor_div(int64_t a, int64_t b);
uint64_t mix(uint64_t x);
class World {
    uint64_t seed_;
    bool caves_;
    std::atomic<uint64_t> revision_{0};
    mutable std::shared_mutex mutex_;
    std::unordered_map<Pos, uint8_t, Hash> edits_;

  public:
    explicit World(uint64_t seed = 42, bool caves = false) : seed_(seed), caves_(caves) {}
    int height(int64_t x, int64_t z) const;
    uint64_t revision() const {
        return revision_.load();
    }
    uint8_t block(Pos p) const;
    void edit(Pos p, uint8_t material);
    size_t edit_count() const;
    void save(const std::filesystem::path &path) const;
    void load(const std::filesystem::path &path);
    uint64_t seed() const {
        return seed_;
    }
};
struct Vertex {
    float x, y, z, r, g, b;
};
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    size_t bytes() const {
        return vertices.capacity() * sizeof(Vertex) + indices.capacity() * sizeof(uint32_t);
    }
};
struct Chunk {
    Pos origin;
    int side;
    std::vector<uint8_t> voxels;
    uint8_t at(int x, int y, int z) const {
        return voxels[(z * side + y) * side + x];
    }
};
Chunk generate(const World &w, Pos origin, int side);
Mesh greedy_mesh(const Chunk &c, const std::function<uint8_t(Pos)> &sample, Pos camera_origin = {});
Mesh surface_ring(const World &w, int64_t extent, int64_t inner, int step, Pos origin = {});
void append(Mesh &into, const Mesh &from);
struct Hit {
    Pos block, previous;
    double distance;
};
bool raycast(const World &w, std::array<double, 3> from, std::array<double, 3> direction, double max_distance,
             Hit &hit);
bool collides(const World &w, std::array<double, 3> lo, std::array<double, 3> hi);
} // namespace vc
