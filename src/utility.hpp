#pragma once

#include <vulkan/vulkan.hpp>

namespace sponza::detail {
void validate(VkResult rCode, const char *msg);
void validate(vk::Result rCode, const char *msg);
void validate(bool test, const char *msg);
} // namespace sponza::detail