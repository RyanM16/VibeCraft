#include "core/player.hpp"
#include "core/tasks.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
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
