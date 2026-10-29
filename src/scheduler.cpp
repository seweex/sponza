#include <ranges>

#include <caldera/caldera.hpp>
#include <sponza/scheduler.hpp>
#include <sponza/utility.hpp>

namespace {
[[nodiscard]] std::expected<vk::CommandPool, vk::Result> make_command_pool(vk::Device const device,
                                                                           uint32_t const family) {
    vk::CommandPoolCreateInfo const createInfo{vk::CommandPoolCreateFlagBits::eTransient, family};
    vk::CommandPool pool;

    SPONZA_CHECK_RCODE((device.createCommandPool(&createInfo, nullptr, &pool)), std::unexpected);
    return pool;
}

[[nodiscard]] std::expected<vk::Semaphore, vk::Result> make_semaphore(vk::Device const device, bool const timeline) {
    vk::StructureChain const createInfo{
        vk::SemaphoreCreateInfo{},
        vk::SemaphoreTypeCreateInfo{timeline ? vk::SemaphoreType::eTimeline : vk::SemaphoreType::eBinary, 0}};

    vk::Semaphore semaphore;
    SPONZA_CHECK_RCODE((device.createSemaphore(&createInfo.get<>(), nullptr, &semaphore)), std::unexpected);
    return semaphore;
}

[[nodiscard]] std::expected<vk::Fence, vk::Result> make_fence(vk::Device const device, bool const signaled) {
    vk::FenceCreateInfo const createInfo{signaled ? vk::FenceCreateFlagBits::eSignaled : vk::FenceCreateFlagBits{}};

    vk::Fence fence;
    SPONZA_CHECK_RCODE((device.createFence(&createInfo, nullptr, &fence)), std::unexpected);
    return fence;
}

[[nodiscard]] std::expected<std::vector<vk::CommandBuffer>, vk::Result>
make_command_buffers(vk::Device const device, vk::CommandPool const pool, uint32_t const count) {
    vk::CommandBufferAllocateInfo const allocateInfo{pool, vk::CommandBufferLevel::ePrimary, count};

    if (auto rVal = device.allocateCommandBuffers(allocateInfo); rVal.result < vk::Result::eSuccess)
        return std::unexpected{rVal.result};
    else
        return std::move(*rVal);
}
} // namespace

namespace sponza::detail {

// Frame Data

FrameData::FrameData() noexcept = default;

FrameData::~FrameData() noexcept {
    if (m_device) {
        m_device.destroyCommandPool(graphicsCommandPool);
        m_device.destroyCommandPool(transferCommandPool);
        m_device.destroyCommandPool(computeCommandPool);

        m_device.destroySemaphore(imageAcquiredSemaphore);
        m_device.destroyFence(inflightFence);
    }
}

vk::Result FrameData::init(vk::Device const device, caldera::FamilyNumbers const &families,
                           caldera::FamilyNumbers const &commandCounts) {
    m_device = device;

    auto const fence = make_fence(m_device, true);
    if (!fence.has_value())
        return fence.error();
    inflightFence = *fence;

    auto const semaphore = make_semaphore(m_device, false);
    if (!semaphore.has_value())
        return semaphore.error();
    imageAcquiredSemaphore = *semaphore;

    auto createPool = [&] (
        uint32_t const family,
        uint32_t const cmdCount,
        auto& targetPool) -> vk::Result
    {
        auto const pool = make_command_pool(m_device, family);
        if (!pool.has_value())
            return pool.error();

        targetPool = *pool;

        auto const buffers = make_command_buffers(m_device, targetPool, cmdCount);
        if (!buffers.has_value())
            return buffers.error();

        commandDistributor.feed(family, *buffers);
        return vk::Result::eSuccess;
    };

    SPONZA_CHECK_RCODE((createPool(families.graphics, commandCounts.graphics, graphicsCommandPool)), vk::Result);
    SPONZA_CHECK_RCODE((createPool(families.transfer, commandCounts.transfer, transferCommandPool)), vk::Result);
    SPONZA_CHECK_RCODE((createPool(families.compute, commandCounts.compute, computeCommandPool)), vk::Result);

    return vk::Result::eSuccess;
}

// Image Data

ImageData::ImageData() noexcept = default;

ImageData::~ImageData() noexcept {
    if (m_device)
        m_device.destroySemaphore(renderFinishedSemaphore);
}

vk::Result ImageData::init(vk::Device const device, vk::Image const swapchainImage_, vk::ImageView const swapchainImageView_) {
    m_device = device;
    swapchainImage = swapchainImage_;
    swapchainImageView = swapchainImageView_;

    auto const semaphore = make_semaphore(m_device, false);
    if (!semaphore.has_value())
        return semaphore.error();

    renderFinishedSemaphore = *semaphore;
    return vk::Result::eSuccess;
}

// Family Data

FamilyData::FamilyData() noexcept = default;

FamilyData::~FamilyData() noexcept {
    if (m_device)
        m_device.destroySemaphore(semaphore);
}

vk::Result FamilyData::init(vk::Device const device, vk::Queue const queue_) {
    m_device = device;
    queue = queue_;

    auto const semaphore_ = make_semaphore(m_device, true);
    if (!semaphore_.has_value())
        return semaphore_.error();

    semaphore = *semaphore_;
    timeline = 1;
    return vk::Result::eSuccess;
}
} // namespace sponza::detail

namespace sponza {
std::expected<uint32_t, vk::Result> Scheduler::begin_frame() {
    auto const frameIdx = m_frameCount % frames_inflight;
    auto &frameData = m_frameData[frameIdx];

    uint32_t imageIdx;

    SPONZA_CHECK_RCODE(
        (m_device.acquireNextImageKHR(m_swapchain, UINT64_MAX, frameData.imageAcquiredSemaphore, nullptr, &imageIdx)),
        std::unexpected);

    SPONZA_CHECK_RCODE((m_device.waitForFences(frameData.inflightFence, vk::True, UINT64_MAX)), std::unexpected);
    SPONZA_CHECK_RCODE(m_device.resetFences(frameData.inflightFence), std::unexpected);

    SPONZA_CHECK_RCODE(m_device.resetCommandPool(frameData.graphicsCommandPool), std::unexpected);
    SPONZA_CHECK_RCODE(m_device.resetCommandPool(frameData.transferCommandPool), std::unexpected);
    SPONZA_CHECK_RCODE(m_device.resetCommandPool(frameData.computeCommandPool), std::unexpected);

    return imageIdx;
}

vk::Result Scheduler::end_frame(uint32_t const imageIdx) {
    auto const &imageData = m_imageData[imageIdx];

    SPONZA_CHECK_RCODE(m_presentQueue.presentKHR({1u, &imageData.renderFinishedSemaphore, 1u, &m_swapchain, &imageIdx}),
                       vk::Result);

    ++m_frameCount;
    return vk::Result::eSuccess;
}

vk::Result Scheduler::init(VulkanContext const &context, caldera::FamilyNumbers const &commandCounts) {
    m_device = context.get_device();
    m_swapchain = context.get_swapchain();

    auto const families = context.get_family_indices();
    m_presentQueue = context.get_queue(families.graphics);

    m_familyData.try_emplace(families.graphics);
    m_familyData.try_emplace(families.transfer);
    m_familyData.try_emplace(families.compute);

    for (auto &[familyIdx, data] : m_familyData)
        SPONZA_CHECK_RCODE((data.init(m_device, context.get_queue(familyIdx))), vk::Result);

    for (auto &data : m_frameData)
        SPONZA_CHECK_RCODE((data.init(m_device, families, commandCounts)), vk::Result);

    auto const swapchainImages = context.get_swapchain_images();
    auto const swapchainImageViews = context.get_swapchain_image_views();
    m_imageData.resize(swapchainImages.size());

    for (uint32_t imageIdx = 0; imageIdx < swapchainImages.size(); ++imageIdx)
        SPONZA_CHECK_RCODE(
            (m_imageData.emplace_back().init(m_device, swapchainImages[imageIdx], swapchainImageViews[imageIdx])),
            vk::Result);

    m_frameCount = 0;
    return vk::Result::eSuccess;
}

vk::Result Scheduler::submit(RenderingContext const &ctx) {
    auto const imageIdx = begin_frame();
    if (!imageIdx.has_value())
        return imageIdx.error();

    auto const frameIdx = m_frameCount % frames_inflight;
    auto &frameData = m_frameData[frameIdx];
    auto &imageData = m_imageData[*imageIdx];

    caldera::ExecutionContext const context{.resourceMap = &ctx.resourceMap,
                                            .stateManager = &ctx.stateManager,
                                            .distributor = &frameData.commandDistributor};

    auto const submittableGraph = frameData.graphExecutor.execute(ctx.graph, context, frameData.executionMemory);
    if (!submittableGraph.has_value())
        return submittableGraph.error();

    std::vector<vk::SemaphoreSubmitInfo> waitSemaphores;
    std::vector<vk::SemaphoreSubmitInfo> signalSemaphores;

    for (uint32_t passIdx = 0; passIdx < submittableGraph->passes.size(); ++passIdx) {
        auto const &pass = submittableGraph->passes[passIdx];
        auto &familyData = m_familyData.at(pass.familyIdx);

        waitSemaphores.clear();
        signalSemaphores.clear();

        waitSemaphores.reserve(pass.passDependencies.size() + 1);
        signalSemaphores.reserve(2);

        signalSemaphores.emplace_back(familyData.semaphore, familyData.timeline + passIdx,
                                      vk::PipelineStageFlagBits2::eAllCommands);

        if (ctx.imageAcquiredPoint.passIdx == passIdx)
            waitSemaphores.emplace_back(frameData.imageAcquiredSemaphore, 0, ctx.imageAcquiredPoint.flags);

        if (ctx.renderFinishedPoint.passIdx == passIdx)
            waitSemaphores.emplace_back(imageData.renderFinishedSemaphore, 0, ctx.renderFinishedPoint.flags);

        vk::CommandBufferSubmitInfo cmdInfo{};
        cmdInfo.commandBuffer = pass.commandBuffer;

        vk::SubmitInfo2 submitInfo{};
        submitInfo.setCommandBufferInfos(cmdInfo);
        submitInfo.setWaitSemaphoreInfos(waitSemaphores);
        submitInfo.setSignalSemaphoreInfos(signalSemaphores);

        if (auto const rCode = familyData.queue.submit2(
                submitInfo, passIdx == submittableGraph->passes.size() - 1 ? frameData.inflightFence : VK_NULL_HANDLE);
            rCode < vk::Result::eSuccess)
            return rCode;
    }

    return end_frame(*imageIdx);
}
} // namespace sponza