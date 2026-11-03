#include <sponza/context.hpp>

#include <expected>
#include <iostream>

#include "utility.hpp"

namespace {
constexpr uint32_t vulkan_version_major = 1;
constexpr uint32_t vulkan_version_minor = 3;
constexpr uint32_t vulkan_version_patch = 0;

constexpr uint32_t window_width = 800;
constexpr uint32_t window_height = 600;
constexpr const char *window_title = "Sponza - Simulation";

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

    sponza::detail::validate(result.has_value(), "failed to create instance");
    return result.value();
}

[[nodiscard]] GLFWwindow *make_window() {
    sponza::detail::validate(glfwInit(), "failed to init glfw");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_ICONIFIED, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    auto const result = glfwCreateWindow(window_width, window_height, window_title, nullptr, nullptr);
    sponza::detail::validate(result, "failed to create window");

    return result;
}

[[nodiscard]] vk::SurfaceKHR make_surface(vk::Instance const instance, GLFWwindow *const window) {
    VkSurfaceKHR surface;

    sponza::detail::validate(glfwCreateWindowSurface(instance, window, nullptr, &surface), "failed to create surface");
    return surface;
}

[[nodiscard]] vkb::PhysicalDevice choose_physical_device(vkb::Instance const &instance, vk::SurfaceKHR const surface) {
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

    sponza::detail::validate(result.has_value(), "failed to choose physical device");
    return *result;
}

[[nodiscard]] vkb::Device make_device(vkb::PhysicalDevice const &physicalDevice) {

    auto result = vkb::DeviceBuilder{physicalDevice}.build();
    sponza::detail::validate(result.has_value(), "failed to build device");

    sponza::detail::validate(*result->get_queue_index(vkb::QueueType::graphics) ==
                                 *result->get_queue_index(vkb::QueueType::present),
                             "separate graphics and present queues are not allowed");
    return *result;
}

[[nodiscard]] caldera::FamilyNumbers get_families(vkb::Device const &device) {
    auto const graphicsIdx = device.get_queue_index(vkb::QueueType::graphics);
    auto const transferIdx = device.get_queue_index(vkb::QueueType::transfer);
    auto const computeIdx = device.get_queue_index(vkb::QueueType::compute);

    sponza::detail::validate(graphicsIdx.has_value(), "failed to get graphics queue index");
    sponza::detail::validate(transferIdx.has_value(), "failed to get transfer queue index");
    sponza::detail::validate(computeIdx.has_value(), "failed to get compute queue index");

    return caldera::FamilyNumbers{.graphics = *graphicsIdx, .transfer = *transferIdx, .compute = *computeIdx};
}

[[nodiscard]] vk::Extent2D get_framebuffer_extent(GLFWwindow *const window) {
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
}

[[nodiscard]] vkb::Swapchain make_swapchain(vkb::Device const &device, vk::SurfaceKHR const surface,
                                            vk::Extent2D const &extent) {
    auto result =
        vkb::SwapchainBuilder{device}
            .set_desired_format({.format = VK_FORMAT_B8G8R8A8_SRGB, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
            .set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
            .set_desired_extent(extent.width, extent.height)
            .set_desired_min_image_count(swapchain_image_count)
            .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            .build();

    sponza::detail::validate(result.has_value(), "failed to build swapchain");
    return *result;
}

[[nodiscard]] std::vector<vk::Image> get_swapchain_images(vkb::Swapchain &swapchain) {
    auto const images = swapchain.get_images();
    sponza::detail::validate(images.has_value(), "failed to get swapchain images");

    return {images->begin(), images->end()};
}

[[nodiscard]] std::vector<vk::ImageView> get_swapchain_image_views(vkb::Swapchain &swapchain) {
    auto const views = swapchain.get_image_views();
    sponza::detail::validate(views.has_value(), "failed to get swapchain image views");

    return {views->begin(), views->end()};
}

} // namespace

namespace sponza {

Context::Context()
    : m_instance(make_instance()), m_window(make_window()),
      m_surface(make_surface(m_instance.instance, m_window.get())),
      m_physicalDevice(choose_physical_device(m_instance, m_surface)), m_device(make_device(m_physicalDevice)),
      m_extent(get_framebuffer_extent(m_window.get())), m_families(get_families(m_device)),
      m_swapchain(make_swapchain(m_device, m_surface, m_extent)), m_swapchainImages(get_swapchain_images(m_swapchain)),
      m_swapchainImageViews(get_swapchain_image_views(m_swapchain)) {}

Context::~Context() noexcept {
    if (auto const device = static_cast<vk::Device>(m_device.device)) {
        for (auto const view : m_swapchainImageViews)
            device.destroyImageView(view);

        vkb::destroy_swapchain(m_swapchain);
        vkb::destroy_device(m_device);
        vkDestroySurfaceKHR(m_instance.instance, m_surface, nullptr);
        m_window.reset();
        vkb::destroy_instance(m_instance);

        glfwTerminate();
    }
}

void Context::WindowDeleter::operator()(GLFWwindow *const handle) const noexcept { glfwDestroyWindow(handle); }

} // namespace sponza