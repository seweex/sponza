#pragma once

#include <vulkan/vulkan.hpp>

#include <vector>

namespace sponza::detail {
class DescriptorBuilder {
  public:
    DescriptorBuilder() = default;

    DescriptorBuilder &binding(uint32_t binding, vk::DescriptorType type, vk::ShaderStageFlags stages,
                               uint32_t count = 1, vk::DescriptorBindingFlags flags = {});
    DescriptorBuilder &bindless(uint32_t binding, vk::DescriptorType type, vk::ShaderStageFlags stages,
                                uint32_t maxCount);

    DescriptorBuilder &write_buffer(uint32_t binding, vk::DescriptorType type, vk::Buffer buffer,
                                    vk::DeviceSize offset = 0, vk::DeviceSize range = vk::WholeSize,
                                    uint32_t arrayElement = 0);
    DescriptorBuilder &write_image(uint32_t binding, vk::DescriptorType type, vk::ImageView view,
                                   vk::ImageLayout layout, vk::Sampler sampler = {}, uint32_t arrayElement = 0);

    [[nodiscard]] vk::DescriptorSetLayout build_layout(vk::Device device) const;
    [[nodiscard]] vk::DescriptorPool build_pool(vk::Device device, uint32_t maxSets,
                                                vk::DescriptorPoolCreateFlags flags = {}) const;

    [[nodiscard]] static vk::DescriptorSet allocate_set(vk::Device device, vk::DescriptorPool pool,
                                                        vk::DescriptorSetLayout layout, uint32_t variableCount = 0);
    void update_set(vk::Device device, vk::DescriptorSet set) const;

  private:
    struct BufferWrite {
        uint32_t binding;
        vk::DescriptorType type;
        uint32_t arrayElement;
        vk::DescriptorBufferInfo info;
    };

    struct ImageWrite {
        uint32_t binding;
        vk::DescriptorType type;
        uint32_t arrayElement;
        vk::DescriptorImageInfo info;
    };

    [[nodiscard]] bool has_binding_flag(vk::DescriptorBindingFlagBits flag) const noexcept;

    std::vector<vk::DescriptorSetLayoutBinding> m_bindings;
    std::vector<vk::DescriptorBindingFlags> m_bindingFlags;

    std::vector<BufferWrite> m_bufferWrites;
    std::vector<ImageWrite> m_imageWrites;
};
} // namespace sponza::detail
