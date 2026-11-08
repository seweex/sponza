#pragma once

#include <caldera/caldera.hpp>
#include <sponza/context.hpp>
#include <sponza/pipeline.hpp>
#include <vulkan/vulkan.hpp>

namespace sponza::detail {
struct FrameData {
    vk::Semaphore imageAvailableSemaphore;

    vk::Fence inflighFence;

    std::unordered_map<uint32_t, vk::CommandPool> familyCommandPools;
    caldera::CommandBufferDistributor commandDistributor;
    caldera::ExecutionMemory executionMemory;
};

struct ImageData {
    vk::Image image;
    vk::ImageView view;

    vk::Semaphore renderFinishedSemaphore;
};

struct TimelineSemaphores {
    vk::Semaphore semaphore;
    uint64_t timeline;
};

struct ExecutionCache {
    std::vector<vk::SemaphoreSubmitInfo> waitSemaphores;
    std::vector<vk::SemaphoreSubmitInfo> signalSemaphores;
};
} // namespace sponza::detail

namespace sponza {
class Renderer {
  public:
    Renderer(Context const &ctx);
    ~Renderer() noexcept;

    void process_frame(Pipeline& pipeline);

  private:
    Context const *m_context;

    std::unordered_map<uint32_t, vk::Queue> m_queues;

    std::vector<detail::FrameData> m_frameData;
    std::vector<detail::ImageData> m_imageData;
    std::unordered_map<uint32_t, detail::TimelineSemaphores> m_familySemaphores;

    uint64_t m_frameCount;

    detail::ExecutionCache m_cache;
};
} // namespace sponza