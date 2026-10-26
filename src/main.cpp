
#include <iostream>

#include <sponza/vulkan_context.hpp>
#include <sponza/window.hpp>

int main() {
    sponza::VulkanContext context;
    sponza::Window window;

    if (window.init() < vk::Result::eSuccess) {
        std::cerr << "Failed to create a window" << std::endl;
        return -1;
    }

    if (context.init(window.get_handle(), window.get_extent()) < vk::Result::eSuccess) {
        std::cerr << "Failed to init a context" << std::endl;
        return -1;
    }

    while (!window.should_close()) {
        window.poll_events();
    }

    return 0;
}