#pragma once

#include <vulkan/vulkan.hpp>

#include <optional>
#include <string>
#include <vector>

namespace sponza::detail {
class PipelineBuilder {
  public:
    PipelineBuilder() = default;

    PipelineBuilder &layout(vk::PipelineLayout layout) noexcept;
    PipelineBuilder &cache(vk::PipelineCache cache) noexcept;
    PipelineBuilder &shader(vk::ShaderStageFlagBits stage, vk::ShaderModule module, std::string entry = "main");

    PipelineBuilder &vertex_binding(uint32_t binding, uint32_t stride,
                                    vk::VertexInputRate rate = vk::VertexInputRate::eVertex);
    PipelineBuilder &vertex_attribute(uint32_t location, uint32_t binding, vk::Format format, uint32_t offset);

    PipelineBuilder &topology(vk::PrimitiveTopology topology, bool primitiveRestart = false) noexcept;
    PipelineBuilder &polygon_mode(vk::PolygonMode mode) noexcept;
    PipelineBuilder &cull_mode(vk::CullModeFlags cull, vk::FrontFace front = vk::FrontFace::eCounterClockwise) noexcept;
    PipelineBuilder &line_width(float width) noexcept;
    PipelineBuilder &depth_clamp(bool enable = true) noexcept;
    PipelineBuilder &depth_test(bool write = true, vk::CompareOp op = vk::CompareOp::eLess) noexcept;
    PipelineBuilder &samples(vk::SampleCountFlagBits count) noexcept;
    PipelineBuilder &dynamic_state(vk::DynamicState state);

    PipelineBuilder &color_attachment(vk::Format format,
                                      std::optional<vk::PipelineColorBlendAttachmentState> blend = std::nullopt);
    PipelineBuilder &depth_attachment(vk::Format format) noexcept;
    PipelineBuilder &stencil_attachment(vk::Format format) noexcept;

    [[nodiscard]] static vk::PipelineColorBlendAttachmentState opaque_blend() noexcept;
    [[nodiscard]] static vk::PipelineColorBlendAttachmentState alpha_blend() noexcept;
    [[nodiscard]] static vk::PipelineColorBlendAttachmentState additive_blend() noexcept;

    [[nodiscard]] vk::Pipeline build_graphics(vk::Device device) const;
    [[nodiscard]] vk::Pipeline build_compute(vk::Device device) const;

  private:
    struct ShaderStage {
        vk::ShaderStageFlagBits stage;
        vk::ShaderModule module;
        std::string entry;
    };

    [[nodiscard]] std::vector<vk::PipelineShaderStageCreateInfo> make_stage_infos() const;

    vk::PipelineLayout m_layout;
    vk::PipelineCache m_cache;
    std::vector<ShaderStage> m_shaders;

    std::vector<vk::VertexInputBindingDescription> m_bindings;
    std::vector<vk::VertexInputAttributeDescription> m_attributes;

    vk::PrimitiveTopology m_topology = vk::PrimitiveTopology::eTriangleList;
    bool m_primitiveRestart = false;

    vk::PolygonMode m_polygonMode = vk::PolygonMode::eFill;
    vk::CullModeFlags m_cullMode = vk::CullModeFlagBits::eNone;
    vk::FrontFace m_frontFace = vk::FrontFace::eCounterClockwise;
    float m_lineWidth = 1.0f;
    bool m_depthClamp = false;

    bool m_depthTest = false;
    bool m_depthWrite = false;
    vk::CompareOp m_depthCompare = vk::CompareOp::eLess;

    vk::SampleCountFlagBits m_samples = vk::SampleCountFlagBits::e1;

    std::vector<vk::Format> m_colorFormats;
    std::vector<vk::PipelineColorBlendAttachmentState> m_blendStates;
    vk::Format m_depthFormat = vk::Format::eUndefined;
    vk::Format m_stencilFormat = vk::Format::eUndefined;

    std::vector<vk::DynamicState> m_dynamicStates;
};
} // namespace sponza::detail
