#include "utility.hpp"
#include <sponza/renderer.hpp>

#include <iostream>
#include <ranges>

namespace {
constexpr uint32_t frames_inflight = 2;
constexpr uint32_t command_buffer_count = 10;

[[nodiscard]] vk::CommandPool make_command_pool(vk::Device const device, uint32_t const family) {
    vk::CommandPoolCreateInfo const createInfo{vk::CommandPoolCreateFlagBits::eTransient, family};
    vk::CommandPool pool;

    sponza::detail::validate(device.createCommandPool(&createInfo, nullptr, &pool), "failed to create command pool");
    return pool;
}

[[nodiscard]] vk::Semaphore make_semaphore(vk::Device const device, bool const timeline) {
    vk::StructureChain const createInfo{
        vk::SemaphoreCreateInfo{},
        vk::SemaphoreTypeCreateInfo{timeline ? vk::SemaphoreType::eTimeline : vk::SemaphoreType::eBinary, 0}};

    vk::Semaphore semaphore;
    sponza::detail::validate(device.createSemaphore(&createInfo.get<>(), nullptr, &semaphore),
                             "failed to create semaphore");
    return semaphore;
}

[[nodiscard]] vk::Fence make_fence(vk::Device const device, bool const signaled) {
    vk::FenceCreateInfo const createInfo{signaled ? vk::FenceCreateFlagBits::eSignaled : vk::FenceCreateFlagBits{}};

    vk::Fence fence;
    sponza::detail::validate(device.createFence(&createInfo, nullptr, &fence), "failed to create fence");
    return fence;
}

[[nodiscard]] std::vector<vk::CommandBuffer> make_command_buffers(vk::Device const device, vk::CommandPool const pool,
                                                                  uint32_t const count) {
    vk::CommandBufferAllocateInfo const allocateInfo{pool, vk::CommandBufferLevel::ePrimary, count};
    std::vector<vk::CommandBuffer> buffers(count);

    sponza::detail::validate(device.allocateCommandBuffers(&allocateInfo, buffers.data()),
                             "failed to allocate command buffers");
    return buffers;
}

/* * * * */

[[nodiscard]] sponza::detail::FrameData make_frame_data(vk::Device const device, caldera::FamilyNumbers const &families,
                                                        uint32_t const cmdCount) {
    sponza::detail::FrameData frameData;

    frameData.imageAvailableSemaphore = make_semaphore(device, false);
    frameData.renderFinishedSemaphore = make_semaphore(device, false);
    frameData.inflighFence = make_fence(device, true);

    frameData.familyCommandPools[families.graphics] = make_command_pool(device, families.graphics);
    frameData.familyCommandPools[families.transfer] = make_command_pool(device, families.transfer);
    frameData.familyCommandPools[families.compute] = make_command_pool(device, families.compute);

    for (auto const [family, pool] : frameData.familyCommandPools) {
        auto const buffers = make_command_buffers(device, pool, cmdCount);
        frameData.commandDistributor.feed(family, buffers);
    }

    return frameData;
}

[[nodiscard]] sponza::detail::TimelineSemaphores make_timeline_semaphores(vk::Device const device) {
    sponza::detail::TimelineSemaphores timelineSemaphores;

    timelineSemaphores.semaphore = make_semaphore(device, true);
    timelineSemaphores.timeline = 1;

    return timelineSemaphores;
}

/* * * * */

void destroy_frame_data(vk::Device const device, sponza::detail::FrameData const &frameData) {
    if (device) {
        device.destroySemaphore(frameData.imageAvailableSemaphore);
        device.destroySemaphore(frameData.renderFinishedSemaphore);
        device.destroyFence(frameData.inflighFence);

        for (auto const &pool : frameData.familyCommandPools | std::views::values)
            device.destroyCommandPool(pool);
    }
}

void destroy_timeline_semaphores(vk::Device const device,
                                 sponza::detail::TimelineSemaphores const &timelineSemaphores) {
    if (device)
        device.destroySemaphore(timelineSemaphores.semaphore);
}

/* * * * */

[[nodiscard]] uint32_t begin_frame(vk::Device const device, vk::SwapchainKHR const swapchain,
                                   sponza::detail::FrameData const &frameData) {
    std::cout << "[[[ BEGINNING A FRAME ]]]" << std::endl;
    uint32_t imageIdx;
    sponza::detail::validate(
        device.acquireNextImageKHR(swapchain, UINT64_MAX, frameData.imageAvailableSemaphore, nullptr, &imageIdx),
        "failed to acquire swapchain index");

    sponza::detail::validate(device.waitForFences(frameData.inflighFence, vk::True, UINT64_MAX),
                             "failed to wait for fence");

    sponza::detail::validate(device.resetFences(frameData.inflighFence), "failed to reset the fence");

    return imageIdx;
}

void end_frame(vk::Queue const presentQueue, vk::SwapchainKHR const swapchain, uint32_t const imageIdx,
               sponza::detail::FrameData const &frameData) {
    sponza::detail::validate(presentQueue.presentKHR({frameData.renderFinishedSemaphore, swapchain, imageIdx}),
                             "failed to present image");
    std::cout << "[[[ ENDED A FRAME ]]]" << std::endl;
}
} // namespace

namespace sponza {
Renderer::Renderer(Context const &ctx) : m_context(&ctx), m_frameCount(0)
{
    auto const device = static_cast<vk::Device>(m_context->m_device.device);
    auto const families = m_context->m_families;

    for (uint32_t i = 0; i < frames_inflight; ++i)
        m_frameData.emplace_back(make_frame_data(device, families, command_buffer_count));

    for (uint32_t i = 0; i < m_context->m_swapchainImages.size(); ++i)
        m_imageData.emplace_back(m_context->m_swapchainImages[i], m_context->m_swapchainImageViews[i]);

    auto const pushFamilySemaphores = [&](uint32_t const family) {
        if (auto const [iter, inserted] = m_familySemaphores.try_emplace(family); inserted)
            iter->second = make_timeline_semaphores(device);
    };

    auto const setFamilyQueue = [&](uint32_t const family) {
        VkQueue queue;
        vkGetDeviceQueue(m_context->m_device.device, family, 0, &queue);

        m_queues[family] = queue;
    };

    pushFamilySemaphores(families.graphics);
    pushFamilySemaphores(families.transfer);
    pushFamilySemaphores(families.compute);

    setFamilyQueue(families.graphics);
    setFamilyQueue(families.transfer);
    setFamilyQueue(families.compute);
}

Renderer::~Renderer() noexcept {
    auto const device = static_cast<vk::Device>(m_context->m_device.device);

    for (auto const &semaphore : m_familySemaphores | std::views::values)
        destroy_timeline_semaphores(device, semaphore);

    for (auto const &data : m_frameData)
        destroy_frame_data(device, data);
}

void Renderer::process_frame(Pipeline &pipeline) {
    auto const device = static_cast<vk::Device>(m_context->m_device.device);
    auto const swapchain = static_cast<vk::SwapchainKHR>(m_context->m_swapchain.swapchain);

    auto const frameIdx = (m_frameCount++) % frames_inflight;
    auto &frameData = m_frameData[frameIdx];

    auto const imageIdx = begin_frame(device, swapchain, frameData);
    auto const &imageData = m_imageData[imageIdx];

    auto const families = m_context->m_families;
    pipeline.m_resources.map(pipeline.m_swapchainImageID.image, imageData.image);
    pipeline.m_resources.map(pipeline.m_swapchainImageID, imageData.view);

    for (auto const pool : frameData.familyCommandPools | std::views::values)
        detail::validate(device.resetCommandPool(pool), "failed to reset a command pool");

    frameData.commandDistributor.reset(families.graphics);
    frameData.commandDistributor.reset(families.transfer);
    frameData.commandDistributor.reset(families.compute);

    caldera::GraphExecutor executor{};
    caldera::ExecutionContext const context{.resourceMap = &pipeline.m_resources,
                                            .stateManager = &pipeline.m_stateManager,
                                            .distributor = &frameData.commandDistributor};

    auto const graph = executor.execute(pipeline.m_graph, context, frameData.executionMemory);
    detail::validate(graph.has_value(), "failed to execute graph");

    for (uint32_t passIdx = 0; passIdx < graph->passes.size(); ++passIdx) {
        auto const &pass = graph->passes[passIdx];

        m_cache.waitSemaphores.clear();
        m_cache.signalSemaphores.clear();

        m_cache.waitSemaphores.reserve(1 + pass.passDependencies.size());
        m_cache.signalSemaphores.reserve(2);

        auto const queue = m_queues.at(pass.familyIdx);
        auto const& properties = pipeline.m_properties;

        for (auto const &passDep : pass.passDependencies) {
            auto const otherPassFamily = graph->passes[passDep.otherPass].familyIdx;
            auto const &semaphore = m_familySemaphores.at(otherPassFamily);
            m_cache.waitSemaphores.emplace_back(semaphore.semaphore, semaphore.timeline + passDep.otherPass,
                                                passDep.srcStages);
        }

        if (auto const syncPoint = properties.imageAvailableSyncPoint; syncPoint.passIdx == passIdx)
            m_cache.waitSemaphores.emplace_back(frameData.imageAvailableSemaphore, 0, syncPoint.flags);

        auto const &semaphores = m_familySemaphores.at(pass.familyIdx);
        m_cache.signalSemaphores.emplace_back(semaphores.semaphore, semaphores.timeline + passIdx,
                                              vk::PipelineStageFlagBits2::eAllCommands);

        if (auto const syncPoint = properties.renderFinishedSyncPoint; syncPoint.passIdx == passIdx)
            m_cache.signalSemaphores.emplace_back(frameData.renderFinishedSemaphore, 0, syncPoint.flags);

        vk::CommandBufferSubmitInfo cmdInfo{};
        cmdInfo.commandBuffer = pass.commandBuffer;

        vk::SubmitInfo2 submitInfo{};
        submitInfo.setWaitSemaphoreInfos(m_cache.waitSemaphores);
        submitInfo.setSignalSemaphoreInfos(m_cache.signalSemaphores);
        submitInfo.setCommandBufferInfos(cmdInfo);

        detail::validate(
            queue.submit2(submitInfo, passIdx == graph->passes.size() - 1 ? frameData.inflighFence : VK_NULL_HANDLE),
            "failed to submit command buffer");
    }

    for (auto &semaphore : m_familySemaphores | std::views::values)
        semaphore.timeline += graph->passes.size();

    end_frame(m_queues[families.graphics], swapchain, imageIdx, frameData);
}
} // namespace sponza