#include <sponza/resource_factory.hpp>

namespace {
[[nodiscard]] std::expected<VmaAllocator, vk::Result> make_allocator(sponza::VulkanContext const &context) {
    VmaAllocatorCreateInfo const createInfo{.physicalDevice = context.get_physical_device(),
                                            .device = context.get_device(),
                                            .instance = context.get_instance(),
                                            .vulkanApiVersion = VK_MAKE_API_VERSION(0, 1, 3, 0)};

    VmaAllocator allocator;

    if (auto const rCode = vmaCreateAllocator(&createInfo, &allocator); rCode < VK_SUCCESS)
        return std::unexpected{static_cast<vk::Result>(rCode)};

    return allocator;
}
} // namespace

namespace sponza {

ResourceFactory::ResourceFactory() noexcept = default;
ResourceFactory::~ResourceFactory() noexcept {
    for (auto const &[resource, allocation] : m_images)
        vmaDestroyImage(m_allocator, resource, allocation);

    for (auto const &[resource, allocation] : m_buffers)
        vmaDestroyBuffer(m_allocator, resource, allocation);

    vmaDestroyAllocator(m_allocator);
}
vk::Result ResourceFactory::init(VulkanContext const &context) {
    auto allocator = make_allocator(context);

    if (!allocator.has_value())
        return allocator.error();

    m_allocator = *allocator;
    return vk::Result::eSuccess;
}

std::expected<ResourceFactory::Image, vk::Result> ResourceFactory::make_image(vk::ImageCreateInfo const &info) {
    auto const vkInfo = static_cast<VkImageCreateInfo>(info);

    VmaAllocationCreateInfo constexpr allocationInfo{.flags = VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT,
                                                     .usage = VMA_MEMORY_USAGE_AUTO};

    VkImage image;
    VmaAllocation allocation;
    VmaAllocationInfo allocatedInfo;

    auto const rCode = vmaCreateImage(m_allocator, &vkInfo, &allocationInfo, &image, &allocation, &allocatedInfo);
    if (rCode < VK_SUCCESS)
        return std::unexpected{static_cast<vk::Result>(rCode)};

    m_images.emplace_back(image, allocation);
    return Image{image, allocatedInfo};
}

std::expected<ResourceFactory::Buffer, vk::Result> ResourceFactory::make_buffer(vk::BufferCreateInfo const &info,
                                                                                bool const mapped) {
    auto const vkInfo = static_cast<VkBufferCreateInfo>(info);

    VmaAllocationCreateInfo allocationInfo{.flags = VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT,
                                           .usage = VMA_MEMORY_USAGE_AUTO};

    if (mapped) {
        allocationInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocationInfo.requiredFlags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }

    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo allocatedInfo;

    auto const rCode = vmaCreateBuffer(m_allocator, &vkInfo, &allocationInfo, &buffer, &allocation, &allocatedInfo);
    if (rCode < VK_SUCCESS)
        return std::unexpected{static_cast<vk::Result>(rCode)};

    m_buffers.emplace_back(buffer, allocation);
    return Buffer{buffer, allocatedInfo};
}

VmaAllocator ResourceFactory::get_allocator() const noexcept { return m_allocator; }
} // namespace sponza