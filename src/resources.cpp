#include "utility.hpp"
#include <sponza/resources.hpp>

#include <span>
#include <tiny_obj_loader.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
constexpr const char *mesh_path = "assets/atrium.obj";

struct MeshData {
    std::vector<sponza::Resources::Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct IndexHash {
    [[nodiscard]] size_t operator()(tinyobj::index_t const &idx) const noexcept {
        auto const h1 = std::hash<int>{}(idx.vertex_index);
        auto const h2 = std::hash<int>{}(idx.normal_index);
        auto const h3 = std::hash<int>{}(idx.texcoord_index);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

struct IndexEqual {
    [[nodiscard]] bool operator()(tinyobj::index_t const &lhs, tinyobj::index_t const &rhs) const noexcept {
        return lhs.vertex_index == rhs.vertex_index && lhs.normal_index == rhs.normal_index &&
               lhs.texcoord_index == rhs.texcoord_index;
    }
};

/* * * * */

[[nodiscard]] MeshData load_mesh(const char *const path) {
    tinyobj::ObjReader reader;
    sponza::detail::validate(reader.ParseFromFile(path), "failed to load mesh from file");

    auto const &attrib = reader.GetAttrib();
    auto const &shapes = reader.GetShapes();

    MeshData mesh;
    std::unordered_map<tinyobj::index_t, uint32_t, IndexHash, IndexEqual> uniqueVertices;

    for (auto const &shape : shapes) {
        for (auto const &index : shape.mesh.indices) {
            auto const [iter, inserted] =
                uniqueVertices.try_emplace(index, static_cast<uint32_t>(mesh.vertices.size()));

            if (inserted) {
                sponza::Resources::Vertex vertex{};

                vertex.position = {
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2],
                };

                if (index.normal_index >= 0) {
                    vertex.normal = {
                        attrib.normals[3 * index.normal_index + 0],
                        attrib.normals[3 * index.normal_index + 1],
                        attrib.normals[3 * index.normal_index + 2],
                    };
                }

                if (index.texcoord_index >= 0) {
                    vertex.texcoord = {
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        1.0f - attrib.texcoords[2 * index.texcoord_index + 1],
                    };
                }

                mesh.vertices.emplace_back(vertex);
            }

            mesh.indices.emplace_back(iter->second);
        }
    }

    sponza::detail::validate(!mesh.vertices.empty() && !mesh.indices.empty(), "loaded mesh is empty");
    return mesh;
}

template <typename Ty>
[[nodiscard]] std::pair<vk::Buffer, VmaAllocation>
make_buffer(VmaAllocator const allocator, std::span<Ty const> const data, vk::BufferUsageFlags const usage) {
    auto const byteSize = static_cast<vk::DeviceSize>(data.size_bytes());
    vk::BufferCreateInfo const bufferInfo{{}, byteSize, usage, vk::SharingMode::eExclusive};

    VmaAllocationCreateInfo constexpr allocCreateInfo{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VkBuffer buffer{};
    VmaAllocation allocation{};
    VmaAllocationInfo allocInfo{};

    sponza::detail::validate(vmaCreateBuffer(allocator, &static_cast<VkBufferCreateInfo const &>(bufferInfo),
                                             &allocCreateInfo, &buffer, &allocation, &allocInfo),
                             "failed to create buffer");

    std::memcpy(allocInfo.pMappedData, data.data(), data.size_bytes());
    vmaFlushAllocation(allocator, allocation, 0, byteSize);
    return {buffer, allocation};
}

void destroy_buffer(VmaAllocator const allocator, vk::Buffer const buffer, VmaAllocation const allocation) noexcept {
    if (allocator && buffer)
        vmaDestroyBuffer(allocator, buffer, allocation);
}
} // namespace

namespace sponza {
Resources::Resources(Context &context) : m_context(&context) {
    auto const allocator = m_context->m_allocator;
    auto const mesh = load_mesh(mesh_path);

    std::tie(m_vbo, m_vbAllocation) =
        make_buffer(allocator, std::span{mesh.vertices}, vk::BufferUsageFlagBits::eVertexBuffer);
    std::tie(m_ibo, m_ibAllocation) =
        make_buffer(allocator, std::span{mesh.indices}, vk::BufferUsageFlagBits::eIndexBuffer);
}

Resources::~Resources() noexcept {
    if (auto const allocator = m_context->m_allocator) {
        destroy_buffer(allocator, m_ibo, m_ibAllocation);
        destroy_buffer(allocator, m_vbo, m_vbAllocation);
    }
}
} // namespace sponza