#pragma once

#include <VkBootstrap.h>
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <GLFW/glfw3.h>

#include <caldera/structs.hpp>
#include <vector>

namespace sponza {
class Context {
  public:
    Context();
    ~Context() noexcept;

    [[nodiscard]] bool closing() noexcept;
    static void poll_events() noexcept;

  private:
    struct WindowDeleter {
        void operator()(GLFWwindow*) const noexcept;
    };

    vkb::Instance m_instance;

    std::unique_ptr<GLFWwindow, WindowDeleter> m_window;
    vk::SurfaceKHR m_surface;

    vkb::PhysicalDevice m_physicalDevice;
    vkb::Device m_device;
    VmaAllocator m_allocator;

    vk::Extent2D m_extent;
    caldera::FamilyNumbers m_families;

    vkb::Swapchain m_swapchain;
    std::vector<vk::Image> m_swapchainImages;
    std::vector<vk::ImageView> m_swapchainImageViews;

    friend class Resources;
    friend class Renderer;
    friend class Pipeline;
};
} // namespace sponza