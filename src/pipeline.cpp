#include <fstream>
#include <utility>

#include <sponza/pipeline.hpp>
#include <sponza/utility.hpp>

namespace {
[[nodiscard]] std::expected<std::vector<uint32_t>, vk::Result> read_spirv_file(std::filesystem::path const &filePath) {
    std::ifstream file{filePath, std::ios::ate | std::ios::binary};
    if (!file.is_open())
        return std::unexpected{vk::Result::eErrorInitializationFailed};

    auto const fileSize = file.tellg();
    if (fileSize <= 0 || (static_cast<size_t>(fileSize) % sizeof(uint32_t)) != 0)
        return std::unexpected{vk::Result::eErrorInvalidShaderNV};

    std::vector<uint32_t> buffer(static_cast<size_t>(fileSize) / sizeof(uint32_t));
    file.seekg(0);

    if (!file.read(reinterpret_cast<char *>(buffer.data()), fileSize))
        return std::unexpected{vk::Result::eErrorInitializationFailed};

    return buffer;
}
} // namespace

namespace sponza {

// ShaderModule

ShaderModule::~ShaderModule() noexcept {
    if (m_device && m_module)
        m_device.destroyShaderModule(m_module);
}

ShaderModule::ShaderModule(ShaderModule &&other) noexcept
    : m_device(std::exchange(other.m_device, nullptr)), m_module(std::exchange(other.m_module, nullptr)) {}

ShaderModule &ShaderModule::operator=(ShaderModule &&other) noexcept {
    if (this != &other) {
        if (m_device && m_module)
            m_device.destroyShaderModule(m_module);

        m_device = std::exchange(other.m_device, nullptr);
        m_module = std::exchange(other.m_module, nullptr);
    }
    return *this;
}

vk::Result ShaderModule::init(vk::Device const device, std::span<uint32_t const> const spirvCode) {
    assert(device);
    assert(!spirvCode.empty());

    if (m_device && m_module)
        m_device.destroyShaderModule(m_module);

    m_device = device;

    vk::ShaderModuleCreateInfo createInfo{};
    createInfo.codeSize = spirvCode.size_bytes();
    createInfo.pCode = spirvCode.data();

    SPONZA_CHECK_RCODE((m_device.createShaderModule(&createInfo, nullptr, &m_module)), vk::Result);
    return vk::Result::eSuccess;
}

vk::Result ShaderModule::init(vk::Device const device, std::filesystem::path const &filePath) {
    auto const spirv = read_spirv_file(filePath);
    if (!spirv.has_value())
        return spirv.error();

    return init(device, *spirv);
}

// DescriptorSetLayoutBuilder

DescriptorSetLayoutBuilder &DescriptorSetLayoutBuilder::add_binding(uint32_t const binding,
                                                                    vk::DescriptorType const type, uint32_t const count,
                                                                    vk::ShaderStageFlags const stages,
                                                                    vk::DescriptorBindingFlags const flags) {
    m_bindings.emplace_back(binding, type, count, stages, nullptr);
    m_bindingFlags.emplace_back(flags);

    if (flags != vk::DescriptorBindingFlags{})
        m_hasBindingFlags = true;

    return *this;
}

DescriptorSetLayoutBuilder &
DescriptorSetLayoutBuilder::set_flags(vk::DescriptorSetLayoutCreateFlags const flags) noexcept {
    m_createFlags = flags;
    return *this;
}

DescriptorSetLayoutBuilder &DescriptorSetLayoutBuilder::clear() noexcept {
    m_bindings.clear();
    m_bindingFlags.clear();
    m_createFlags = {};
    m_hasBindingFlags = false;
    return *this;
}

std::expected<vk::DescriptorSetLayout, vk::Result> DescriptorSetLayoutBuilder::build(vk::Device const device) const {
    assert(device);
    assert(m_bindings.size() == m_bindingFlags.size());

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.flags = m_createFlags;
    layoutInfo.setBindings(m_bindings);

    vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
    flagsInfo.setBindingFlags(m_bindingFlags);

    vk::StructureChain<vk::DescriptorSetLayoutCreateInfo, vk::DescriptorSetLayoutBindingFlagsCreateInfo> chain{
        layoutInfo, flagsInfo};

    if (!m_hasBindingFlags)
        chain.unlink<vk::DescriptorSetLayoutBindingFlagsCreateInfo>();

    vk::DescriptorSetLayout layout;
    SPONZA_CHECK_RCODE((device.createDescriptorSetLayout(&chain.get<>(), nullptr, &layout)), std::unexpected);
    return layout;
}

// PipelineLayoutBuilder

PipelineLayoutBuilder &PipelineLayoutBuilder::add_set_layout(vk::DescriptorSetLayout const layout) {
    assert(layout);
    m_setLayouts.emplace_back(layout);
    return *this;
}

PipelineLayoutBuilder &PipelineLayoutBuilder::add_set_layouts(std::span<vk::DescriptorSetLayout const> const layouts) {
    m_setLayouts.insert(m_setLayouts.end(), layouts.begin(), layouts.end());
    return *this;
}

PipelineLayoutBuilder &PipelineLayoutBuilder::add_push_constant_range(vk::ShaderStageFlags const stages,
                                                                      uint32_t const offset, uint32_t const size) {
    assert(size > 0);
    m_pushConstantRanges.emplace_back(stages, offset, size);
    return *this;
}

PipelineLayoutBuilder &PipelineLayoutBuilder::clear() noexcept {
    m_setLayouts.clear();
    m_pushConstantRanges.clear();
    return *this;
}

std::expected<vk::PipelineLayout, vk::Result> PipelineLayoutBuilder::build(vk::Device const device) const {
    assert(device);

    vk::PipelineLayoutCreateInfo createInfo{};
    createInfo.setSetLayouts(m_setLayouts);
    createInfo.setPushConstantRanges(m_pushConstantRanges);

    vk::PipelineLayout layout;
    SPONZA_CHECK_RCODE((device.createPipelineLayout(&createInfo, nullptr, &layout)), std::unexpected);
    return layout;
}

// GraphicsPipelineBuilder

GraphicsPipelineBuilder::GraphicsPipelineBuilder() noexcept { clear(); }

GraphicsPipelineBuilder &GraphicsPipelineBuilder::clear() noexcept {
    m_stages.clear();
    m_vertexBindings.clear();
    m_vertexAttributes.clear();

    m_inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{};
    m_inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    m_inputAssembly.primitiveRestartEnable = vk::False;

    m_tessellation = vk::PipelineTessellationStateCreateInfo{};
    m_tessellation.patchControlPoints = 0;

    m_rasterizer = vk::PipelineRasterizationStateCreateInfo{};
    m_rasterizer.depthClampEnable = vk::False;
    m_rasterizer.rasterizerDiscardEnable = vk::False;
    m_rasterizer.polygonMode = vk::PolygonMode::eFill;
    m_rasterizer.cullMode = vk::CullModeFlagBits::eBack;
    m_rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
    m_rasterizer.depthBiasEnable = vk::False;
    m_rasterizer.lineWidth = 1.0f;

    m_multisampling = vk::PipelineMultisampleStateCreateInfo{};
    m_multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
    m_multisampling.sampleShadingEnable = vk::False;
    m_multisampling.minSampleShading = 1.0f;

    m_depthStencil = vk::PipelineDepthStencilStateCreateInfo{};
    m_depthStencil.depthTestEnable = vk::False;
    m_depthStencil.depthWriteEnable = vk::False;
    m_depthStencil.depthCompareOp = vk::CompareOp::eLessOrEqual;
    m_depthStencil.depthBoundsTestEnable = vk::False;
    m_depthStencil.stencilTestEnable = vk::False;

    m_colorAttachmentFormats.clear();
    m_colorBlendAttachments.clear();
    m_depthFormat = vk::Format::eUndefined;
    m_stencilFormat = vk::Format::eUndefined;
    m_viewMask = 0;

    m_dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    m_layout = nullptr;
    m_flags = {};

    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::add_shader(vk::ShaderStageFlagBits const stage,
                                                             vk::ShaderModule const module,
                                                             char const *const entryPoint,
                                                             SpecializationInfoBuilder specialization) {
    assert(module);
    assert(entryPoint != nullptr);

    m_stages.push_back(detail::StageRecord{
        .stage = stage, .module = module, .entryPoint = entryPoint, .specialization = std::move(specialization)});
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_vertex_shader(vk::ShaderModule const module,
                                                                    char const *const entryPoint,
                                                                    SpecializationInfoBuilder specialization) {
    return add_shader(vk::ShaderStageFlagBits::eVertex, module, entryPoint, std::move(specialization));
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_fragment_shader(vk::ShaderModule const module,
                                                                      char const *const entryPoint,
                                                                      SpecializationInfoBuilder specialization) {
    return add_shader(vk::ShaderStageFlagBits::eFragment, module, entryPoint, std::move(specialization));
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_vertex_input(VertexInputBuilder const &vertexInput) {
    auto const bindings = vertexInput.get_bindings();
    auto const attributes = vertexInput.get_attributes();

    m_vertexBindings.assign(bindings.begin(), bindings.end());
    m_vertexAttributes.assign(attributes.begin(), attributes.end());
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::add_vertex_binding(uint32_t const binding, uint32_t const stride,
                                                                     vk::VertexInputRate const inputRate) {
    m_vertexBindings.emplace_back(binding, stride, inputRate);
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::add_vertex_attribute(uint32_t const location, uint32_t const binding,
                                                                       vk::Format const format, uint32_t const offset) {
    m_vertexAttributes.emplace_back(location, binding, format, offset);
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_topology(vk::PrimitiveTopology const topology,
                                                               bool const primitiveRestart) noexcept {
    m_inputAssembly.topology = topology;
    m_inputAssembly.primitiveRestartEnable = primitiveRestart ? vk::True : vk::False;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_patch_control_points(uint32_t const controlPoints) noexcept {
    m_tessellation.patchControlPoints = controlPoints;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_polygon_mode(vk::PolygonMode const mode,
                                                                   float const lineWidth) noexcept {
    m_rasterizer.polygonMode = mode;
    m_rasterizer.lineWidth = lineWidth;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_cull_mode(vk::CullModeFlags const cullMode,
                                                                vk::FrontFace const frontFace) noexcept {
    m_rasterizer.cullMode = cullMode;
    m_rasterizer.frontFace = frontFace;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_depth_bias(float const constantFactor, float const clamp,
                                                                 float const slopeFactor) noexcept {
    m_rasterizer.depthBiasEnable = vk::True;
    m_rasterizer.depthBiasConstantFactor = constantFactor;
    m_rasterizer.depthBiasClamp = clamp;
    m_rasterizer.depthBiasSlopeFactor = slopeFactor;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_depth_clamp(bool const enable) noexcept {
    m_rasterizer.depthClampEnable = enable ? vk::True : vk::False;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_multisampling(vk::SampleCountFlagBits const samples,
                                                                    bool const sampleShading,
                                                                    float const minSampleShading) noexcept {
    m_multisampling.rasterizationSamples = samples;
    m_multisampling.sampleShadingEnable = sampleShading ? vk::True : vk::False;
    m_multisampling.minSampleShading = minSampleShading;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::enable_depth_test(bool const writeEnable,
                                                                    vk::CompareOp const compareOp) noexcept {
    m_depthStencil.depthTestEnable = vk::True;
    m_depthStencil.depthWriteEnable = writeEnable ? vk::True : vk::False;
    m_depthStencil.depthCompareOp = compareOp;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::disable_depth_test() noexcept {
    m_depthStencil.depthTestEnable = vk::False;
    m_depthStencil.depthWriteEnable = vk::False;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_depth_bounds(float const minBounds,
                                                                   float const maxBounds) noexcept {
    m_depthStencil.depthBoundsTestEnable = vk::True;
    m_depthStencil.minDepthBounds = minBounds;
    m_depthStencil.maxDepthBounds = maxBounds;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::enable_stencil_test(vk::StencilOpState const &front,
                                                                      vk::StencilOpState const &back) noexcept {
    m_depthStencil.stencilTestEnable = vk::True;
    m_depthStencil.front = front;
    m_depthStencil.back = back;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::add_color_attachment(vk::Format const format,
                                              vk::PipelineColorBlendAttachmentState const &blendState) {
    assert(format != vk::Format::eUndefined);
    m_colorAttachmentFormats.emplace_back(format);
    m_colorBlendAttachments.emplace_back(blendState);
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_depth_format(vk::Format const format) noexcept {
    m_depthFormat = format;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_stencil_format(vk::Format const format) noexcept {
    m_stencilFormat = format;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_view_mask(uint32_t const viewMask) noexcept {
    m_viewMask = viewMask;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::add_dynamic_state(vk::DynamicState const state) {
    for (auto const existing : m_dynamicStates) {
        if (existing == state)
            return *this;
    }
    m_dynamicStates.emplace_back(state);
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_layout(vk::PipelineLayout const layout) noexcept {
    assert(layout);
    m_layout = layout;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_flags(vk::PipelineCreateFlags const flags) noexcept {
    m_flags = flags;
    return *this;
}

std::expected<vk::Pipeline, vk::Result> GraphicsPipelineBuilder::build(vk::Device const device,
                                                                       vk::PipelineCache const cache) const {
    assert(device);
    assert(m_layout && "PipelineLayout must be provided before building GraphicsPipeline");
    assert(!m_stages.empty() && "GraphicsPipeline requires at least one shader stage");
    assert(m_colorAttachmentFormats.size() == m_colorBlendAttachments.size());

    std::vector<vk::SpecializationInfo> specInfos;
    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
    specInfos.reserve(m_stages.size());
    shaderStages.reserve(m_stages.size());

    for (auto const &record : m_stages) {
        vk::SpecializationInfo const *specPtr = nullptr;
        if (!record.specialization.empty()) {
            specInfos.emplace_back(record.specialization.build_info());
            specPtr = &specInfos.back();
        }

        shaderStages.emplace_back(vk::PipelineShaderStageCreateFlags{}, record.stage, record.module, record.entryPoint,
                                  specPtr);
    }

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.setVertexBindingDescriptions(m_vertexBindings);
    vertexInputInfo.setVertexAttributeDescriptions(m_vertexAttributes);

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.logicOpEnable = vk::False;
    colorBlending.logicOp = vk::LogicOp::eCopy;
    colorBlending.setAttachments(m_colorBlendAttachments);

    vk::PipelineDynamicStateCreateInfo dynamicStateInfo{};
    dynamicStateInfo.setDynamicStates(m_dynamicStates);

    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.flags = m_flags;
    pipelineInfo.setStages(shaderStages);
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &m_inputAssembly;
    pipelineInfo.pTessellationState = m_tessellation.patchControlPoints > 0 ? &m_tessellation : nullptr;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &m_rasterizer;
    pipelineInfo.pMultisampleState = &m_multisampling;
    pipelineInfo.pDepthStencilState = &m_depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicStateInfo;
    pipelineInfo.layout = m_layout;
    pipelineInfo.renderPass = nullptr;
    pipelineInfo.subpass = 0;

    vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.viewMask = m_viewMask;
    renderingInfo.setColorAttachmentFormats(m_colorAttachmentFormats);
    renderingInfo.depthAttachmentFormat = m_depthFormat;
    renderingInfo.stencilAttachmentFormat = m_stencilFormat;

    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> const createInfo{pipelineInfo,
                                                                                                         renderingInfo};

    vk::Pipeline pipeline;
    SPONZA_CHECK_RCODE((device.createGraphicsPipelines(cache, 1u, &createInfo.get<>(), nullptr, &pipeline)),
                       std::unexpected);
    return pipeline;
}

// ComputePipelineBuilder

ComputePipelineBuilder &ComputePipelineBuilder::clear() noexcept {
    m_stage = {};
    m_layout = nullptr;
    m_flags = {};
    return *this;
}

ComputePipelineBuilder &ComputePipelineBuilder::set_shader(vk::ShaderModule const module, char const *const entryPoint,
                                                           SpecializationInfoBuilder specialization) {
    assert(module);
    assert(entryPoint != nullptr);

    m_stage = detail::StageRecord{.stage = vk::ShaderStageFlagBits::eCompute,
                                  .module = module,
                                  .entryPoint = entryPoint,
                                  .specialization = std::move(specialization)};
    return *this;
}

ComputePipelineBuilder &ComputePipelineBuilder::set_layout(vk::PipelineLayout const layout) noexcept {
    assert(layout);
    m_layout = layout;
    return *this;
}

ComputePipelineBuilder &ComputePipelineBuilder::set_flags(vk::PipelineCreateFlags const flags) noexcept {
    m_flags = flags;
    return *this;
}

std::expected<vk::Pipeline, vk::Result> ComputePipelineBuilder::build(vk::Device const device,
                                                                      vk::PipelineCache const cache) const {
    assert(device);
    assert(m_stage.module && "Compute shader module must be set before building ComputePipeline");
    assert(m_layout && "PipelineLayout must be set before building ComputePipeline");

    vk::SpecializationInfo specInfo{};
    vk::SpecializationInfo const *specPtr = nullptr;
    if (!m_stage.specialization.empty()) {
        specInfo = m_stage.specialization.build_info();
        specPtr = &specInfo;
    }

    vk::PipelineShaderStageCreateInfo stageInfo{};
    stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    stageInfo.module = m_stage.module;
    stageInfo.pName = m_stage.entryPoint;
    stageInfo.pSpecializationInfo = specPtr;

    vk::ComputePipelineCreateInfo createInfo{};
    createInfo.flags = m_flags;
    createInfo.stage = stageInfo;
    createInfo.layout = m_layout;

    vk::Pipeline pipeline;
    SPONZA_CHECK_RCODE((device.createComputePipelines(cache, 1u, &createInfo, nullptr, &pipeline)), std::unexpected);
    return pipeline;
}

// PipelineBuilder

PipelineBuilder::~PipelineBuilder() noexcept {
    if (m_device && m_cache)
        m_device.destroyPipelineCache(m_cache);
}

PipelineBuilder::PipelineBuilder(PipelineBuilder &&other) noexcept
    : m_device(std::exchange(other.m_device, nullptr)), m_cache(std::exchange(other.m_cache, nullptr)) {}

PipelineBuilder &PipelineBuilder::operator=(PipelineBuilder &&other) noexcept {
    if (this != &other) {
        if (m_device && m_cache)
            m_device.destroyPipelineCache(m_cache);

        m_device = std::exchange(other.m_device, nullptr);
        m_cache = std::exchange(other.m_cache, nullptr);
    }
    return *this;
}

vk::Result PipelineBuilder::init(VulkanContext const &context, bool const enableCache) {
    return init(context.get_device(), enableCache);
}

vk::Result PipelineBuilder::init(vk::Device const device, bool const enableCache) {
    assert(device);

    if (m_device && m_cache) {
        m_device.destroyPipelineCache(m_cache);
        m_cache = nullptr;
    }

    m_device = device;

    if (enableCache) {
        vk::PipelineCacheCreateInfo const cacheInfo{};
        SPONZA_CHECK_RCODE((m_device.createPipelineCache(&cacheInfo, nullptr, &m_cache)), vk::Result);
    }

    return vk::Result::eSuccess;
}

std::expected<vk::DescriptorSetLayout, vk::Result>
PipelineBuilder::build(DescriptorSetLayoutBuilder const &builder) const {
    assert(m_device);
    return builder.build(m_device);
}

std::expected<vk::PipelineLayout, vk::Result> PipelineBuilder::build(PipelineLayoutBuilder const &builder) const {
    assert(m_device);
    return builder.build(m_device);
}

std::expected<vk::Pipeline, vk::Result> PipelineBuilder::build(GraphicsPipelineBuilder const &builder) const {
    assert(m_device);
    return builder.build(m_device, m_cache);
}

std::expected<vk::Pipeline, vk::Result> PipelineBuilder::build(ComputePipelineBuilder const &builder) const {
    assert(m_device);
    return builder.build(m_device, m_cache);
}

} // namespace sponza