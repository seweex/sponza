#include "descriptor_builder.hpp"
#include "utility.hpp"

#include <algorithm>
#include <ranges>

namespace {
constexpr vk::DescriptorBindingFlags bindless_flags =
    vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eVariableDescriptorCount |
    vk::DescriptorBindingFlagBits::eUpdateAfterBind | vk::DescriptorBindingFlagBits::eUpdateUnusedWhilePending;

[[nodiscard]] bool has_flag(vk::DescriptorBindingFlags const flags, vk::DescriptorBindingFlagBits const flag) noexcept {
    return static_cast<bool>(flags & flag);
}

[[nodiscard]] bool is_dynamic(vk::DescriptorType const type) noexcept {
    return type == vk::DescriptorType::eUniformBufferDynamic || type == vk::DescriptorType::eStorageBufferDynamic;
}
} // namespace

namespace sponza::detail {
DescriptorBuilder &DescriptorBuilder::binding(uint32_t const binding, vk::DescriptorType const type,
                                              vk::ShaderStageFlags const stages, uint32_t const count,
                                              vk::DescriptorBindingFlags const flags) {
    m_bindings.emplace_back(binding, type, count, stages, nullptr);
    m_bindingFlags.push_back(flags);
    return *this;
}

DescriptorBuilder &DescriptorBuilder::bindless(uint32_t const binding, vk::DescriptorType const type,
                                               vk::ShaderStageFlags const stages, uint32_t const maxCount) {
    return this->binding(binding, type, stages, maxCount, bindless_flags);
}

DescriptorBuilder &DescriptorBuilder::write_buffer(uint32_t const binding, vk::DescriptorType const type,
                                                   vk::Buffer const buffer, vk::DeviceSize const offset,
                                                   vk::DeviceSize const range, uint32_t const arrayElement) {
    m_bufferWrites.push_back(BufferWrite{binding, type, arrayElement, vk::DescriptorBufferInfo{buffer, offset, range}});
    return *this;
}

DescriptorBuilder &DescriptorBuilder::write_image(uint32_t const binding, vk::DescriptorType const type,
                                                  vk::ImageView const view, vk::ImageLayout const layout,
                                                  vk::Sampler const sampler, uint32_t const arrayElement) {
    m_imageWrites.push_back(ImageWrite{binding, type, arrayElement, vk::DescriptorImageInfo{sampler, view, layout}});
    return *this;
}

vk::DescriptorSetLayout DescriptorBuilder::build_layout(vk::Device const device) const {
    validate(!m_bindings.empty(), "descriptor set layout has no bindings");

    size_t variableBindings = 0;

    for (size_t i = 0; i < m_bindings.size(); ++i) {
        auto const &current = m_bindings[i];
        auto const flags = m_bindingFlags[i];

        auto const duplicate = std::ranges::any_of(m_bindings | std::views::drop(i + 1),
                                                   [&](auto const &other) { return other.binding == current.binding; });
        validate(!duplicate, "duplicate descriptor binding");

        validate(!(is_dynamic(current.descriptorType) &&
                   (has_flag(flags, vk::DescriptorBindingFlagBits::eUpdateAfterBind) ||
                    has_flag(flags, vk::DescriptorBindingFlagBits::eVariableDescriptorCount))),
                 "dynamic buffer descriptors cannot be update-after-bind or variable-sized");

        if (has_flag(flags, vk::DescriptorBindingFlagBits::eVariableDescriptorCount)) {
            ++variableBindings;

            auto const notLast =
                std::ranges::any_of(m_bindings, [&](auto const &other) { return other.binding > current.binding; });
            validate(!notLast, "variable-sized binding must have the highest binding number");
        }
    }

    validate(variableBindings <= 1, "only one variable-sized binding is allowed per layout");

    vk::DescriptorSetLayoutCreateFlags layoutFlags{};
    if (has_binding_flag(vk::DescriptorBindingFlagBits::eUpdateAfterBind))
        layoutFlags |= vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;

    vk::StructureChain const createInfo{
        vk::DescriptorSetLayoutCreateInfo{layoutFlags, static_cast<uint32_t>(m_bindings.size()), m_bindings.data()},
        vk::DescriptorSetLayoutBindingFlagsCreateInfo{static_cast<uint32_t>(m_bindingFlags.size()),
                                                      m_bindingFlags.data()}};

    vk::DescriptorSetLayout layout;
    validate(device.createDescriptorSetLayout(&createInfo.get<>(), nullptr, &layout),
             "failed to create descriptor set layout");
    return layout;
}

vk::DescriptorPool DescriptorBuilder::build_pool(vk::Device const device, uint32_t const maxSets,
                                                 vk::DescriptorPoolCreateFlags flags) const {
    validate(!m_bindings.empty(), "descriptor pool has no bindings to size from");
    validate(maxSets > 0, "descriptor pool max sets is zero");

    if (has_binding_flag(vk::DescriptorBindingFlagBits::eUpdateAfterBind))
        flags |= vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;

    std::vector<vk::DescriptorPoolSize> poolSizes;
    for (auto const &binding : m_bindings) {
        auto const iter = std::ranges::find(poolSizes, binding.descriptorType, &vk::DescriptorPoolSize::type);

        if (iter != poolSizes.end())
            iter->descriptorCount += binding.descriptorCount * maxSets;
        else
            poolSizes.emplace_back(binding.descriptorType, binding.descriptorCount * maxSets);
    }

    vk::DescriptorPoolCreateInfo const createInfo{flags, maxSets, static_cast<uint32_t>(poolSizes.size()),
                                                  poolSizes.data()};

    vk::DescriptorPool pool;
    validate(device.createDescriptorPool(&createInfo, nullptr, &pool), "failed to create descriptor pool");
    return pool;
}

vk::DescriptorSet DescriptorBuilder::allocate_set(vk::Device const device, vk::DescriptorPool const pool,
                                                  vk::DescriptorSetLayout const layout, uint32_t const variableCount) {
    vk::StructureChain const allocateInfo{vk::DescriptorSetAllocateInfo{pool, 1, &layout},
                                          vk::DescriptorSetVariableDescriptorCountAllocateInfo{1, &variableCount}};

    vk::DescriptorSet set;
    validate(device.allocateDescriptorSets(&allocateInfo.get<>(), &set), "failed to allocate descriptor set");
    return set;
}

void DescriptorBuilder::update_set(vk::Device const device, vk::DescriptorSet const set) const {
    validate(static_cast<bool>(set), "descriptor set is not set");

    std::vector<vk::WriteDescriptorSet> writes;
    writes.reserve(m_bufferWrites.size() + m_imageWrites.size());

    for (auto const &write : m_bufferWrites)
        writes.emplace_back(set, write.binding, write.arrayElement, 1, write.type, nullptr, &write.info);

    for (auto const &write : m_imageWrites)
        writes.emplace_back(set, write.binding, write.arrayElement, 1, write.type, &write.info);

    device.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

bool DescriptorBuilder::has_binding_flag(vk::DescriptorBindingFlagBits const flag) const noexcept {
    return std::ranges::any_of(m_bindingFlags, [flag](auto const flags) { return has_flag(flags, flag); });
}
} // namespace sponza::detail
