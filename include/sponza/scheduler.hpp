#pragma once

#include <sponza/vulkan_context.hpp>
#include <vulkan/vulkan.hpp>

namespace sponza {
static constexpr uint32_t frames_inflight = 3;
}

namespace sponza::detail {
class FrameData {
  public:
    FrameData() noexcept;
    ~FrameData() noexcept;

    [[nodiscard]] vk::Result init(vk::Device device, caldera::FamilyNumbers const &families,
                                  caldera::FamilyNumbers const &commandCounts);

    vk::Fence inflightFence;
    vk::Semaphore imageAcquiredSemaphore;

    caldera::ExecutionMemory executionMemory;
    caldera::GraphExecutor graphExecutor;

    vk::CommandPool graphicsCommandPool;
    vk::CommandPool transferCommandPool;
    vk::CommandPool computeCommandPool;

    caldera::CommandBufferDistributor commandDistributor;

  private:
    vk::Device m_device;
};

class ImageData {
  public:
    ImageData() noexcept;
    ~ImageData() noexcept;

    [[nodiscard]] vk::Result init(vk::Device device, vk::Image swapchainImage, vk::ImageView swapchainImageView);

    vk::Image swapchainImage;
    vk::ImageView swapchainImageView;
    vk::Semaphore renderFinishedSemaphore;

  private:
    vk::Device m_device;
};

class FamilyData {
  public:
    FamilyData() noexcept;
    ~FamilyData() noexcept;

    [[nodiscard]] vk::Result init(vk::Device device, vk::Queue queue);

    vk::Queue queue;
    vk::Semaphore semaphore;
    uint64_t timeline = 1;

  private:
    vk::Device m_device;
};
} // namespace sponza::detail

namespace sponza {
struct RenderingContext {
    caldera::ImageID swapchainImageID;
    caldera::SemaphoreSyncPoint imageAcquiredPoint;
    caldera::SemaphoreSyncPoint renderFinishedPoint;

    caldera::RenderGraph& graph;
    caldera::ResourceMap& resourceMap;
    caldera::ResourceStateManager& stateManager;
};

class Scheduler {
    [[nodiscard]] std::expected<uint32_t, vk::Result> begin_frame();
    [[nodiscard]] vk::Result end_frame(uint32_t imageIdx);

  public:
    [[nodiscard]] vk::Result init(VulkanContext const& context, caldera::FamilyNumbers const &commandCounts);
    [[nodiscard]] vk::Result submit(RenderingContext const &ctx);

  private:
    vk::Device m_device;
    vk::SwapchainKHR m_swapchain;

    vk::Queue m_presentQueue;
    std::unordered_map<uint32_t, detail::FamilyData> m_familyData;

    std::array<detail::FrameData, frames_inflight> m_frameData;
    std::vector<detail::ImageData> m_imageData;

    uint64_t m_frameCount = 0;
};
} // namespace sponza