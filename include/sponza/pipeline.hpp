#pragma once

#include <caldera/caldera.hpp>
#include <sponza/context.hpp>
#include <sponza/resources.hpp>

#include <vector>
#include <vulkan/vulkan.hpp>

namespace sponza::detail {
    struct PassData {
        std::vector<vk::ShaderModule> shaderModules;

        std::vector<vk::DescriptorSetLayout> descriptorSetLayouts;
        vk::DescriptorPool descriptorPool;

        std::vector<vk::DescriptorSet> descriptors;

        vk::PipelineLayout pipelineLayout;
        vk::Pipeline pipeline;
    };
}

namespace sponza {
class Pipeline {
public:
    Pipeline(Context& ctx, Resources& resources);
    ~Pipeline() noexcept;

private:
    Context* m_context;

    vk::PipelineCache m_pipelineCache;

    std::vector<detail::PassData> m_passes;

    caldera::ResourceMap m_resources;
    caldera::RenderGraph m_graph;
};
}