#include <sponza/window.hpp>

#include <expected>

namespace {
constexpr uint32_t window_width = 1600;
constexpr uint32_t window_height = 900;
constexpr const char *window_title = "Sponza Simulation\0";

[[nodiscard]] GLFWwindow *make_window() {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_ICONIFIED, GLFW_FALSE);

    return glfwCreateWindow(window_width, window_height, window_title, nullptr, nullptr);
}
} // namespace

namespace sponza {

Window::Window() noexcept = default;
Window::~Window() noexcept {
    m_window.reset();
    glfwTerminate();
}
vk::Result Window::init() {
    if (glfwInit() != GLFW_TRUE)
        return vk::Result::eErrorUnknown;

    auto const window = make_window();
    if (!window)
        return vk::Result::eErrorUnknown;

    m_window = std::unique_ptr<GLFWwindow, Deleter>{window, Deleter{}};
    return vk::Result::eSuccess;
}
vk::Extent2D Window::get_extent() const noexcept {
    int width, height;
    glfwGetFramebufferSize(m_window.get(), &width, &height);
    return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
}
bool Window::should_close() const noexcept { return glfwWindowShouldClose(m_window.get()) == GLFW_TRUE; }
void Window::poll_events() const noexcept { glfwPollEvents(); }
GLFWwindow *Window::get_handle() const noexcept { return  m_window.get(); }
void Window::Deleter::operator()(GLFWwindow *handle) const noexcept { glfwDestroyWindow(handle); }
} // namespace sponza