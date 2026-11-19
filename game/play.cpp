#include "play.hpp"
#include "core/player.hpp"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <sstream>

using namespace vc;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace {
constexpr int chunk_side = 16;
constexpr double reach = 8;
constexpr size_t mesh_budget = 32 * 1024 * 1024;

struct View {
    Pos center;
    uint64_t revision;
    Mesh mesh;
};

Pos view_center(const Camera &camera) {
    auto align = [](double v) { return floor_div(int64_t(std::floor(v)), chunk_side) * chunk_side; };
    return {align(camera.x), align(camera.y), align(camera.z)};
}

View build_view(const World &world, Pos center, uint64_t revision, int radius) {
    View view{center, revision, {}};
    for (int z = -radius; z < radius; ++z)
        for (int x = -radius; x < radius; ++x)
            for (int y = -2; y < 3; ++y) {
                if (world.revision() != revision)
                    return {center, revision, {}};
                Pos start{center.x + x * chunk_side, center.y + y * chunk_side, center.z + z * chunk_side};
                auto chunk = generate(world, start, chunk_side);
                append(view.mesh, greedy_mesh(chunk, [&](Pos p) { return world.block(p); }, center));
                if (view.mesh.bytes() > mesh_budget)
                    throw std::runtime_error("local terrain exceeds mesh budget; reduce --radius");
            }
    return view;
}

bool inside(Pos p, Pos center, int radius, int margin = 0) {
    const auto span = radius * chunk_side;
    return p.x >= center.x - span + margin && p.x < center.x + span - margin &&
           p.z >= center.z - span + margin && p.z < center.z + span - margin &&
           p.y >= center.y - 2 * chunk_side + margin && p.y < center.y + 3 * chunk_side - margin;
}
} // namespace

int play(World &world, Renderer &renderer, const std::filesystem::path &save, int radius, int frames,
         const std::string &capture) {
    auto *window = renderer.window();
    Player player;
    player.respawn(world);
    Camera camera{player.feet[0], player.feet[1] + eye_height, player.feet[2], 0, -.25f};
    Pos rendered_center = view_center(camera);
    uint64_t rendered_revision = world.revision();
    auto initial = build_view(world, rendered_center, rendered_revision, radius);
    size_t mesh_bytes = initial.mesh.bytes();
    renderer.upload(initial.mesh);
    initial.mesh = {};
    std::future<View> terrain;
    std::future<void> writer;
    uint64_t saved_revision = world.revision();
    bool save_requested = false;
    bool captured = true;
    bool mouse_ready = false;
    bool jump_down = false, mine_down = false, place_down = false, save_down = false, tab_down = false;
    bool reset_down = false;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    std::cout << "VibeCraft Beta V2 | seed " << world.seed() << " | " << renderer.device_name() << " | world "
              << save.string() << '\n';
    auto previous = Clock::now();
    auto sample_start = previous;
    int sampled_frames = 0;
    double old_x = 0, old_y = 0;
    for (int frame = 0; !frames || frame < frames;) {
        glfwPollEvents();
        auto key = [&](int code) { return glfwGetKey(window, code) == GLFW_PRESS; };
        if (glfwWindowShouldClose(window) || key(GLFW_KEY_ESCAPE))
            break;
        auto now = Clock::now();
        double dt = std::min(.1, std::chrono::duration<double>(now - previous).count());
        previous = now;
        auto pressed = [](bool down, bool &was_down) {
            bool edge = down && !was_down;
            was_down = down;
            return edge;
        };
        if (pressed(key(GLFW_KEY_TAB), tab_down)) {
            captured = !captured;
            mouse_ready = false;
            glfwSetInputMode(window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        }
        bool focused = glfwGetWindowAttrib(window, GLFW_FOCUSED);
        if (focused && captured) {
            double x, y;
            glfwGetCursorPos(window, &x, &y);
            if (mouse_ready) {
                camera.yaw -= float(x - old_x) * .0025f;
                camera.pitch -= float(y - old_y) * .0025f;
            }
            mouse_ready = true;
            old_x = x;
            old_y = y;
        } else {
            mouse_ready = false;
        }
        const bool active = focused && captured;
        if (active)
            camera.yaw += float((key(GLFW_KEY_LEFT) - key(GLFW_KEY_RIGHT)) * dt * 1.5);
        camera.yaw = std::remainder(camera.yaw, 2 * std::numbers::pi_v<float>);
        if (active)
            camera.pitch = std::clamp(
                camera.pitch + float((key(GLFW_KEY_UP) - key(GLFW_KEY_DOWN)) * dt * 1.5), -1.55f, 1.55f);
        if (pressed(key(GLFW_KEY_R), reset_down))
            player.respawn(world);
        double forward = active ? key(GLFW_KEY_W) - key(GLFW_KEY_S) : 0;
        double right = active ? key(GLFW_KEY_D) - key(GLFW_KEY_A) : 0;
        double speed = (key(GLFW_KEY_LEFT_SHIFT) ? 7. : 4.5) / std::max(1., std::hypot(forward, right));
        double vx = speed * (std::sin(camera.yaw) * forward - std::cos(camera.yaw) * right);
        double vz = speed * (std::cos(camera.yaw) * forward + std::sin(camera.yaw) * right);
        auto next = player;
        next.move(world, vx, vz, pressed(active && key(GLFW_KEY_SPACE), jump_down), dt);
        auto [nx, ny, nz] = next.feet;
        if (inside({int64_t(std::floor(nx)), int64_t(std::floor(ny)), int64_t(std::floor(nz))},
                   rendered_center, radius, 2))
            player = next;
        camera.x = player.feet[0];
        camera.y = player.feet[1] + eye_height;
        camera.z = player.feet[2];

        Hit hit{};
        double cp = std::cos(camera.pitch);
        bool selected =
            raycast(world, {camera.x, camera.y, camera.z},
                    {std::sin(camera.yaw) * cp, std::sin(camera.pitch), std::cos(camera.yaw) * cp}, reach,
                    hit) &&
            inside(hit.block, rendered_center, radius);
        bool mine = pressed(active && (key(GLFW_KEY_E) || glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)),
                            mine_down);
        bool place = pressed(
            active && (key(GLFW_KEY_Q) || glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT)), place_down);
        if (selected && mine)
            world.edit(hit.block, 0);
        else if (selected && place && inside(hit.previous, rendered_center, radius) &&
                 !player.overlaps(hit.previous) && !world.block(hit.previous))
            world.edit(hit.previous, 1);

        // Keep at most one worker result. Reject meshes built across any intervening edit.
        auto center = view_center(camera);
        if (terrain.valid() && terrain.wait_for(0s) == std::future_status::ready) {
            auto view = terrain.get();
            if (view.revision == world.revision() &&
                (view.center == center || view.center == rendered_center)) {
                renderer.upload(view.mesh);
                mesh_bytes = view.mesh.bytes();
                rendered_center = view.center;
                rendered_revision = view.revision;
            }
        }
        if (!terrain.valid() && (center != rendered_center || rendered_revision != world.revision()))
            terrain = std::async(std::launch::async, build_view, std::cref(world), center, world.revision(),
                                 radius);

        if (pressed(key(GLFW_KEY_F5), save_down))
            save_requested = true;
        if (writer.valid() && writer.wait_for(0s) == std::future_status::ready) {
            writer.get();
            std::cout << "Saved " << save.string() << '\n';
        }
        if (save_requested && !writer.valid()) {
            save_requested = false;
            saved_revision = world.revision();
            writer = std::async(std::launch::async, [&] { world.save(save); });
        }

        Camera local = camera;
        local.x -= rendered_center.x;
        local.y -= rendered_center.y;
        local.z -= rendered_center.z;
        if (renderer.draw(local, selected ? Reticle::target : Reticle::idle, float(radius * chunk_side))) {
            ++frame;
            ++sampled_frames;
        }
        if (now - sample_start >= 500ms) {
            std::ostringstream title;
            double elapsed = std::chrono::duration<double>(now - sample_start).count();
            title << "VibeCraft Beta V2 | " << std::fixed << std::setprecision(1) << sampled_frames / elapsed
                  << " FPS | " << (sampled_frames ? elapsed * 1000. / sampled_frames : 0.) << " ms | XYZ "
                  << camera.x << "/" << camera.y << "/" << camera.z << " | seed " << world.seed()
                  << " | bricks " << 20 * radius * radius << " | mesh " << (mesh_bytes / 1024)
                  << " KiB | jobs " << int(terrain.valid());
            if (selected)
                title << " | target " << hit.block.x << "/" << hit.block.y << "/" << hit.block.z;
            glfwSetWindowTitle(window, title.str().c_str());
            sampled_frames = 0;
            sample_start = now;
        }
    }
    if (writer.valid())
        writer.get();
    if (saved_revision != world.revision() || !std::filesystem::exists(save))
        world.save(save);
    if (!capture.empty())
        renderer.screenshot(capture);
    if (terrain.valid())
        terrain.get();
    std::cout << "Validation errors: " << renderer.validation_errors() << '\n';
    return renderer.validation_errors() ? 2 : 0;
}
