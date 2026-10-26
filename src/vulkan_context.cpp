#include <sponza/vulkan_context.hpp>

#include <iostream>
#include <ostream>

namespace {
constexpr uint32_t vulkan_version_major = 1;
constexpr uint32_t vulkan_version_minor = 3;
constexpr uint32_t vulkan_version_patch = 0;
constexpr uint32_t swapchain_image_count = 3;

[[nodiscard]] vkb::Instance make_instance() {
    auto result = vkb::InstanceBuilder{}
                      .set_app_name("Sponza Simulation")
                      .set_engine_name("Sponza Engine")
                      .set_app_version(VK_MAKE_API_VERSION(0, 1, 0, 0))
                      .set_engine_version(VK_MAKE_API_VERSION(0, 1, 0, 0))
                      .require_api_version(vulkan_version_major, vulkan_version_minor, vulkan_version_patch)
                      .enable_validation_layers()
                      .use_default_debug_messenger()
                      .build();

    if (!result.has_value()) {
        std::cerr << "Failed to create an instance" << std::endl;
        std::abort();
    }

    return result.value();
}

[[nodiscard]] std::expected<vk::SurfaceKHR, vk::Result> make_surface(vk::Instance const instance,
                                                                     GLFWwindow *const window) {
    VkSurfaceKHR surface;

    if (auto const rCode = glfwCreateWindowSurface(instance, window, nullptr, &surface); rCode < VK_SUCCESS)
        return std::unexpected{static_cast<vk::Result>(rCode)};

    return surface;
}

[[nodiscard]] std::expected<vkb::PhysicalDevice, vk::Result> choose_physical_device(vkb::Instance const &instance,
                                                                                    vk::SurfaceKHR const surface) {
    vk::PhysicalDeviceFeatures features_10{};
    features_10.samplerAnisotropy = VK_TRUE;

    vk::PhysicalDeviceVulkan12Features features_12{};
    features_12.timelineSemaphore = VK_TRUE;
    features_12.separateDepthStencilLayouts = VK_TRUE;
    features_12.bufferDeviceAddress = VK_TRUE;
    features_12.descriptorIndexing = VK_TRUE;
    features_12.descriptorBindingPartiallyBound = VK_TRUE;
    features_12.runtimeDescriptorArray = VK_TRUE;

    vk::PhysicalDeviceVulkan13Features features_13{};
    features_13.dynamicRendering = VK_TRUE;
    features_13.synchronization2 = VK_TRUE;

    auto result = vkb::PhysicalDeviceSelector{instance}
                      .set_surface(surface)
                      .set_minimum_version(vulkan_version_major, vulkan_version_minor)
                      .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
                      .set_required_features(features_10)
                      .set_required_features_12(features_12)
                      .set_required_features_13(features_13)
                      .require_present()
                      .select();

    if (!result.has_value())
        return std::unexpected{static_cast<vk::Result>(result.vk_result())};

    return *result;
}

[[nodiscard]] std::expected<vkb::Device, vk::Result> make_device(vkb::PhysicalDevice const &physicalDevice) {

    auto result = vkb::DeviceBuilder{physicalDevice}.build();

    if (!result.has_value())
        return std::unexpected{static_cast<vk::Result>(result.vk_result())};

    if (*result->get_queue_index(vkb::QueueType::graphics) != *result->get_queue_index(vkb::QueueType::present)) {
        std::cerr << "No graphics queue that can present an image" << std::endl;
        std::abort();
    }

    return *result;
}

[[nodiscard]] std::expected<vkb::Swapchain, vk::Result>
make_swapchain(vkb::Device const &device, vk::SurfaceKHR const surface, vk::Extent2D const &extent) {
    auto result =
        vkb::SwapchainBuilder{device}
            .set_desired_format({.format = VK_FORMAT_B8G8R8A8_SRGB, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
            .set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
            .set_desired_extent(extent.width, extent.height)
            .set_desired_min_image_count(swapchain_image_count)
            .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            .build();

    if (!result.has_value())
        return std::unexpected{static_cast<vk::Result>(result.vk_result())};

    return *result;
}

} // namespace

namespace sponza {
VulkanContext::VulkanContext() noexcept : m_instance(make_instance()) {}
VulkanContext::~VulkanContext() noexcept {
    vkb::destroy_swapchain(m_swapchain);
    vkb::destroy_device(m_device);
    vkDestroySurfaceKHR(m_instance.instance, m_surface, nullptr);
    vkb::destroy_instance(m_instance);
};

vk::Result VulkanContext::init(GLFWwindow* const window, vk::Extent2D const extent) {
    auto surface = make_surface(m_instance.instance, window);
    if (!surface.has_value())
        return surface.error();

    m_surface = *surface;
    auto physicalDevice = choose_physical_device(m_instance, m_surface);

    if (!physicalDevice.has_value())
        return physicalDevice.error();

    m_physicalDevice = *physicalDevice;
    auto device = make_device(m_physicalDevice);

    if (!device.has_value())
        return device.error();

    m_device = *device;
    auto swapchain = make_swapchain(m_device, m_surface, extent);

    if (!swapchain.has_value())
        return swapchain.error();

    m_swapchain = *swapchain;
    return vk::Result::eSuccess;
}

vk::Instance VulkanContext::get_instance() const noexcept { return m_instance.instance; }
vk::PhysicalDevice VulkanContext::get_physical_device() const noexcept { return m_physicalDevice.physical_device; }
vk::Device VulkanContext::get_device() const noexcept { return m_device.device; }
caldera::FamilyNumbers VulkanContext::get_family_indices() const noexcept {
    return {.graphics = *m_device.get_queue_index(vkb::QueueType::graphics),
            .transfer = *m_device.get_queue_index(vkb::QueueType::transfer),
            .compute = *m_device.get_queue_index(vkb::QueueType::compute)};
}
vk::Queue VulkanContext::get_queue(uint32_t const family) const noexcept { return get_device().getQueue(family, 0); }
} // namespace sponza