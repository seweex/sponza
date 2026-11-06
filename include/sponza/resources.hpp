#pragma once

#include <glm/glm.hpp>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

#include <sponza/context.hpp>

namespace sponza {
class Resources {
  public:
    struct Vertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 texcoord;
    };

    Resources(Context &context);
    ~Resources() noexcept;

  private:
    Context *m_context;

    VmaAllocation m_vbAllocation;
    VmaAllocation m_ibAllocation;

    vk::Buffer m_vbo;
    vk::Buffer m_ibo;

    friend class Pipeline;
};
} // namespace sponza