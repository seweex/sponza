#include <caldera/caldera.hpp>
#include <sponza/pipeline.hpp>
#include <sponza/resources.hpp>

#include "pipeline_builder.hpp"
#include "utility.hpp"

#include <sponza/shaders/compiled/forward.frag.hpp>
#include <sponza/shaders/compiled/forward.vert.hpp>

namespace {
[[nodiscard]] vk::ShaderModule create_shader_module(vk::Device const device, std::span<uint32_t const> const code) {
    vk::ShaderModuleCreateInfo const createInfo{vk::ShaderModuleCreateFlags{}, code.size_bytes(), code.data()};

    vk::ShaderModule module;
    sponza::detail::validate(device.createShaderModule(&createInfo, nullptr, &module),
                             "failed to create shader module");
    return module;
}

[[nodiscard]] vk::PipelineLayout create_pipeline_layout(vk::Device const device,
                                                        std::span<vk::DescriptorSetLayout const> const layouts,
                                                        uint32_t const pcSize = 0,
                                                        vk::ShaderStageFlags const pcShaderStages = {}) {
    vk::PushConstantRange const pushConstantRange{pcShaderStages, 0, pcSize};

    vk::PipelineLayoutCreateInfo const createInfo{vk::PipelineLayoutCreateFlags{},
                                                  static_cast<uint32_t>(layouts.size()), layouts.data(),
                                                  static_cast<bool>(pcShaderStages) ? 1u : 0u, &pushConstantRange};

    vk::PipelineLayout layout;
    sponza::detail::validate(device.createPipelineLayout(&createInfo, nullptr, &layout),
                             "failed to create pipeline layout");
    return layout;
}

[[nodiscard]] vk::PipelineCache create_pipeline_cache(vk::Device const device) {
    vk::PipelineCacheCreateInfo constexpr createInfo{vk::PipelineCacheCreateFlags{}};

    vk::PipelineCache cache;
    sponza::detail::validate(device.createPipelineCache(&createInfo, nullptr, &cache),
                             "failed to create pipeline cache");
    return cache;
}

/* * * * */

[[nodiscard]] sponza::detail::PassData make_forward_pass_data(vk::Device const device, vk::Extent2D const framebuffer,
                                                              vk::PipelineCache const cache) {
    sponza::detail::PassData data;

    auto const vertShader = create_shader_module(device, shader_link_compiled::spv_forward_vert);
    auto const fragShader = create_shader_module(device, shader_link_compiled::spv_forward_frag);

    sponza::detail::PipelineBuilder builder;

    data.pipelineLayout = create_pipeline_layout(device, data.descriptorSetLayouts);
    data.pipeline =
        builder.cache(cache)
            .layout(data.pipelineLayout)
            .shader(vk::ShaderStageFlagBits::eVertex, vertShader)
            .shader(vk::ShaderStageFlagBits::eFragment, fragShader)
            .viewport(
                {0.f, 0.f, static_cast<float>(framebuffer.width), static_cast<float>(framebuffer.height), 0.f, 1.f})
            .scissors({vk::Offset2D{0, 0}, framebuffer})
            .vertex_binding(0, sizeof(sponza::Resources::Vertex))
            .vertex_attribute(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(sponza::Resources::Vertex, position))
            .vertex_attribute(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(sponza::Resources::Vertex, normal))
            .vertex_attribute(2, 0, vk::Format::eR32G32Sfloat, offsetof(sponza::Resources::Vertex, texcoord))
            .cull_mode(vk::CullModeFlagBits::eBack, vk::FrontFace::eClockwise)
            .color_attachment(vk::Format::eB8G8R8A8Srgb, sponza::detail::PipelineBuilder::alpha_blend())
            .depth_attachment(vk::Format::eD32SfloatS8Uint)
            .depth_test(true, vk::CompareOp::eLess)
            .polygon_mode(vk::PolygonMode::eFill)
            .build_graphics(device);

    device.destroyShaderModule(vertShader);
    device.destroyShaderModule(fragShader);

    return data;
}

[[nodiscard]] sponza::detail::PassData make_present_pass_data() noexcept { return {}; }

void destroy_pass_data(vk::Device const device, sponza::detail::PassData const &data) {
    for (auto &descriptorSetLayout : data.descriptorSetLayouts)
        device.destroyDescriptorSetLayout(descriptorSetLayout);

    device.destroyDescriptorPool(data.descriptorPool);
    device.destroyPipelineLayout(data.pipelineLayout);
    device.destroyPipeline(data.pipeline);
}

/* * * * */

struct GraphResourceIDs {
    caldera::BufferVersion vbo;
    caldera::BufferVersion ibo;

    caldera::ImageVersion outImage;
    caldera::ImageVersion depthImage;

    caldera::ImageViewID outView;
    caldera::ImageViewID depthView;
};

void declare_forward_pass(caldera::GraphDeclarator &declarator, GraphResourceIDs &ids,
                          sponza::detail::PassData const &passData)
{
    struct ForwardPassData {
        caldera::BufferID vbo;
        caldera::BufferID ibo;
        caldera::ImageViewID color;
        caldera::ImageViewID depth;

        vk::Pipeline pipeline;
    };

    declarator.add_pass<ForwardPassData>(
        "forward-pass", caldera::FamilyType::graphics,
        [&](caldera::PassDeclarator &pass, ForwardPassData& data) {
            ids.depthView = pass.request_image_view(
                ids.depthImage.id,
                {.flags = {},
                 .type = vk::ImageViewType::e2D,
                 .format = vk::Format::eD32SfloatS8Uint,
                 .mapping = {},
                 .subresource = vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eDepth, 0, vk::RemainingMipLevels, 0,
                                                          vk::RemainingArrayLayers}});

            data.vbo = ids.vbo.id;
            data.ibo = ids.ibo.id;
            data.color = ids.outView;
            data.depth = ids.depthView;
            data.pipeline = passData.pipeline;

            pass.read(data.vbo, caldera::BufferUsage::vertices);
            pass.read(data.ibo, caldera::BufferUsage::indices);

            ids.outImage = pass.write(ids.outImage, caldera::ImageUsage::color_attachment);
            ids.depthImage = pass.write(ids.depthImage, caldera::ImageUsage::depth_stencil_attachment);
        },
        [](vk::CommandBuffer const cmd, caldera::ResourceMap const &rMap, ForwardPassData const& data) {
            vk::DeviceSize constexpr zero_offset = 0;
            uint32_t constexpr index_count = 786801;

            auto const vbo = rMap.at(data.vbo);
            auto const ibo = rMap.at(data.ibo);
            auto const outImage = rMap.at(data.color);
            auto const depthImage = rMap.at(data.depth);

            vk::RenderingAttachmentInfo colorAttachment{};
            colorAttachment.clearValue = vk::ClearColorValue{0.f, 0.f, 0.f, 1.f};
            colorAttachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            colorAttachment.imageView = outImage;
            colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
            colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;

            vk::RenderingAttachmentInfo depthAttachment{};
            depthAttachment.clearValue = vk::ClearDepthStencilValue{1.f, 0};
            depthAttachment.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            depthAttachment.imageView = depthImage;
            depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
            depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;

            vk::RenderingInfo renderingInfo{};
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttachment;
            renderingInfo.pDepthAttachment = &depthAttachment;
            renderingInfo.layerCount = 1;
            renderingInfo.renderArea = vk::Rect2D{{0, 0}, {1600, 900}};

            cmd.beginRendering(renderingInfo);

            cmd.bindVertexBuffers(0, 1, &vbo, &zero_offset);
            cmd.bindIndexBuffer(ibo, zero_offset, vk::IndexType::eUint32);

            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, data.pipeline);

            cmd.drawIndexed(index_count, 1, 0, 0, 0);

            cmd.endRendering();
        });
}

void declare_present_pass(caldera::GraphDeclarator &declarator, GraphResourceIDs const &ids) {
    declarator.add_pass(
        "present-pass", caldera::FamilyType::graphics,
        [&](caldera::PassDeclarator &pass) { pass.read(ids.outImage, caldera::ImageUsage::present); },
        [=](vk::CommandBuffer const, caldera::ResourceMap const &) {});
}
} // namespace

namespace sponza {

Pipeline::Pipeline(Context &ctx, Resources &resources)
    : m_context(&ctx), m_pipelineCache(create_pipeline_cache(ctx.m_device.device)) {
    auto const device = static_cast<vk::Device>(m_context->m_device.device);

    caldera::GraphDeclarator declarator{};
    GraphResourceIDs ids{};
    ids.vbo = m_resources.map(resources.m_vbo);
    ids.ibo = m_resources.map(resources.m_ibo);

    auto const swapchainImageID = m_resources.map(vk::Image{VK_NULL_HANDLE});
    ids.outImage = swapchainImageID;
    ids.outView = m_resources.map(swapchainImageID, vk::ImageView{VK_NULL_HANDLE});
    m_swapchainImageID = ids.outView;

    ids.depthImage = declarator.transient_image({.flags = {},
                                                 .type = vk::ImageType::e2D,
                                                 .format = vk::Format::eD32SfloatS8Uint,
                                                 .extent = vk::Extent3D{m_context->m_extent, 1},
                                                 .mipLevels = 1,
                                                 .arrayLayers = 1,
                                                 .samples = vk::SampleCountFlagBits::e1,
                                                 .tiling = vk::ImageTiling::eOptimal});

    m_passes.reserve(2);
    auto const &forwardPassData =
        m_passes.emplace_back(make_forward_pass_data(device, m_context->m_extent, m_pipelineCache));
    m_passes.emplace_back(make_present_pass_data());

    declare_forward_pass(declarator, ids, forwardPassData);
    declare_present_pass(declarator, ids);

    auto const ir = std::move(declarator).extract();
    auto graph = caldera::RenderGraph::create(device, m_context->m_allocator, m_context->m_families, ir);

    detail::validate(graph.has_value(), "failed to create render graph");
    m_graph = std::move(*graph);

    caldera::GraphProperties properties{m_graph};
    m_properties.imageAvailableSyncPoint = properties.find_image_acquire_sync_point(m_swapchainImageID.image);
    m_properties.renderFinishedSyncPoint = properties.find_render_finished_sync_point(m_swapchainImageID.image);

    m_properties.familyPassCount.reserve(3);
    auto const passCounts = properties.get_pass_counts_per_families();

    for (auto const [familyIdx, passCount] : passCounts)
        m_properties.familyPassCount[familyIdx] += passCount;
}

Pipeline::~Pipeline() noexcept {
    if (auto const device = static_cast<vk::Device>(m_context->m_device.device)) {
        (void)device.waitIdle();

        for (auto &pass : m_passes)
            destroy_pass_data(device, pass);

        device.destroyPipelineCache(m_pipelineCache);
    }
}
} // namespace sponza