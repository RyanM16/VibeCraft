#include "core/telemetry.hpp"
#include "core/world.hpp"
#include "play.hpp"
#include "render/vulkan/renderer.hpp"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
using namespace vc;
using Clock = std::chrono::steady_clock;
static double percentile(std::vector<double> v, double p) {
    std::sort(v.begin(), v.end());
    return v[size_t((v.size() - 1) * p)];
}
static Mesh scene(const World &w, int radius) {
    Mesh mesh;
    for (int z = -radius; z < radius; ++z)
        for (int x = -radius; x < radius; ++x) {
            int top = 0;
            for (int dz = 0; dz <= 16; dz += 8)
                for (int dx = 0; dx <= 16; dx += 8)
                    top = std::max(top, w.height(x * 16 + dx, z * 16 + dz));
            for (int y = 0; y <= top / 16; ++y) {
                auto chunk = generate(w, {x * 16, y * 16, z * 16}, 16);
                append(mesh, greedy_mesh(chunk, [&](Pos p) { return w.block(p); }));
            }
        }
    // Surface-only LOD experiments. Shared sample positions, no volumetric mid tier or transition morphing
    // yet.
    int inner = radius * 16;
    for (auto [extent, step] : {std::pair{256, 16}, {1024, 32}, {4096, 128}, {16768, 524}, {33536, 1048}}) {
        append(mesh, surface_ring(w, extent, inner, step));
        inner = extent;
    }
    return mesh;
}
int main(int argc, char **argv) {
    try {
        RenderConfig config;
        int frames = 0, radius = 3, horizon = 2048;
        bool horizon_selected = false;
        uint64_t seed = 42;
        bool benchmark = false, caves = false, cave_start = false, world_selected = false;
        std::string capture, output = "artifacts/render", scenario = "stationary",
                             save = "worlds/testbed.vced";
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--help") {
                std::cout
                    << "VibeCraft Beta V2.2\n"
                       "--seed N --world PATH.vced --radius 2..4 --horizon local|128|512 --validation "
                       "--frames N\n"
                       "--caves enables connected caves for new worlds; --cave-start spawns in a tunnel.\n"
                       "WASD walk, mouse/arrows look, Space jump, Shift sprint, left/right click "
                       "or E/Q mine/place, R respawn, Tab release mouse, F5 save, Esc quit.\n"
                       "Benchmark: --headless --frames N --scenario stationary|flight|yaw|edits "
                       "--width N --height N --radius 1..16 --output PATH_PREFIX "
                       "--screenshot PATH.ppm\n";
                return 0;
            }
            if (a == "--headless")
                config.headless = true;
            else if (a == "--caves")
                caves = true;
            else if (a == "--cave-start")
                caves = cave_start = true;
            else if (a == "--validation")
                config.validation = true;
            else {
                if (i + 1 == argc)
                    throw std::runtime_error("missing value for " + a);
                std::string v = argv[++i];
                if (a == "--frames")
                    frames = std::stoi(v);
                else if (a == "--width")
                    config.width = std::stoi(v);
                else if (a == "--height")
                    config.height = std::stoi(v);
                else if (a == "--radius")
                    radius = std::stoi(v);
                else if (a == "--horizon") {
                    horizon_selected = true;
                    if (v == "local")
                        horizon = 0;
                    else if (v == "128")
                        horizon = 2048;
                    else if (v == "512")
                        horizon = 8192;
                    else
                        throw std::runtime_error("--horizon requires local, 128, or 512");
                } else if (a == "--screenshot")
                    capture = v;
                else if (a == "--output")
                    output = v;
                else if (a == "--scenario") {
                    scenario = v;
                    benchmark = true;
                } else if (a == "--seed") {
                    auto parsed = std::from_chars(v.data(), v.data() + v.size(), seed);
                    if (parsed.ec != std::errc{} || parsed.ptr != v.data() + v.size())
                        throw std::runtime_error("--seed requires an unsigned 64-bit integer");
                } else if (a == "--world") {
                    save = v;
                    world_selected = true;
                } else
                    throw std::runtime_error("unknown option " + a);
            }
        }
        if (radius < 1 || radius > 16 || frames < 0 || (config.headless && frames == 0) ||
            (scenario != "stationary" && scenario != "flight" && scenario != "yaw" && scenario != "edits"))
            throw std::runtime_error("invalid options (headless requires --frames; radius 1..16; scenarios "
                                     "stationary/flight/yaw/edits)");
        benchmark = benchmark || config.headless;
        if (benchmark && horizon_selected)
            throw std::runtime_error("--horizon applies to gameplay; omit --headless and --scenario");
        if (!benchmark && (radius < 2 || radius > 4))
            throw std::runtime_error("playable radius must be 2..4 chunks");
        if (benchmark && cave_start)
            throw std::runtime_error("--cave-start requires windowed gameplay");
        if (caves && !world_selected)
            save = "worlds/caves.vced";
        World world(seed, caves);
        if (std::filesystem::exists(save))
            world.load(save);
        Renderer renderer(config);
        if (!benchmark)
            return play(world, renderer, save, radius, horizon, frames, capture, cave_start);
        auto start = Clock::now();
        auto mesh = scene(world, radius);
        renderer.upload(mesh);
        std::cout << "Device: " << renderer.device_name() << "\nScene: " << mesh.vertices.size()
                  << " vertices, " << mesh.indices.size() / 3 << " triangles, " << mesh.bytes()
                  << " geometry bytes; visual extent 33536 m (surface approximation)\nScene build ms: "
                  << std::chrono::duration<double, std::milli>(Clock::now() - start).count() << '\n';
        Camera camera;
        camera.x = 45;
        camera.z = 45;
        camera.y = world.height(0, 0) + 40;
        camera.yaw = -2.35f;
        camera.pitch = -.5f;
        auto base = camera;
        auto previous = Clock::now();
        std::vector<double> times, gpu_times;
        auto path = std::filesystem::path(output);
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());
        std::ofstream csv(output + ".csv");
        if (!csv)
            throw std::runtime_error("cannot write render telemetry");
        csv << "frame,frame_ms,gpu_opaque_ms\n";
        bool edit_down = false, save_down = false;
        int rendered = 0;
        while (!frames || rendered < frames) {
            auto now = Clock::now();
            double dt = std::min(.1, std::chrono::duration<double>(now - previous).count());
            previous = now;
            auto window = renderer.window();
            if (window) {
                glfwPollEvents();
                if (glfwWindowShouldClose(window) || glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                    break;
                auto key = [&](int k) { return glfwGetKey(window, k) == GLFW_PRESS; };
                float speed = key(GLFW_KEY_LEFT_SHIFT) ? 80.f : 15.f;
                float turn = float(dt) * 1.5f;
                camera.yaw += (key(GLFW_KEY_LEFT) - key(GLFW_KEY_RIGHT)) * turn;
                camera.pitch =
                    std::clamp(camera.pitch + (key(GLFW_KEY_UP) - key(GLFW_KEY_DOWN)) * turn, -1.5f, 1.5f);
                int fwd = key(GLFW_KEY_W) - key(GLFW_KEY_S), right = key(GLFW_KEY_D) - key(GLFW_KEY_A);
                camera.x += (std::sin(camera.yaw) * fwd - std::cos(camera.yaw) * right) * dt * speed;
                camera.z += (std::cos(camera.yaw) * fwd + std::sin(camera.yaw) * right) * dt * speed;
                camera.y += (key(GLFW_KEY_SPACE) - key(GLFW_KEY_LEFT_CONTROL)) * dt * speed;
                bool editing = key(GLFW_KEY_E) || key(GLFW_KEY_Q);
                if (editing && !edit_down) {
                    Hit hit;
                    std::array<double, 3> d = {std::sin(camera.yaw) * std::cos(camera.pitch),
                                               std::sin(camera.pitch),
                                               std::cos(camera.yaw) * std::cos(camera.pitch)};
                    if (raycast(world, {camera.x, camera.y, camera.z}, d, 12, hit)) {
                        world.edit(key(GLFW_KEY_E) ? hit.block : hit.previous, key(GLFW_KEY_E) ? 0 : 1);
                        mesh = scene(world, radius);
                        renderer.upload(mesh);
                    }
                }
                edit_down = editing;
                bool saving = key(GLFW_KEY_F5);
                if (saving && !save_down) {
                    world.save(save);
                    std::cout << "Saved " << world.edit_count() << " edits\n";
                }
                save_down = saving;
            }
            if (scenario == "flight") {
                double a = rendered * .025;
                camera.x = std::sin(a) * 60;
                camera.z = std::cos(a) * 60;
                camera.y = base.y + std::sin(a * .7) * 15;
                camera.yaw = float(a + 3.14159265);
                camera.pitch = -.45f;
            } else if (scenario == "yaw")
                camera.yaw = base.yaw + float(rendered) * .035f;
            if (scenario == "edits" && rendered % 15 == 0) {
                auto height = world.height(0, 0);
                world.edit({0, height, 0}, (rendered / 15) % 2 ? 1 : 0);
                mesh = scene(world, radius);
                renderer.upload(mesh);
            }
            auto frame_start = now;
            if (!renderer.draw(camera))
                continue;
            double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - frame_start).count();
            double gpu = renderer.gpu_ms();
            csv << rendered << ',' << elapsed << ',' << gpu << '\n';
            if (rendered >= 5 && gpu >= 0) {
                times.push_back(elapsed);
                gpu_times.push_back(gpu);
            }
            ++rendered;
            if (window && rendered % 30 == 0) {
                auto title = std::string("VibeCraft test bed | GPU ") + std::to_string(gpu) +
                             " ms | E/Q edit | F5 save | WASD/Space/Ctrl fly";
                glfwSetWindowTitle(window, title.c_str());
            }
        }
        if (!capture.empty())
            renderer.screenshot(capture);
        if (world.edit_count())
            world.save(save);
        std::ofstream json(output + ".json");
        json << "{\n  \"device\": \"" << renderer.device_name()
             << "\",\n  \"software_or_hardware\": \"inspect device name; cloud uses software Vulkan\",\n  "
                "\"frames\": "
             << rendered << ", \"warmup_frames\": 5, \"width\": " << config.width
             << ", \"height\": " << config.height << ",\n  \"scenario\": \"" << scenario
             << "\", \"preset\": \"flat-color-fog-no-shadows\",\n  \"presentation\": \""
             << (config.headless ? "offscreen" : "FIFO-vsync")
             << "\",\n  \"visual_extent_blocks\": 33536, \"near_radius_chunks\": " << radius
             << ",\n  \"triangles\": " << mesh.indices.size() / 3 << ", \"geometry_bytes\": " << mesh.bytes()
             << ",\n  \"validation_errors\": " << renderer.validation_errors();
        if (!times.empty()) {
            auto sorted = times;
            std::sort(sorted.begin(), sorted.end(), std::greater<>());
            size_t slow = std::max<size_t>(1, size_t(std::ceil(sorted.size() * .01)));
            double slow_mean = std::accumulate(sorted.begin(), sorted.begin() + slow, 0.) / slow;
            json << ",\n  \"cpu_frame_ms\": {\"median\": " << percentile(times, .5)
                 << ", \"p95\": " << percentile(times, .95) << ", \"p99\": " << percentile(times, .99)
                 << "},\n  \"gpu_opaque_ms\": {\"median\": " << percentile(gpu_times, .5)
                 << ", \"p99\": " << percentile(gpu_times, .99)
                 << "},\n  \"one_percent_low_fps\": " << 1000 / slow_mean;
        }
        json << ",\n  \"peak_rss_bytes\": " << peak_rss_bytes() << "\n}\n";
        json.flush();
        csv.flush();
        if (!json || !csv)
            throw std::runtime_error("telemetry write failed");
        std::cout << "Rendered " << rendered << " frames; validation errors " << renderer.validation_errors()
                  << "; telemetry " << output << ".json\n";
        return renderer.validation_errors() ? 2 : 0;
    } catch (const std::exception &e) {
        std::cerr << "VibeCraft: " << e.what() << '\n';
        return 1;
    }
}
