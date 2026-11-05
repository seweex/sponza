#include "pipeline_builder.hpp"
#include "utility.hpp"

#include <algorithm>
#include <utility>

namespace {
constexpr vk::ColorComponentFlags color_write_all = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                                    vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

[[nodiscard]] bool has_stage(std::vector<vk::ShaderStageFlagBits> const &stages, vk::ShaderStageFlagBits const stage) {
    return std::ranges::find(stages, stage) != stages.end();
}
} // namespace

namespace sponza::detail {
PipelineBuilder &PipelineBuilder::layout(vk::PipelineLayout const layout) noexcept {
    m_layout = layout;
    return *this;
}

PipelineBuilder &PipelineBuilder::cache(vk::PipelineCache const cache) noexcept {
    m_cache = cache;
    return *this;
}

PipelineBuilder &PipelineBuilder::shader(vk::ShaderStageFlagBits const stage, vk::ShaderModule const module,
                                         std::string entry) {
    m_shaders.push_back(ShaderStage{stage, module, std::move(entry)});
    return *this;
}

PipelineBuilder &PipelineBuilder::vertex_binding(uint32_t const binding, uint32_t const stride,
                                                 vk::VertexInputRate const rate) {
    m_bindings.emplace_back(binding, stride, rate);
    return *this;
}

PipelineBuilder &PipelineBuilder::vertex_attribute(uint32_t const location, uint32_t const binding,
                                                   vk::Format const format, uint32_t const offset) {
    m_attributes.emplace_back(location, binding, format, offset);
    return *this;
}

PipelineBuilder &PipelineBuilder::topology(vk::PrimitiveTopology const topology, bool const primitiveRestart) noexcept {
    m_topology = topology;
    m_primitiveRestart = primitiveRestart;
    return *this;
}

PipelineBuilder &PipelineBuilder::polygon_mode(vk::PolygonMode const mode) noexcept {
    m_polygonMode = mode;
    return *this;
}

PipelineBuilder &PipelineBuilder::cull_mode(vk::CullModeFlags const cull, vk::FrontFace const front) noexcept {
    m_cullMode = cull;
    m_frontFace = front;
    return *this;
}

PipelineBuilder &PipelineBuilder::line_width(float const width) noexcept {
    m_lineWidth = width;
    return *this;
}

PipelineBuilder &PipelineBuilder::depth_clamp(bool const enable) noexcept {
    m_depthClamp = enable;
    return *this;
}

PipelineBuilder &PipelineBuilder::depth_test(bool const write, vk::CompareOp const op) noexcept {
    m_depthTest = true;
    m_depthWrite = write;
    m_depthCompare = op;
    return *this;
}

PipelineBuilder &PipelineBuilder::samples(vk::SampleCountFlagBits const count) noexcept {
    m_samples = count;
    return *this;
}

PipelineBuilder &PipelineBuilder::dynamic_state(vk::DynamicState const state) {
    m_dynamicStates.push_back(state);
    return *this;
}

PipelineBuilder &PipelineBuilder::color_attachment(vk::Format const format,
                                                   std::optional<vk::PipelineColorBlendAttachmentState> const blend) {
    m_colorFormats.push_back(format);
    m_blendStates.push_back(blend.value_or(opaque_blend()));
    return *this;
}

PipelineBuilder &PipelineBuilder::depth_attachment(vk::Format const format) noexcept {
    m_depthFormat = format;
    return *this;
}

PipelineBuilder &PipelineBuilder::stencil_attachment(vk::Format const format) noexcept {
    m_stencilFormat = format;
    return *this;
}

vk::PipelineColorBlendAttachmentState PipelineBuilder::opaque_blend() noexcept {
    vk::PipelineColorBlendAttachmentState state{};
    state.blendEnable = vk::False;
    state.colorWriteMask = color_write_all;
    return state;
}

vk::PipelineColorBlendAttachmentState PipelineBuilder::alpha_blend() noexcept {
    vk::PipelineColorBlendAttachmentState state{};
    state.blendEnable = vk::True;
    state.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    state.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    state.colorBlendOp = vk::BlendOp::eAdd;
    state.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    state.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    state.alphaBlendOp = vk::BlendOp::eAdd;
    state.colorWriteMask = color_write_all;
    return state;
}

vk::PipelineColorBlendAttachmentState PipelineBuilder::additive_blend() noexcept {
    vk::PipelineColorBlendAttachmentState state{};
    state.blendEnable = vk::True;
    state.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    state.dstColorBlendFactor = vk::BlendFactor::eOne;
    state.colorBlendOp = vk::BlendOp::eAdd;
    state.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    state.dstAlphaBlendFactor = vk::BlendFactor::eOne;
    state.alphaBlendOp = vk::BlendOp::eAdd;
    state.colorWriteMask = color_write_all;
    return state;
}

vk::Pipeline PipelineBuilder::build_graphics(vk::Device const device) const {
    validate(static_cast<bool>(m_layout), "pipeline layout is not set");
    validate(!m_shaders.empty(), "graphics pipeline has no shader stages");

    std::vector<vk::ShaderStageFlagBits> stages;
    for (auto const &shader : m_shaders)
        stages.push_back(shader.stage);

    validate(!has_stage(stages, vk::ShaderStageFlagBits::eCompute), "compute stage in a graphics pipeline");
    validate(has_stage(stages, vk::ShaderStageFlagBits::eVertex) ||
                 has_stage(stages, vk::ShaderStageFlagBits::eMeshEXT),
             "graphics pipeline requires a vertex or a mesh stage");

    auto const stageInfos = make_stage_infos();

    vk::PipelineVertexInputStateCreateInfo const vertexInput{{},
                                                             static_cast<uint32_t>(m_bindings.size()),
                                                             m_bindings.data(),
                                                             static_cast<uint32_t>(m_attributes.size()),
                                                             m_attributes.data()};

    vk::PipelineInputAssemblyStateCreateInfo const inputAssembly{
        {}, m_topology, m_primitiveRestart ? vk::True : vk::False};

    vk::PipelineViewportStateCreateInfo const viewport{{}, 1, nullptr, 1, nullptr};

    vk::PipelineRasterizationStateCreateInfo rasterization{};
    rasterization.depthClampEnable = m_depthClamp ? vk::True : vk::False;
    rasterization.rasterizerDiscardEnable = vk::False;
    rasterization.polygonMode = m_polygonMode;
    rasterization.cullMode = m_cullMode;
    rasterization.frontFace = m_frontFace;
    rasterization.depthBiasEnable = vk::False;
    rasterization.lineWidth = m_lineWidth;

    vk::PipelineMultisampleStateCreateInfo multisample{};
    multisample.rasterizationSamples = m_samples;

    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable = m_depthTest ? vk::True : vk::False;
    depthStencil.depthWriteEnable = m_depthWrite ? vk::True : vk::False;
    depthStencil.depthCompareOp = m_depthCompare;

    vk::PipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.attachmentCount = static_cast<uint32_t>(m_blendStates.size());
    colorBlend.pAttachments = m_blendStates.data();

    std::vector dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    for (auto const state : m_dynamicStates)
        if (std::ranges::find(dynamicStates, state) == dynamicStates.end())
            dynamicStates.push_back(state);

    vk::PipelineDynamicStateCreateInfo const dynamic{
        {}, static_cast<uint32_t>(dynamicStates.size()), dynamicStates.data()};

    vk::StructureChain const createInfo{vk::GraphicsPipelineCreateInfo{{},
                                                                       static_cast<uint32_t>(stageInfos.size()),
                                                                       stageInfos.data(),
                                                                       &vertexInput,
                                                                       &inputAssembly,
                                                                       nullptr,
                                                                       &viewport,
                                                                       &rasterization,
                                                                       &multisample,
                                                                       &depthStencil,
                                                                       &colorBlend,
                                                                       &dynamic,
                                                                       m_layout,
                                                                       nullptr},
                                        vk::PipelineRenderingCreateInfo{0, static_cast<uint32_t>(m_colorFormats.size()),
                                                                        m_colorFormats.data(), m_depthFormat,
                                                                        m_stencilFormat}};

    vk::Pipeline pipeline;
    validate(device.createGraphicsPipelines(m_cache, 1, &createInfo.get<>(), nullptr, &pipeline),
             "failed to create graphics pipeline");
    return pipeline;
}

vk::Pipeline PipelineBuilder::build_compute(vk::Device const device) const {
    validate(static_cast<bool>(m_layout), "pipeline layout is not set");
    validate(m_shaders.size() == 1 && m_shaders.front().stage == vk::ShaderStageFlagBits::eCompute,
             "compute pipeline requires exactly one compute stage");

    auto const stageInfos = make_stage_infos();
    vk::ComputePipelineCreateInfo const createInfo{{}, stageInfos.front(), m_layout};

    vk::Pipeline pipeline;
    validate(device.createComputePipelines(m_cache, 1, &createInfo, nullptr, &pipeline),
             "failed to create compute pipeline");
    return pipeline;
}

std::vector<vk::PipelineShaderStageCreateInfo> PipelineBuilder::make_stage_infos() const {
    std::vector<vk::PipelineShaderStageCreateInfo> infos;
    infos.reserve(m_shaders.size());

    for (auto const &shader : m_shaders)
        infos.emplace_back(vk::PipelineShaderStageCreateFlags{}, shader.stage, shader.module, shader.entry.c_str());

    return infos;
}
} // namespace sponza::detail
