#pragma once
#include "render/vulkan/renderer.hpp"

int play(vc::World &world, vc::Renderer &renderer, const std::filesystem::path &save, int radius, int frames,
         const std::string &capture);
