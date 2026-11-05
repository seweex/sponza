#pragma once

#include <caldera/caldera.hpp>
#include <sponza/context.hpp>
#include <vulkan/vulkan.hpp>

namespace sponza::detail {
struct FrameData {
    vk::Semaphore imageAvailableSemaphore;
    vk::Semaphore renderFinishedSemaphore;

    vk::Fence inflighFence;

    std::unordered_map<uint32_t, vk::CommandPool> familyCommandPools;
    caldera::CommandBufferDistributor commandDistributor;
};

struct ImageData {
    vk::Image image;
    vk::ImageView view;
};

struct TimelineSemaphores {
    vk::Semaphore semaphore;
    uint64_t timeline;
};
} // namespace sponza::detail

namespace sponza {
class Renderer {
  public:
    Renderer(Context const &ctx);
    ~Renderer() noexcept;

  private:
    Context const *m_context;

    std::vector<detail::FrameData> m_frameData;
    std::vector<detail::ImageData> m_imageData;
    std::unordered_map<uint32_t, detail::TimelineSemaphores> m_familySemaphores;
};
} // namespace sponza