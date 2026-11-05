#include "utility.hpp"
#include <sponza/renderer.hpp>

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
} // namespace

namespace sponza {
Renderer::Renderer(Context const &ctx) : m_context(&ctx) {
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

    pushFamilySemaphores(families.graphics);
    pushFamilySemaphores(families.transfer);
    pushFamilySemaphores(families.compute);
}

Renderer::~Renderer() noexcept {
    auto const device = static_cast<vk::Device>(m_context->m_device.device);

    for (auto const &semaphore : m_familySemaphores | std::views::values)
        destroy_timeline_semaphores(device, semaphore);

    for (auto const &data : m_frameData)
        destroy_frame_data(device, data);
}
} // namespace sponza