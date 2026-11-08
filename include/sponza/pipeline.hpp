#pragma once

#include <caldera/caldera.hpp>
#include <sponza/context.hpp>
#include <sponza/resources.hpp>

#include <vector>
#include <vulkan/vulkan.hpp>

namespace sponza::detail {
struct PassData {
    std::vector<vk::DescriptorSetLayout> descriptorSetLayouts;
    vk::DescriptorPool descriptorPool;

    std::vector<vk::DescriptorSet> descriptors;

    vk::PipelineLayout pipelineLayout;
    vk::Pipeline pipeline;
};

struct GraphProperties {
    caldera::SemaphoreSyncPoint imageAvailableSyncPoint;
    caldera::SemaphoreSyncPoint renderFinishedSyncPoint;
    std::unordered_map<uint32_t, uint32_t> familyPassCount;
};
} // namespace sponza::detail

namespace sponza {
class Pipeline {
  public:
    Pipeline(Context &ctx, Resources &resources);
    ~Pipeline() noexcept;

  private:
    Context *m_context;

    vk::PipelineCache m_pipelineCache;

    std::vector<detail::PassData> m_passes;

    caldera::ResourceMap m_resources;
    caldera::ResourceStateManager m_stateManager;

    caldera::RenderGraph m_graph;
    detail::GraphProperties m_properties;

    caldera::ImageViewID m_swapchainImageID;

    friend class Renderer;
};
} // namespace sponza