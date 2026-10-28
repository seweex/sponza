#pragma once

#include <VkBootstrap.h>
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>

#include <caldera/caldera.hpp>

namespace sponza {
class VulkanContext {
  public:
    VulkanContext() noexcept;
    ~VulkanContext() noexcept;

    [[nodiscard]] vk::Result init(GLFWwindow* window, vk::Extent2D extent);

    [[nodiscard]] vk::Instance get_instance() const noexcept;
    [[nodiscard]] vk::PhysicalDevice get_physical_device() const noexcept;
    [[nodiscard]] vk::Device get_device() const noexcept;

    [[nodiscard]] caldera::FamilyNumbers get_family_indices() const noexcept;
    [[nodiscard]] vk::Queue get_queue(uint32_t family) const noexcept;

    [[nodiscard]] std::span<vk::ImageView const> get_swapchain_image_views() const noexcept;

  private:
    vkb::Instance m_instance;
    vk::SurfaceKHR m_surface;

    vkb::PhysicalDevice m_physicalDevice;
    vkb::Device m_device;

    vkb::Swapchain m_swapchain;
    std::vector<vk::ImageView> m_swapchainImageViews;
};
} // namespace sponza