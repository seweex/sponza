#pragma once

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>

namespace sponza {
class Window {
  public:
    Window() noexcept;
    ~Window() noexcept;

    [[nodiscard]] vk::Result init();
    [[nodiscard]] vk::Extent2D get_extent() const noexcept;

    [[nodiscard]] bool should_close() const noexcept;
    void poll_events() const noexcept;

    [[nodiscard]] GLFWwindow* get_handle() const noexcept;

  private:
    struct Deleter {
        void operator()(GLFWwindow *) const noexcept;
    };

    std::unique_ptr<GLFWwindow, Deleter> m_window;
};
} // namespace sponza