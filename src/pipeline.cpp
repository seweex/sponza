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

    auto const vertShader =
        data.shaderModules.emplace_back(create_shader_module(device, shader_link_compiled::spv_forward_vert));
    auto const fragShader =
        data.shaderModules.emplace_back(create_shader_module(device, shader_link_compiled::spv_forward_frag));

    sponza::detail::PipelineBuilder builder;

    data.pipelineLayout = create_pipeline_layout(device, data.descriptorSetLayouts);
    data.pipeline =
        builder.cache(cache)
            .layout(data.pipelineLayout)
            .shader(vk::ShaderStageFlagBits::eVertex, vertShader)
            .shader(vk::ShaderStageFlagBits::eFragment, fragShader)
            .viewport({ 0.f, 0.f, static_cast<float>(framebuffer.width), static_cast<float>(framebuffer.height), 0.f, 1.f })
            .scissors({vk::Offset2D{0, 0}, framebuffer})
            .vertex_binding(0, sizeof(sponza::Resources::Vertex))
            .vertex_attribute(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(sponza::Resources::Vertex, position))
            .vertex_attribute(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(sponza::Resources::Vertex, normal))
            .vertex_attribute(2, 0, vk::Format::eR32G32Sfloat, offsetof(sponza::Resources::Vertex, texcoord))
            .cull_mode(vk::CullModeFlagBits::eBack, vk::FrontFace::eClockwise)
            .color_attachment(vk::Format::eB8G8R8A8Srgb, sponza::detail::PipelineBuilder::alpha_blend())
            .depth_attachment(vk::Format::eD32Sfloat)
            .build_graphics(device);

    device.destroyShaderModule(vertShader);
    device.destroyShaderModule(fragShader);

    return data;
}

void destroy_pass_data(vk::Device const device, sponza::detail::PassData const &data) {
    for (auto &descriptorSetLayout : data.descriptorSetLayouts)
        device.destroyDescriptorSetLayout(descriptorSetLayout);

    device.destroyDescriptorPool(data.descriptorPool);
    device.destroyPipelineLayout(data.pipelineLayout);
    device.destroyPipeline(data.pipeline);
}

/* * * * */

struct GraphResourceIDs {
    caldera::BufferID vboID;
    caldera::BufferID iboID;
    caldera::ImageViewID outImageID;
};

void declare_forward_pass(caldera::GraphDeclarator &declarator, GraphResourceIDs const &ids,
                          sponza::detail::PassData const &passData) {
    declarator.add_pass(
        "forward-pass", caldera::FamilyType::graphics,
        [&](caldera::PassDeclarator &pass) {
            pass.read(ids.vboID, caldera::BufferUsage::vertices);
            pass.read(ids.iboID, caldera::BufferUsage::indices);

            pass.write(ids.outImageID.image, caldera::ImageUsage::color_attachment);
        },
        [=](vk::CommandBuffer const cmd, caldera::ResourceMap const &rMap) {
            vk::DeviceSize constexpr zero_offset = 0;
            uint32_t constexpr index_count = 3 * 6 * 2;

            auto const vbo = rMap.at(ids.vboID);
            auto const ibo = rMap.at(ids.iboID);
            auto const outImage = rMap.at(ids.outImageID);

            vk::RenderingAttachmentInfo colorAttachment{};
            colorAttachment.clearValue = vk::ClearColorValue{1.f, 0.7f, 1.f, 1.f};
            colorAttachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            colorAttachment.imageView = outImage;
            colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
            colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;

            vk::RenderingInfo renderingInfo{};
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttachment;
            renderingInfo.layerCount = 1;
            renderingInfo.renderArea = vk::Rect2D{{0, 0}, {800, 600}};

            cmd.beginRendering(renderingInfo);

            cmd.bindVertexBuffers(0, 1, &vbo, &zero_offset);
            cmd.bindIndexBuffer(ibo, zero_offset, vk::IndexType::eUint32);

            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, passData.pipeline);

            cmd.drawIndexed(index_count, 1, 0, 0, 0);

            cmd.endRendering();
        });
}
} // namespace

namespace sponza {

Pipeline::Pipeline(Context &ctx, Resources &resources)
    : m_context(&ctx), m_pipelineCache(create_pipeline_cache(ctx.m_device.device)) {
    auto const device = static_cast<vk::Device>(m_context->m_device.device);

    GraphResourceIDs ids{};
    ids.vboID = m_resources.map(resources.m_vbo);
    ids.iboID = m_resources.map(resources.m_ibo);

    auto const swapchainImageID = m_resources.map(vk::Image{VK_NULL_HANDLE});
    ids.outImageID = m_resources.map(swapchainImageID, vk::ImageView{VK_NULL_HANDLE});
    m_swapchainImageID = ids.outImageID;

    auto const &forwardPassData = m_passes.emplace_back(make_forward_pass_data(device, m_context->m_extent, m_pipelineCache));

    caldera::GraphDeclarator declarator{};
    declare_forward_pass(declarator, ids, forwardPassData);

    auto const ir = std::move(declarator).extract();
    auto graph = caldera::RenderGraph::create(device, m_context->m_allocator, m_context->m_families, ir);

    detail::validate(graph.has_value(), "failed to create render graph");
    m_graph = std::move(*graph);

    caldera::GraphProperties properties{m_graph};
    m_properties.imageAvailableSyncPoint = properties.find_image_acquire_sync_point(m_swapchainImageID.image);
    m_properties.renderFinishedSyncPoint = properties.find_render_finished_sync_point(m_swapchainImageID.image);
}

Pipeline::~Pipeline() noexcept {
    if (auto const device = static_cast<vk::Device>(m_context->m_device.device)) {
        for (auto &pass : m_passes) {
            device.destroyDescriptorPool(pass.descriptorPool);

            for (auto const layout : pass.descriptorSetLayouts)
                device.destroyDescriptorSetLayout(layout);

            device.destroyPipelineLayout(pass.pipelineLayout);
            device.destroyPipeline(pass.pipeline);
        }

        device.destroyPipelineCache(m_pipelineCache);
    }
}
} // namespace sponza