#include "core/world.hpp"
#include "core/caves.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <tuple>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace vc {
uint64_t mix(uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
size_t Hash::operator()(Pos p) const noexcept {
    return mix(uint64_t(p.x)) ^ mix(uint64_t(p.y) + 17) ^ mix(uint64_t(p.z) + 37);
}
int64_t floor_div(int64_t a, int64_t b) {
    if (b <= 0)
        throw std::invalid_argument("positive divisor required");
    auto q = a / b;
    return q - (a % b < 0);
}
int World::height(int64_t x, int64_t z) const {
    auto noise = [&](int64_t scale, int amplitude) {
        auto gx = floor_div(x, scale), gz = floor_div(z, scale);
        double fx = double(x % scale + (x % scale < 0 ? scale : 0)) / scale,
               fz = double(z % scale + (z % scale < 0 ? scale : 0)) / scale;
        fx = fx * fx * (3 - 2 * fx);
        fz = fz * fz * (3 - 2 * fz);
        auto h = [&](int64_t a, int64_t b) {
            return double(mix(uint64_t(a) * 0x9e3779b97f4a7c15ULL ^ uint64_t(b) ^ seed_) % 10001) / 10000;
        };
        return amplitude * ((1 - fz) * ((1 - fx) * h(gx, gz) + fx * h(gx + 1, gz)) +
                            fz * ((1 - fx) * h(gx, gz + 1) + fx * h(gx + 1, gz + 1)));
    };
    return int(8 + noise(64, 30) + noise(512, 60) + noise(8192, 180));
}
uint8_t World::block(Pos p) const {
    {
        std::shared_lock lock(mutex_);
        auto it = edits_.find(p);
        if (it != edits_.end())
            return it->second;
    }
    int h = height(p.x, p.z);
    if (p.y > h)
        return 0;
    if (caves_ && cave_air(*this, p, h))
        return 0;
    if (!caves_ && p.y > 2 && p.y < h - 4 &&
        mix(uint64_t(floor_div(p.x, 4)) ^ mix(uint64_t(floor_div(p.y, 3))) ^
            mix(uint64_t(floor_div(p.z, 4))) ^ seed_) %
                19 ==
            0)
        return 0;
    return p.y == h ? 1 : (p.y > h - 4 ? 2 : 3);
}
void World::edit(Pos p, uint8_t m) {
    if (m > 3)
        throw std::invalid_argument("material outside palette");
    std::unique_lock lock(mutex_);
    edits_[p] = m;
    ++revision_;
}
size_t World::edit_count() const {
    std::shared_lock lock(mutex_);
    return edits_.size();
}
static void put(std::vector<uint8_t> &b, uint64_t v, int n) {
    for (int i = 0; i < n; ++i)
        b.push_back(uint8_t(v >> (8 * i)));
}
static uint64_t get(const std::vector<uint8_t> &b, size_t &at, int n) {
    if (at + size_t(n) > b.size())
        throw std::runtime_error("truncated save");
    uint64_t v = 0;
    for (int i = 0; i < n; ++i)
        v |= uint64_t(b[at++]) << (8 * i);
    return v;
}
static uint64_t checksum(const std::vector<uint8_t> &b, size_t n) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < n; ++i) {
        h ^= b[i];
        h *= 1099511628211ULL;
    }
    return h;
}
void World::save(const std::filesystem::path &path) const {
    std::vector<std::pair<Pos, uint8_t>> edits;
    {
        std::shared_lock lock(mutex_);
        edits.assign(edits_.begin(), edits_.end());
    }
    std::sort(edits.begin(), edits.end(), [](auto &a, auto &b) {
        return std::tie(a.first.x, a.first.y, a.first.z) < std::tie(b.first.x, b.first.y, b.first.z);
    });
    std::vector<uint8_t> b = {'V', 'C', 'E', 'D'};
    put(b, caves_ ? 2 : 1, 4);
    put(b, seed_, 8);
    put(b, edits.size(), 8);
    for (auto [p, m] : edits) {
        put(b, uint64_t(p.x), 8);
        put(b, uint64_t(p.y), 8);
        put(b, uint64_t(p.z), 8);
        put(b, m, 1);
    }
    put(b, checksum(b, b.size()), 8);
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char *>(b.data()), std::streamsize(b.size()));
        f.flush();
        if (!f)
            throw std::runtime_error("save write failed");
    }
    // Single-writer save protocol: flush data before atomically replacing the previous snapshot.
#ifdef _WIN32
    auto handle =
        CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("cannot open save for durable flush");
    bool flushed = FlushFileBuffers(handle);
    CloseHandle(handle);
    if (!flushed)
        throw std::runtime_error("durable flush failed");
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("atomic save replacement failed");
#else
    int fd = ::open(tmp.c_str(), O_RDONLY);
    if (fd < 0)
        throw std::runtime_error("cannot open save for durable flush");
    int result = ::fsync(fd);
    ::close(fd);
    if (result)
        throw std::runtime_error("durable flush failed");
    std::filesystem::rename(tmp, path);
    auto directory = path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();
    fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        throw std::runtime_error("cannot open save directory");
    result = ::fsync(fd);
    ::close(fd);
    if (result)
        throw std::runtime_error("directory flush failed");
#endif
}
void World::load(const std::filesystem::path &path) {
    auto length = std::filesystem::file_size(path);
    if (length < 32 || length > 64 * 1024 * 1024)
        throw std::runtime_error("invalid save length");
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> b(static_cast<size_t>(length));
    f.read(reinterpret_cast<char *>(b.data()), std::streamsize(length));
    if (!f)
        throw std::runtime_error("save read failed");
    size_t at = 4;
    auto version = get(b, at, 4);
    if (std::string(b.begin(), b.begin() + 4) != "VCED" || (version != 1 && version != 2))
        throw std::runtime_error("unsupported save version");
    if ((version == 2) != caves_)
        throw std::runtime_error(version == 2
                                     ? "cave world requires --caves"
                                     : "legacy world requires omitting --caves or choosing another --world");
    if (get(b, at, 8) != seed_)
        throw std::runtime_error("world seed mismatch");
    auto n = get(b, at, 8);
    if (n > (64 * 1024 * 1024 - 32) / 25 || 32 + n * 25 != length)
        throw std::runtime_error("invalid edit count");
    size_t tail = b.size() - 8;
    if (get(b, tail, 8) != checksum(b, b.size() - 8))
        throw std::runtime_error("save checksum mismatch");
    std::unordered_map<Pos, uint8_t, Hash> fresh;
    for (uint64_t i = 0; i < n; ++i) {
        Pos p{int64_t(get(b, at, 8)), int64_t(get(b, at, 8)), int64_t(get(b, at, 8))};
        auto m = get(b, at, 1);
        if (m > 3 || !fresh.emplace(p, uint8_t(m)).second)
            throw std::runtime_error("invalid save entry");
    }
    std::unique_lock lock(mutex_);
    edits_.swap(fresh);
    ++revision_;
}
Chunk generate(const World &w, Pos o, int side) {
    if (side != 16 && side != 32)
        throw std::invalid_argument("chunk side must be 16 or 32");
    Chunk c{o, side, {}};
    c.voxels.resize(size_t(side) * side * side);
    for (int z = 0; z < side; ++z)
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x)
                c.voxels[(z * side + y) * side + x] = w.block({o.x + x, o.y + y, o.z + z});
    return c;
}
static void quad(Mesh &m, std::array<float, 3> p, std::array<float, 3> u, std::array<float, 3> v,
                 int material, bool reverse, int axis) {
    static constexpr float colors[4][3] = {
        {0, 0, 0}, {.28f, .62f, .24f}, {.46f, .30f, .16f}, {.48f, .49f, .52f}};
    auto base = uint32_t(m.vertices.size());
    float shade = axis == 1 ? 1.f : (axis == 0 ? .78f : .6f);
    for (auto [a, b] : {std::pair{0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}})
        m.vertices.push_back({p[0] + a * u[0] + b * v[0], p[1] + a * u[1] + b * v[1],
                              p[2] + a * u[2] + b * v[2], colors[material][0] * shade,
                              colors[material][1] * shade, colors[material][2] * shade});
    for (auto i : reverse ? std::array{0, 2, 1, 0, 3, 2} : std::array{0, 1, 2, 0, 2, 3})
        m.indices.push_back(base + i);
}
Mesh greedy_mesh(const Chunk &c, const std::function<uint8_t(Pos)> &sample, Pos relative) {
    Mesh m;
    int n = c.side;
    std::vector<int> mask(n * n);
    int64_t origin[3] = {c.origin.x, c.origin.y, c.origin.z}, rel[3] = {relative.x, relative.y, relative.z};
    auto block = [&](int x[3]) {
        if (x[0] >= 0 && x[1] >= 0 && x[2] >= 0 && x[0] < n && x[1] < n && x[2] < n)
            return c.at(x[0], x[1], x[2]);
        return sample({origin[0] + x[0], origin[1] + x[1], origin[2] + x[2]});
    };
    for (int d = 0; d < 3; ++d) {
        int u = (d + 1) % 3, v = (d + 2) % 3;
        int x[3] = {};
        for (x[d] = -1; x[d] < n;) {
            for (x[v] = 0; x[v] < n; ++x[v])
                for (x[u] = 0; x[u] < n; ++x[u]) {
                    auto a = block(x);
                    ++x[d];
                    auto b = block(x);
                    --x[d];
                    int value = 0;
                    if (bool(a) != bool(b)) {
                        if (a && x[d] >= 0)
                            value = a;
                        else if (b && x[d] + 1 < n)
                            value = -int(b);
                    }
                    mask[x[v] * n + x[u]] = value;
                }
            ++x[d];
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n;) {
                    int val = mask[j * n + i];
                    if (!val) {
                        ++i;
                        continue;
                    }
                    int w = 1;
                    while (i + w < n && mask[j * n + i + w] == val)
                        ++w;
                    int h = 1;
                    bool same = true;
                    while (j + h < n && same) {
                        for (int k = 0; k < w; ++k)
                            if (mask[(j + h) * n + i + k] != val) {
                                same = false;
                                break;
                            }
                        if (same)
                            ++h;
                    }
                    x[u] = i;
                    x[v] = j;
                    std::array<float, 3> p{}, du{}, dv{};
                    for (int a = 0; a < 3; ++a)
                        p[a] = float(origin[a] - rel[a] + x[a]);
                    du[u] = float(w);
                    dv[v] = float(h);
                    quad(m, p, du, dv, std::abs(val), val < 0, d);
                    for (int b = 0; b < h; ++b)
                        for (int a = 0; a < w; ++a)
                            mask[(j + b) * n + i + a] = 0;
                    i += w;
                }
        }
    }
    return m;
}
void append(Mesh &a, const Mesh &b) {
    auto base = uint32_t(a.vertices.size());
    a.vertices.insert(a.vertices.end(), b.vertices.begin(), b.vertices.end());
    for (auto i : b.indices)
        a.indices.push_back(base + i);
}
Mesh surface_ring(const World &w, int64_t extent, int64_t inner, int step, Pos origin) {
    if (extent <= 0 || inner < 0 || inner >= extent || step <= 0 || extent > 1000000 || extent / step > 512)
        throw std::invalid_argument("invalid surface ring dimensions");
    Mesh m;
    std::vector<int64_t> axis{-extent, -inner, inner, extent};
    for (int64_t v = -extent; v < extent; v += step)
        axis.push_back(v);
    std::sort(axis.begin(), axis.end());
    axis.erase(std::unique(axis.begin(), axis.end()), axis.end());
    for (size_t zi = 1; zi < axis.size(); ++zi)
        for (size_t xi = 1; xi < axis.size(); ++xi) {
            auto x = axis[xi - 1], z = axis[zi - 1], x1 = axis[xi], z1 = axis[zi];
            if (x >= -inner && x1 <= inner && z >= -inner && z1 <= inner)
                continue;
            auto base = uint32_t(m.vertices.size());
            for (auto [wx, wz] : {std::pair{x, z}, std::pair{x1, z}, std::pair{x1, z1}, std::pair{x, z1}}) {
                float y = float(w.height(origin.x + wx, origin.z + wz) + 1 - origin.y);
                m.vertices.push_back({float(wx), y, float(wz), .23f, .48f, .22f});
            }
            for (auto i : {0, 2, 1, 0, 3, 2})
                m.indices.push_back(base + i);
        }
    // Skirts mask T-junction gaps for this experiment; they are not a geomorph or silhouette solution.
    for (int64_t edge : {-inner, inner, -extent, extent}) {
        int64_t limit = std::abs(edge);
        if (!limit)
            continue;
        std::vector<int64_t> segment{-limit, limit};
        for (auto v : axis)
            if (v > -limit && v < limit)
                segment.push_back(v);
        std::sort(segment.begin(), segment.end());
        for (size_t i = 1; i < segment.size(); ++i)
            for (int side = 0; side < 2; ++side) {
                int64_t x0 = side ? edge : segment[i - 1], z0 = side ? segment[i - 1] : edge,
                        x1 = side ? edge : segment[i], z1 = side ? segment[i] : edge;
                float h0 = float(w.height(origin.x + x0, origin.z + z0) + 1 - origin.y),
                      h1 = float(w.height(origin.x + x1, origin.z + z1) + 1 - origin.y);
                auto base = uint32_t(m.vertices.size());
                m.vertices.push_back({float(x0), h0, float(z0), .23f, .48f, .22f});
                m.vertices.push_back({float(x1), h1, float(z1), .23f, .48f, .22f});
                m.vertices.push_back({float(x1), h1 - 64, float(z1), .23f, .48f, .22f});
                m.vertices.push_back({float(x0), h0 - 64, float(z0), .23f, .48f, .22f});
                for (auto j : {0, 1, 2, 0, 2, 3})
                    m.indices.push_back(base + j);
            }
    }

    return m;
}
bool raycast(const World &w, std::array<double, 3> p, std::array<double, 3> d, double maxd, Hit &hit) {
    double len = std::hypot(d[0], d[1], d[2]);
    if (!std::isfinite(len) || len == 0 || maxd < 0)
        return false;
    for (auto &v : d)
        v /= len;
    int64_t cell[3];
    int step[3];
    double next[3], delta[3];
    for (int a = 0; a < 3; ++a) {
        if (!std::isfinite(p[a]) || std::abs(p[a]) > 9e15)
            return false;
        cell[a] = int64_t(std::floor(p[a]));
        step[a] = d[a] > 0 ? 1 : -1;
        delta[a] = d[a] == 0 ? INFINITY : std::abs(1 / d[a]);
        next[a] = d[a] == 0 ? INFINITY : ((d[a] > 0 ? double(cell[a]) + 1 : double(cell[a])) - p[a]) / d[a];
    }
    Pos previous{cell[0], cell[1], cell[2]};
    double t = 0;
    while (t <= maxd) {
        Pos q{cell[0], cell[1], cell[2]};
        if (w.block(q)) {
            hit = {q, previous, t};
            return true;
        }
        previous = q;
        int a = int(std::min_element(next, next + 3) - next);
        t = next[a];
        cell[a] += step[a];
        next[a] += delta[a];
    }
    return false;
}
bool collides(const World &w, std::array<double, 3> lo, std::array<double, 3> hi) {
    for (int64_t z = int64_t(std::floor(lo[2])); z < int64_t(std::ceil(hi[2])); ++z)
        for (int64_t y = int64_t(std::floor(lo[1])); y < int64_t(std::ceil(hi[1])); ++y)
            for (int64_t x = int64_t(std::floor(lo[0])); x < int64_t(std::ceil(hi[0])); ++x)
                if (w.block({x, y, z}))
                    return true;
    return false;
}
} // namespace vc
