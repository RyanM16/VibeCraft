#include "core/caves.hpp"
#include "core/player.hpp"
#include "core/tasks.hpp"
#include "core/terrain_lod.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>
using namespace vc;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " at line " + std::to_string(__LINE__));              \
    } while (0)
static void drain(Streamer &s) {
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (s.stats().pending) {
        s.poll(16 * 1024 * 1024);
        if (std::chrono::steady_clock::now() > until)
            throw std::runtime_error("streamer timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
int main() {
    int passed = 0;
    auto test = [&](const char *name, auto fn) {
        try {
            fn();
            ++passed;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception &e) {
            std::cerr << "FAIL " << name << ": " << e.what() << '\n';
            throw;
        }
    };
    try {
        test("connected cave crosses brick boundaries and preserves its save profile", [] {
            World cave(42, true), legacy(42);
            auto start = cave_start(cave);
            Player player;
            player.spawn_near(cave, start);
            CHECK(player.grounded);
            CHECK(player.feet[1] < cave.height(start.x, start.z));
            CHECK(!cave.block(start));
            CHECK(legacy.block(start));
            std::vector<Pos> reachable{start};
            std::unordered_set<Pos, Hash> visited{start};
            constexpr Pos adjacent[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            int64_t farthest = start.x, deepest = start.y;
            for (size_t i = 0; i < reachable.size(); ++i) {
                auto p = reachable[i];
                farthest = std::max(farthest, p.x);
                deepest = std::min(deepest, p.y);
                for (auto step : adjacent) {
                    Pos next{p.x + step.x, p.y + step.y, p.z + step.z};
                    if (next.y < cave.height(next.x, next.z) - 3 && visited.insert(next).second &&
                        !cave.block(next))
                        reachable.push_back(next);
                }
            }
            CHECK(farthest - start.x > 130);
            CHECK(start.y - deepest > 75);
            const Pos branch{start.x + 125, start.y - 39, start.z + 39};
            CHECK(std::find(reachable.begin(), reachable.end(), branch) != reachable.end());
            const Pos ordinary{88, cave.height(110, 166) - 25, 172};
            CHECK(!cave.block(ordinary));
            CHECK(std::find(reachable.begin(), reachable.end(), ordinary) == reachable.end());
            auto chunk_origin =
                Pos{floor_div(start.x, 32) * 32, floor_div(start.y, 32) * 32, floor_div(start.z, 32) * 32};
            auto wide = generate(cave, chunk_origin, 32);
            auto across = chunk_origin;
            across.x += 16;
            auto narrow = generate(cave, across, 16);
            for (int z = 0; z < 16; ++z)
                for (int y = 0; y < 16; ++y)
                    for (int x = 0; x < 16; ++x)
                        CHECK(wide.at(x + 16, y, z) == narrow.at(x, y, z));

            auto file = std::filesystem::temp_directory_path() / "vibecraft-cave-profile-test.vced";
            legacy.save(file);
            bool rejected_legacy = false;
            try {
                cave.load(file);
            } catch (const std::runtime_error &) {
                rejected_legacy = true;
            }
            CHECK(rejected_legacy);
            cave.edit(start, 3);
            cave.save(file);
            World reloaded(42, true);
            reloaded.load(file);
            CHECK(reloaded.block(start) == 3);
            CHECK(reloaded.block(reachable.back()) == 0);
            bool rejected = false;
            try {
                legacy.load(file);
            } catch (const std::runtime_error &) {
                rejected = true;
            }
            CHECK(rejected);
            std::filesystem::remove(file);
        });
        test("playable movement uses current voxels at chunk boundaries", [] {
            World world;
            Player player;
            player.respawn(world);
            CHECK(player.grounded);
            CHECK(!world.block({0, int64_t(player.feet[1]), 0}));
            player.feet = {15.5, 500., .5};
            for (int x = 14; x <= 19; ++x)
                world.edit({x, 499, 0}, 3);
            world.edit({16, 500, 0}, 3);
            world.edit({16, 501, 0}, 3);
            player.move(world, 7., 0., false, .1);
            CHECK(player.feet[0] < 15.71);
            CHECK(player.grounded);
            Hit hit;
            CHECK(raycast(world, {player.feet[0], 500.5, .5}, {1., 0., 0.}, 8., hit));
            CHECK((hit.block == Pos{16, 500, 0}));
            CHECK(player.overlaps(hit.previous));
            world.edit(hit.block, 0);
            world.edit({16, 501, 0}, 0);
            player.move(world, 7., 0., false, .1);
            CHECK(player.feet[0] > 16.);
            player.move(world, 0., 0., true, .1);
            CHECK(player.feet[1] > 500.5);
            for (int i = 0; i < 20; ++i)
                player.move(world, 0., 0., false, .05);
            CHECK(player.grounded);
            CHECK(std::abs(player.feet[1] - 500.) < .01);
        });
        test("negative and large coordinates", [] {
            CHECK(floor_div(-1, 16) == -1);
            CHECK(floor_div(-16, 16) == -1);
            CHECK(floor_div(-17, 16) == -2);
            World a(42), b(42), c(43);
            bool differs = false;
            for (int64_t x : {-9000000000LL, -17LL, 0LL, 33536LL, 9000000000LL}) {
                CHECK(a.height(x, -x) == b.height(x, -x));
                differs |= a.height(x, 0) != c.height(x, 0);
            }
            CHECK(differs);
        });
        test("chunk source consistency 16 and 32", [] {
            World w;
            for (int n : {16, 32}) {
                auto c = generate(w, {-16, 64, -16}, n);
                for (int z = 0; z < n; ++z)
                    for (int y = 0; y < n; ++y)
                        for (int x = 0; x < n; ++x)
                            CHECK(c.at(x, y, z) == w.block({x - 16, y + 64, z - 16}));
            }
        });
        test("greedy cube reduces to six quads", [] {
            Chunk c{{0, 0, 0}, 16, std::vector<uint8_t>(4096, 3)};
            auto m = greedy_mesh(c, [](Pos) { return uint8_t(0); });
            CHECK(m.vertices.size() == 24);
            CHECK(m.indices.size() == 36);
            for (auto i : m.indices)
                CHECK(i < m.vertices.size());
        });
        test("neighbor faces eliminated and empty chunks", [] {
            Chunk c{{0, 0, 0}, 16, std::vector<uint8_t>(4096, 3)};
            CHECK(greedy_mesh(c, [](Pos) { return uint8_t(3); }).indices.empty());
            auto m = greedy_mesh(c, [](Pos p) { return uint8_t(p.x >= 16 ? 3 : 0); });
            CHECK(m.indices.size() == 30);
            std::fill(c.voxels.begin(), c.voxels.end(), 0);
            CHECK(greedy_mesh(c, [](Pos) { return uint8_t(0); }).indices.empty());
        });
        test("mesh coverage equals brute force", [] {
            World w;
            auto c = generate(w, {-16, w.height(0, 0) - 8, 0}, 16);
            auto m = greedy_mesh(c, [&](Pos p) { return w.block(p); });
            size_t faces = 0;
            for (int z = 0; z < 16; ++z)
                for (int y = 0; y < 16; ++y)
                    for (int x = 0; x < 16; ++x) {
                        Pos p{c.origin.x + x, c.origin.y + y, c.origin.z + z};
                        if (!w.block(p))
                            continue;
                        for (auto d : {Pos{1, 0, 0}, Pos{-1, 0, 0}, Pos{0, 1, 0}, Pos{0, -1, 0}, Pos{0, 0, 1},
                                       Pos{0, 0, -1}})
                            if (!w.block({p.x + d.x, p.y + d.y, p.z + d.z}))
                                ++faces;
                    }
            double area = 0;
            for (size_t i = 0; i < m.vertices.size(); i += 4) {
                auto a = m.vertices[i], b = m.vertices[i + 1], d = m.vertices[i + 3];
                area +=
                    std::hypot(b.x - a.x, b.y - a.y, b.z - a.z) * std::hypot(d.x - a.x, d.y - a.y, d.z - a.z);
            }
            CHECK(size_t(area) == faces);
        });
        test("save roundtrip overwrite corruption and transactional load", [] {
            World a(42), b(42);
            auto path =
                std::filesystem::temp_directory_path() /
                ("vc-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                 ".vced");
            a.edit({-19, 400, 9000000000LL}, 3);
            a.edit({1, 2, 3}, 0);
            a.save(path);
            b.load(path);
            CHECK(b.block({-19, 400, 9000000000LL}) == 3);
            CHECK(b.block({1, 2, 3}) == 0);
            a.edit({1, 2, 3}, 2);
            a.save(path);
            b.load(path);
            CHECK(b.block({1, 2, 3}) == 2);
            // An abandoned partial snapshot must not shadow the last committed file.
            auto partial = path;
            partial += ".tmp";
            std::ofstream(partial) << "VCED";
            b.load(path);
            CHECK(b.block({1, 2, 3}) == 2);
            std::filesystem::remove(partial);
            {
                std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
                f.seekp(25);
                f.put('X');
            }
            bool failed = false;
            try {
                b.load(path);
            } catch (...) {
                failed = true;
            }
            CHECK(failed);
            CHECK(b.block({1, 2, 3}) == 2);
            std::filesystem::remove(path);
            auto backup = path;
            backup += ".bak";
            std::filesystem::remove(backup);
        });
        test("ray hit edit and AABB", [] {
            World w;
            int h = w.height(0, 0);
            Hit hit;
            CHECK(raycast(w, {.5, double(h + 10), .5}, {0, -1, 0}, 20, hit));
            CHECK((hit.block == Pos{0, h, 0}));
            CHECK((hit.previous == Pos{0, h + 1, 0}));
            w.edit(hit.block, 0);
            CHECK(raycast(w, {.5, double(h + 10), .5}, {0, -1, 0}, 20, hit));
            CHECK(hit.block.y == h - 1);
            CHECK(!raycast(w, {0, 500, 0}, {0, 0, 0}, 10, hit));
            CHECK(collides(w, {0, double(h - 2), 0}, {1, double(h - 1), 1}));
            CHECK(!collides(w, {0, 500, 0}, {1, 501, 1}));
        });
        test("far ring bounded and source aligned", [] {
            World w;
            auto m = surface_ring(w, 33536, 16768, 1048);
            CHECK(m.vertices.size() < 20000);
            for (auto v : m.vertices)
                CHECK(v.y == w.height(int64_t(v.x), int64_t(v.z)) + 1 ||
                      v.y == w.height(int64_t(v.x), int64_t(v.z)) + 1 - 64);
        });
        test("gameplay surface rings share full edges at every resolution join", [] {
            const World world(567);
            const Pos origin{-112, 160, -240};
            constexpr int width = 48, extent = 8192;
            auto mesh = terrain_horizon(world, origin, width, extent);
            CHECK(mesh.bytes() < 8 * 1024 * 1024);
            CHECK(mesh.indices.size() / 3 < 50000);
            using Point = std::array<int, 3>;
            std::map<std::pair<Point, Point>, int> edges;
            auto point = [&](uint32_t index) {
                CHECK(index < mesh.vertices.size());
                const auto &vertex = mesh.vertices[index];
                return Point{int(vertex.x), int(vertex.y), int(vertex.z)};
            };
            auto on_surface = [&](Point p) {
                return p[1] == world.height(origin.x + p[0], origin.z + p[2]) + 1 - origin.y;
            };
            for (size_t i = 0; i < mesh.indices.size(); i += 3) {
                std::array vertices{point(mesh.indices[i]), point(mesh.indices[i + 1]),
                                    point(mesh.indices[i + 2])};
                if (!on_surface(vertices[0]) || !on_surface(vertices[1]) || !on_surface(vertices[2]))
                    continue; // The near-edge skirt uses vertices below the generated surface.
                auto [a, b, c] = vertices;
                CHECK((b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]) > 0);
                for (size_t j = 0; j < vertices.size(); ++j) {
                    auto [start, end] = std::minmax(vertices[j], vertices[(j + 1) % vertices.size()]);
                    ++edges[{start, end}];
                }
            }
            auto square_boundary = [](Point start, Point end, int half_width) {
                return (start[0] == end[0] && std::abs(start[0]) == half_width &&
                        std::abs(start[2]) <= half_width && std::abs(end[2]) <= half_width) ||
                       (start[2] == end[2] && std::abs(start[2]) == half_width &&
                        std::abs(start[0]) <= half_width && std::abs(end[0]) <= half_width);
            };
            for (const auto &[edge, count] : edges) {
                auto [start, end] = edge;
                const bool boundary =
                    square_boundary(start, end, width) || square_boundary(start, end, extent);
                CHECK(count == (boundary ? 1 : 2));
            }
        });
        test("priority scheduler and bounded admission", [] {
            Scheduler s(1, 2);
            std::promise<void> started, release;
            auto gate = release.get_future().share();
            std::vector<int> order;
            std::mutex mutex;
            CHECK(s.submit(0, [&] {
                started.set_value();
                gate.wait();
            }));
            started.get_future().wait();
            CHECK(s.submit(10, [&] {
                std::lock_guard l(mutex);
                order.push_back(10);
            }));
            CHECK(s.submit(1, [&] {
                std::lock_guard l(mutex);
                order.push_back(1);
            }));
            CHECK(!s.submit(0, [] {}));
            release.set_value();
            auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            for (;;) {
                {
                    std::lock_guard l(mutex);
                    if (order.size() == 2) {
                        CHECK(order[0] == 1);
                        break;
                    }
                }
                CHECK(std::chrono::steady_clock::now() < until);
                std::this_thread::yield();
            }
        });
        test("stream cancellation admission and cache caps", [] {
            World w;
            Streamer s(w, 2, 100000, 4);
            for (int i = 0; i < 4; ++i)
                CHECK(s.request({i * 16, 64, 0}));
            CHECK(!s.request({100, 64, 0}));
            s.teleport();
            drain(s);
            CHECK(s.stats().cancelled == 4);
            CHECK(s.stats().resident == 0);
            for (int batch = 0; batch < 4; ++batch) {
                for (int i = 0; i < 4; ++i)
                    s.request({i * 16, 64, batch * 16});
                drain(s);
                CHECK(s.stats().resident_bytes <= 100000);
            }
            CHECK(s.stats().peak_bytes <= 100000);
            CHECK(s.stats().completed == s.stats().submitted);
        });
        test("stream deduplication edit invalidation and chunk size switch", [] {
            World w;
            Streamer s(w, 2, 1024 * 1024, 4);
            CHECK(s.request({0, 1000, 0}));
            CHECK(s.request({0, 1000, 0}));
            CHECK(s.stats().submitted == 1);
            drain(s);
            CHECK(s.contains({0, 1000, 0}));
            CHECK(s.stats().resident_bytes == 0);
            w.edit({0, 1000, 0}, 3);
            s.poll(1024 * 1024);
            CHECK(s.stats().resident == 0);
            CHECK(s.request({0, 1000, 0}));
            drain(s);
            CHECK(s.stats().resident_bytes > 0);
            CHECK(s.request({0, 1000, 0}, 32));
            drain(s);
            CHECK(s.stats().submitted == 3);
        });
        test("empty mesh metadata and upload quotas stay bounded", [] {
            World w;
            Streamer s(w, 1, 1024, 4);
            for (int x = 0; x < 8; ++x) {
                CHECK(s.request({x * 16, 1000, 0}));
                drain(s);
                CHECK(s.stats().resident <= 1);
            }
            CHECK(s.stats().evicted == 7);
            w.edit({0, 1000, 0}, 3);
            CHECK(s.request({0, 1000, 0}));
            auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (s.stats().pending) {
                CHECK(s.poll(0) == 0);
                CHECK(std::chrono::steady_clock::now() < until);
                std::this_thread::yield();
            }
            CHECK(s.stats().resident == 0);
            CHECK(s.stats().rejected > 0);
        });
        std::cout << passed << " tests passed\n";
        return 0;
    } catch (...) {
        return 1;
    }
}
