#pragma once

#include <GLFW/glfw3.h>
#include <expected>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

#include <sponza/vulkan_context.hpp>

namespace sponza {
class ResourceFactory {
  public:
    using Image = std::pair<vk::Image, VmaAllocationInfo>;
    using Buffer = std::pair<vk::Buffer, VmaAllocationInfo>;

    ResourceFactory() noexcept;
    ~ResourceFactory() noexcept;

    [[nodiscard]] vk::Result init(VulkanContext const& context);

    [[nodiscard]] std::expected<Image, vk::Result> make_image(vk::ImageCreateInfo const &info);
    [[nodiscard]] std::expected<Buffer, vk::Result> make_buffer(vk::BufferCreateInfo const &info, bool mapping);

    [[nodiscard]] VmaAllocator get_allocator() const noexcept;

  private:
    VmaAllocator m_allocator;

    std::vector<std::pair<VkImage, VmaAllocation>> m_images;
    std::vector<std::pair<VkBuffer, VmaAllocation>> m_buffers;
};
} // namespace sponza