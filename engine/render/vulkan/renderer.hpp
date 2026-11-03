#pragma once
#include "core/world.hpp"
#include <memory>
#include <string>
struct GLFWwindow;
namespace vc {
struct Camera {
    double x = 70, y = 180, z = 70;
    float yaw = -2.35f, pitch = -.45f;
};
struct RenderConfig {
    int width = 1280, height = 720;
    bool headless = false, validation = false;
};
class Renderer {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit Renderer(RenderConfig config);
    ~Renderer();
    void upload(const Mesh &mesh);
    bool draw(const Camera &camera);
    void screenshot(const std::string &path);
    GLFWwindow *window() const;
    double gpu_ms() const;
    unsigned validation_errors() const;
    std::string device_name() const;
};
} // namespace vc
