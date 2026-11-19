#pragma once
#include "render/vulkan/renderer.hpp"

int play(vc::World &world, vc::Renderer &renderer, const std::filesystem::path &save, int radius, int horizon,
         int frames, const std::string &capture, bool start_in_cave);
