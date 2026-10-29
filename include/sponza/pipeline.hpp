#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>
#include <expected>
#include <filesystem>
#include <span>
#include <type_traits>
#include <vector>

#include <sponza/vulkan_context.hpp>
#include <vulkan/vulkan.hpp>

namespace sponza {

namespace blend_presets {
[[nodiscard]] constexpr vk::PipelineColorBlendAttachmentState disabled() noexcept {
    vk::PipelineColorBlendAttachmentState state{};
    state.blendEnable = vk::False;
    state.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    return state;
}

[[nodiscard]] constexpr vk::PipelineColorBlendAttachmentState alpha() noexcept {
    vk::PipelineColorBlendAttachmentState state{};
    state.blendEnable = vk::True;
    state.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    state.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    state.colorBlendOp = vk::BlendOp::eAdd;
    state.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    state.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    state.alphaBlendOp = vk::BlendOp::eAdd;
    state.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    return state;
}

[[nodiscard]] constexpr vk::PipelineColorBlendAttachmentState additive() noexcept {
    vk::PipelineColorBlendAttachmentState state{};
    state.blendEnable = vk::True;
    state.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    state.dstColorBlendFactor = vk::BlendFactor::eOne;
    state.colorBlendOp = vk::BlendOp::eAdd;
    state.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    state.dstAlphaBlendFactor = vk::BlendFactor::eOne;
    state.alphaBlendOp = vk::BlendOp::eAdd;
    state.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    return state;
}
} // namespace blend_presets

class ShaderModule {
  public:
    ShaderModule() noexcept = default;
    ~ShaderModule() noexcept;

    ShaderModule(ShaderModule const &) = delete;
    ShaderModule &operator=(ShaderModule const &) = delete;

    ShaderModule(ShaderModule &&other) noexcept;
    ShaderModule &operator=(ShaderModule &&other) noexcept;

    [[nodiscard]] vk::Result init(vk::Device device, std::span<uint32_t const> spirvCode);
    [[nodiscard]] vk::Result init(vk::Device device, std::filesystem::path const &filePath);

    [[nodiscard]] vk::ShaderModule get() const noexcept { return m_module; }
    [[nodiscard]] operator vk::ShaderModule() const noexcept { return m_module; }

  private:
    vk::Device m_device;
    vk::ShaderModule m_module;
};

class SpecializationInfoBuilder {
  public:
    template <typename T>
        requires std::is_trivially_copyable_v<T>
    SpecializationInfoBuilder &add_constant(uint32_t const constantID, T const &value) {
        auto const offset = static_cast<uint32_t>(m_data.size());
        m_entries.emplace_back(constantID, offset, sizeof(T));

        m_data.resize(offset + sizeof(T));
        std::memcpy(m_data.data() + offset, &value, sizeof(T));
        return *this;
    }

    [[nodiscard]] vk::SpecializationInfo build_info() const noexcept {
        return vk::SpecializationInfo{
            static_cast<uint32_t>(m_entries.size()),
            m_entries.data(),
            m_data.size(),
            m_data.data()};
    }

    [[nodiscard]] bool empty() const noexcept { return m_entries.empty(); }

  private:
    std::vector<vk::SpecializationMapEntry> m_entries;
    std::vector<std::byte> m_data;
};

class VertexInputBuilder {
  public:
    VertexInputBuilder &add_binding(uint32_t const binding, uint32_t const stride,
                                    vk::VertexInputRate const inputRate = vk::VertexInputRate::eVertex) {
        m_bindings.emplace_back(binding, stride, inputRate);
        return *this;
    }

    VertexInputBuilder &add_attribute(uint32_t const location, uint32_t const binding,
                                      vk::Format const format, uint32_t const offset) {
        m_attributes.emplace_back(location, binding, format, offset);
        return *this;
    }

    [[nodiscard]] std::span<vk::VertexInputBindingDescription const> get_bindings() const noexcept {
        return m_bindings;
    }

    [[nodiscard]] std::span<vk::VertexInputAttributeDescription const> get_attributes() const noexcept {
        return m_attributes;
    }

  private:
    std::vector<vk::VertexInputBindingDescription> m_bindings;
    std::vector<vk::VertexInputAttributeDescription> m_attributes;
};

class DescriptorSetLayoutBuilder {
  public:
    DescriptorSetLayoutBuilder &add_binding(uint32_t const binding, vk::DescriptorType const type,
                                            uint32_t const count, vk::ShaderStageFlags const stages,
                                            vk::DescriptorBindingFlags const flags = {});

    DescriptorSetLayoutBuilder &set_flags(vk::DescriptorSetLayoutCreateFlags const flags) noexcept;
    DescriptorSetLayoutBuilder &clear() noexcept;

    [[nodiscard]] std::expected<vk::DescriptorSetLayout, vk::Result> build(vk::Device device) const;

  private:
    std::vector<vk::DescriptorSetLayoutBinding> m_bindings;
    std::vector<vk::DescriptorBindingFlags> m_bindingFlags;
    vk::DescriptorSetLayoutCreateFlags m_createFlags{};
    bool m_hasBindingFlags = false;
};

class PipelineLayoutBuilder {
  public:
    PipelineLayoutBuilder &add_set_layout(vk::DescriptorSetLayout layout);
    PipelineLayoutBuilder &add_set_layouts(std::span<vk::DescriptorSetLayout const> layouts);
    PipelineLayoutBuilder &add_push_constant_range(vk::ShaderStageFlags stages, uint32_t offset, uint32_t size);

    template <typename T>
        requires std::is_trivially_copyable_v<T>
    PipelineLayoutBuilder &add_push_constant(vk::ShaderStageFlags const stages, uint32_t const offset = 0) {
        return add_push_constant_range(stages, offset, static_cast<uint32_t>(sizeof(T)));
    }

    PipelineLayoutBuilder &clear() noexcept;

    [[nodiscard]] std::expected<vk::PipelineLayout, vk::Result> build(vk::Device device) const;

  private:
    std::vector<vk::DescriptorSetLayout> m_setLayouts;
    std::vector<vk::PushConstantRange> m_pushConstantRanges;
};

namespace detail {
struct StageRecord {
    vk::ShaderStageFlagBits stage;
    vk::ShaderModule module;
    char const *entryPoint = "main";
    SpecializationInfoBuilder specialization;
};
} // namespace detail

class GraphicsPipelineBuilder {
  public:
    GraphicsPipelineBuilder() noexcept;

    GraphicsPipelineBuilder &clear() noexcept;

    // Shaders
    GraphicsPipelineBuilder &add_shader(vk::ShaderStageFlagBits stage, vk::ShaderModule module,
                                        char const *entryPoint = "main",
                                        SpecializationInfoBuilder specialization = {});

    GraphicsPipelineBuilder &set_vertex_shader(vk::ShaderModule module, char const *entryPoint = "main",
                                               SpecializationInfoBuilder specialization = {});
    GraphicsPipelineBuilder &set_fragment_shader(vk::ShaderModule module, char const *entryPoint = "main",
                                                 SpecializationInfoBuilder specialization = {});

    // Vertex Input & Input Assembly
    GraphicsPipelineBuilder &set_vertex_input(VertexInputBuilder const &vertexInput);
    GraphicsPipelineBuilder &add_vertex_binding(uint32_t binding, uint32_t stride,
                                                vk::VertexInputRate inputRate = vk::VertexInputRate::eVertex);
    GraphicsPipelineBuilder &add_vertex_attribute(uint32_t location, uint32_t binding,
                                                  vk::Format format, uint32_t offset);

    GraphicsPipelineBuilder &set_topology(vk::PrimitiveTopology topology, bool primitiveRestart = false) noexcept;
    GraphicsPipelineBuilder &set_patch_control_points(uint32_t controlPoints) noexcept;

    // Rasterization & Multisampling
    GraphicsPipelineBuilder &set_polygon_mode(vk::PolygonMode mode, float lineWidth = 1.0f) noexcept;
    GraphicsPipelineBuilder &set_cull_mode(vk::CullModeFlags cullMode,
                                           vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise) noexcept;
    GraphicsPipelineBuilder &set_depth_bias(float constantFactor, float clamp, float slopeFactor) noexcept;
    GraphicsPipelineBuilder &set_depth_clamp(bool enable) noexcept;
    GraphicsPipelineBuilder &set_multisampling(vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1,
                                               bool sampleShading = false, float minSampleShading = 1.0f) noexcept;

    // Depth & Stencil
    GraphicsPipelineBuilder &enable_depth_test(bool writeEnable = true,
                                               vk::CompareOp compareOp = vk::CompareOp::eLessOrEqual) noexcept;
    GraphicsPipelineBuilder &disable_depth_test() noexcept;
    GraphicsPipelineBuilder &set_depth_bounds(float minBounds, float maxBounds) noexcept;
    GraphicsPipelineBuilder &enable_stencil_test(vk::StencilOpState const &front,
                                                 vk::StencilOpState const &back) noexcept;

    // Dynamic Rendering Formats & Color Blending
    GraphicsPipelineBuilder &add_color_attachment(
        vk::Format format,
        vk::PipelineColorBlendAttachmentState const &blendState = blend_presets::disabled());
    GraphicsPipelineBuilder &set_depth_format(vk::Format format) noexcept;
    GraphicsPipelineBuilder &set_stencil_format(vk::Format format) noexcept;
    GraphicsPipelineBuilder &set_view_mask(uint32_t viewMask) noexcept;

    // Dynamic States & Layout
    GraphicsPipelineBuilder &add_dynamic_state(vk::DynamicState state);
    GraphicsPipelineBuilder &set_layout(vk::PipelineLayout layout) noexcept;
    GraphicsPipelineBuilder &set_flags(vk::PipelineCreateFlags flags) noexcept;

    [[nodiscard]] std::expected<vk::Pipeline, vk::Result> build(vk::Device device,
                                                                vk::PipelineCache cache = nullptr) const;

  private:
    std::vector<detail::StageRecord> m_stages;

    std::vector<vk::VertexInputBindingDescription> m_vertexBindings;
    std::vector<vk::VertexInputAttributeDescription> m_vertexAttributes;

    vk::PipelineInputAssemblyStateCreateInfo m_inputAssembly;
    vk::PipelineTessellationStateCreateInfo m_tessellation;
    vk::PipelineRasterizationStateCreateInfo m_rasterizer;
    vk::PipelineMultisampleStateCreateInfo m_multisampling;
    vk::PipelineDepthStencilStateCreateInfo m_depthStencil;

    std::vector<vk::Format> m_colorAttachmentFormats;
    std::vector<vk::PipelineColorBlendAttachmentState> m_colorBlendAttachments;
    vk::Format m_depthFormat = vk::Format::eUndefined;
    vk::Format m_stencilFormat = vk::Format::eUndefined;
    uint32_t m_viewMask = 0;

    std::vector<vk::DynamicState> m_dynamicStates;
    vk::PipelineLayout m_layout;
    vk::PipelineCreateFlags m_flags{};
};

class ComputePipelineBuilder {
  public:
    ComputePipelineBuilder() noexcept = default;

    ComputePipelineBuilder &clear() noexcept;
    ComputePipelineBuilder &set_shader(vk::ShaderModule module, char const *entryPoint = "main",
                                       SpecializationInfoBuilder specialization = {});
    ComputePipelineBuilder &set_layout(vk::PipelineLayout layout) noexcept;
    ComputePipelineBuilder &set_flags(vk::PipelineCreateFlags flags) noexcept;

    [[nodiscard]] std::expected<vk::Pipeline, vk::Result> build(vk::Device device,
                                                                vk::PipelineCache cache = nullptr) const;

  private:
    detail::StageRecord m_stage{};
    vk::PipelineLayout m_layout;
    vk::PipelineCreateFlags m_flags{};
};

class PipelineBuilder {
  public:
    PipelineBuilder() noexcept = default;
    ~PipelineBuilder() noexcept;

    PipelineBuilder(PipelineBuilder const &) = delete;
    PipelineBuilder &operator=(PipelineBuilder const &) = delete;

    PipelineBuilder(PipelineBuilder &&other) noexcept;
    PipelineBuilder &operator=(PipelineBuilder &&other) noexcept;

    [[nodiscard]] vk::Result init(VulkanContext const &context, bool enableCache = true);
    [[nodiscard]] vk::Result init(vk::Device device, bool enableCache = true);

    [[nodiscard]] static DescriptorSetLayoutBuilder descriptor_set_layout() noexcept {
        return DescriptorSetLayoutBuilder{};
    }
    [[nodiscard]] static PipelineLayoutBuilder pipeline_layout() noexcept {
        return PipelineLayoutBuilder{};
    }
    [[nodiscard]] static GraphicsPipelineBuilder graphics() noexcept {
        return GraphicsPipelineBuilder{};
    }
    [[nodiscard]] static ComputePipelineBuilder compute() noexcept {
        return ComputePipelineBuilder{};
    }

    [[nodiscard]] std::expected<vk::DescriptorSetLayout, vk::Result>
    build(DescriptorSetLayoutBuilder const &builder) const;

    [[nodiscard]] std::expected<vk::PipelineLayout, vk::Result>
    build(PipelineLayoutBuilder const &builder) const;

    [[nodiscard]] std::expected<vk::Pipeline, vk::Result>
    build(GraphicsPipelineBuilder const &builder) const;

    [[nodiscard]] std::expected<vk::Pipeline, vk::Result>
    build(ComputePipelineBuilder const &builder) const;

    [[nodiscard]] vk::PipelineCache get_cache() const noexcept { return m_cache; }
    [[nodiscard]] vk::Device get_device() const noexcept { return m_device; }

  private:
    vk::Device m_device;
    vk::PipelineCache m_cache;
};

} // namespace sponza